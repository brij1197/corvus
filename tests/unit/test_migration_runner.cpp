#include <gtest/gtest.h>
#include "corvus/db/migration_runner.h"
#include <filesystem>
#include <fstream>
#include <regex>

namespace fs = std::filesystem;
using namespace corvus::db;

class MigrationDiscoveryTest : public ::testing::Test
{
protected:
    fs::path tmp_dir;

    void SetUp() override
    {
        tmp_dir = fs::temp_directory_path() / "corvus_migration_test";
        fs::create_directories(tmp_dir);
    }

    void TearDown() override
    {
        fs::remove_all(tmp_dir);
    }

    void write_file(const std::string &name, const std::string &content)
    {
        std::ofstream f(tmp_dir / name);
        f << content;
    }
};

TEST(MigrationErrorTest, IsStdRuntimeError)
{
    MigrationError err("something went wrong");
    EXPECT_STREQ(err.what(), "something went wrong");
    EXPECT_TRUE((std::is_base_of<std::runtime_error, MigrationError>::value));
}

TEST(MigrationRunnerConstructorTest, ThrowsOnMissingDirectory)
{
    MigrationError err("Migration directory does not exist: /nonexistent/path");
    EXPECT_STREQ(err.what(),
                 "Migration directory does not exist: /nonexistent/path");
    EXPECT_TRUE((std::is_base_of<std::runtime_error, MigrationError>::value));
}

TEST_F(MigrationDiscoveryTest, DiscoversSingleFile)
{
    PoolConfig cfg{
        .connection_string = "host=127.0.0.1 port=19999 dbname=x connect_timeout=1",
        .min_connections = 1,
        .max_connections = 1};

    write_file("0001_initial_schema.sql", "SELECT 1;");

    const std::regex name_re(R"(^(\d+)_(.+)\.sql$)");
    std::smatch match;
    std::string filename = "0001_initial_schema.sql";
    EXPECT_TRUE(std::regex_match(filename, match, name_re));
    EXPECT_EQ(std::stoll(match[1].str()), 1LL);
    EXPECT_EQ(match[2].str(), "initial_schema");
}

TEST_F(MigrationDiscoveryTest, ParsesVersionCorrectly)
{
    const std::regex name_re(R"(^(\d+)_(.+)\.sql$)");
    std::smatch match;

    struct TestCase
    {
        std::string filename;
        long long version;
        std::string stem;
    };
    std::vector<TestCase> cases = {
        {"0001_initial_schema.sql", 1, "initial_schema"},
        {"0002_add_indexes.sql", 2, "add_indexes"},
        {"0010_timescale.sql", 10, "timescale"},
        {"1000_big_migration.sql", 1000, "big_migration"},
    };

    for (const auto &tc : cases)
    {
        EXPECT_TRUE(std::regex_match(tc.filename, match, name_re))
            << "No match for: " << tc.filename;
        EXPECT_EQ(std::stoll(match[1].str()), tc.version);
        EXPECT_EQ(match[2].str(), tc.stem);
    }
}

TEST_F(MigrationDiscoveryTest, IgnoresNonSqlFiles)
{
    const std::regex name_re(R"(^(\d+)_(.+)\.sql$)");
    std::smatch match;

    std::vector<std::string> non_sql = {
        "README.md",
        "0001_schema.txt",
        "schema.sql",
        "abc_schema.sql",
        ".gitkeep",
    };

    for (const auto &f : non_sql)
    {
        EXPECT_FALSE(std::regex_match(f, match, name_re))
            << "Should not match: " << f;
    }
}

TEST_F(MigrationDiscoveryTest, SortsByVersionAscending)
{
    std::vector<long long> versions = {3, 1, 10, 2};
    std::sort(versions.begin(), versions.end());
    EXPECT_EQ(versions, (std::vector<long long>{1, 2, 3, 10}));
}

TEST_F(MigrationDiscoveryTest, EmptyDirectoryReturnsNoMigrations)
{
    std::smatch dummy;
    const std::string empty_str;
    const std::regex name_re(R"(^(\d+)_(.+)\.sql$)");
    EXPECT_FALSE(std::regex_match(empty_str, dummy, name_re));
}

TEST(MigrationStructTest, CanBeConstructedAndMoved)
{
    Migration m;
    m.version = 42;
    m.name = "0042_test";
    m.sql = "SELECT 42;";
    m.path = "/tmp/0042_test.sql";

    Migration m2 = std::move(m);
    EXPECT_EQ(m2.version, 42);
    EXPECT_EQ(m2.name, "0042_test");
    EXPECT_EQ(m2.sql, "SELECT 42;");
}

TEST(MigrationStructTest, DefaultVersionIsZero)
{
    Migration m;
    EXPECT_EQ(m.version, 0);
}

#include <atomic>
#include <random>
#include <thread>

TEST(ManagesOwnTransactionTest, DetectsTransactionControl)
{
    EXPECT_TRUE(manages_own_transaction("BEGIN;\nCREATE TABLE t (x int);\nCOMMIT;"));
    EXPECT_TRUE(manages_own_transaction("create table t (x int);\n  commit ;"));
    EXPECT_TRUE(manages_own_transaction("START TRANSACTION; SELECT 1;"));
    EXPECT_TRUE(manages_own_transaction("SELECT 1; ROLLBACK"));
}

TEST(ManagesOwnTransactionTest, IgnoresFunctionBodiesCommentsAndStrings)
{
    EXPECT_FALSE(manages_own_transaction(R"(
        -- BEGIN; in a comment
        /* COMMIT; in a block comment */
        CREATE FUNCTION f() RETURNS trigger LANGUAGE plpgsql AS $$
        BEGIN
            RETURN NEW;
        END;
        $$;
        CREATE FUNCTION g() RETURNS int LANGUAGE plpgsql AS $body$
        BEGIN RETURN 1; END;
        $body$;
        INSERT INTO notes VALUES ('BEGIN; COMMIT;');
        CREATE TABLE "commit" (x int);
    )"));
}

TEST(ManagesOwnTransactionTest, ShippedMigrationsAreClean)
{
    const fs::path dir = fs::path(CORVUS_SOURCE_DIR) / "db" / "migrations";
    ASSERT_TRUE(fs::exists(dir)) << dir;
    for (const auto &entry : fs::directory_iterator(dir))
    {
        if (entry.path().extension() != ".sql")
            continue;
        std::ifstream f(entry.path());
        const std::string sql((std::istreambuf_iterator<char>(f)), {});
        EXPECT_FALSE(manages_own_transaction(sql)) << entry.path().filename();
    }
}

namespace
{
    class LiveMigrationTest : public ::testing::Test
    {
    protected:
        void SetUp() override
        {
            schema_ = "mtest_" + std::to_string(std::random_device{}());
            try
            {
                pqxx::connection c(connection_string_from_env());
                pqxx::work(c).exec("SELECT 1");
                pqxx::nontransaction(c).exec("CREATE SCHEMA " + schema_);
            }
            catch (const std::exception &)
            {
                GTEST_SKIP() << "Postgres not reachable";
            }
            dir_ = fs::temp_directory_path() / schema_;
            fs::create_directories(dir_);
        }

        void TearDown() override
        {
            if (!dir_.empty())
                fs::remove_all(dir_);
            try
            {
                pqxx::connection c(connection_string_from_env());
                pqxx::nontransaction(c).exec("DROP SCHEMA IF EXISTS " + schema_ + " CASCADE");
            }
            catch (...)
            {
            }
        }

        std::shared_ptr<PgPool> make_pool(std::size_t max = 2)
        {
            PoolConfig cfg;
            cfg.connection_string = connection_string_from_env() + " options=-csearch_path=" + schema_;
            cfg.min_connections = 1;
            cfg.max_connections = max;
            return std::make_shared<PgPool>(cfg);
        }

        void write(const std::string &name, const std::string &sql)
        {
            std::ofstream(dir_ / name) << sql;
        }

        long long count(const std::string &sql)
        {
            pqxx::connection c(connection_string_from_env() + " options=-csearch_path=" + schema_);
            return pqxx::work(c).exec(sql)[0][0].as<long long>();
        }

        std::string schema_;
        fs::path dir_;
    };
} // namespace

TEST_F(LiveMigrationTest, ConcurrentRunnersApplyEachMigrationOnce)
{
    write("0001_slow.sql", "CREATE TABLE marks (n int); SELECT pg_sleep(1); INSERT INTO marks VALUES (1);");
    write("0002_more.sql", "INSERT INTO marks VALUES (2);");

    std::atomic<int> applied{0}, failures{0};
    auto run = [&]
    {
        try
        {
            applied += MigrationRunner(make_pool(), dir_).migrate();
        }
        catch (const std::exception &e)
        {
            ++failures;
            ADD_FAILURE() << e.what();
        }
    };
    std::thread a(run), b(run);
    a.join();
    b.join();

    EXPECT_EQ(failures, 0);
    EXPECT_EQ(applied, 2) << "each migration should be applied by exactly one runner";
    EXPECT_EQ(count("SELECT count(*) FROM marks"), 2);
    EXPECT_EQ(count("SELECT count(*) FROM schema_migrations"), 2);
}

TEST_F(LiveMigrationTest, FailingMigrationLeavesNoPartialSchema)
{
    write("0001_half.sql", "CREATE TABLE first_half (x int); SELECT * FROM no_such_table;");

    EXPECT_THROW(MigrationRunner(make_pool(), dir_).migrate(), MigrationError);

    EXPECT_EQ(count("SELECT count(*) FROM information_schema.tables "
                    "WHERE table_schema = current_schema() AND table_name = 'first_half'"),
              0)
        << "the table from the first statement must be rolled back";
    EXPECT_EQ(count("SELECT count(*) FROM schema_migrations"), 0);
}

TEST_F(LiveMigrationTest, FixedMigrationIsRetriedOnNextRun)
{
    write("0001_t.sql", "CREATE TABLE t (x int); SELECT * FROM no_such_table;");
    EXPECT_THROW(MigrationRunner(make_pool(), dir_).migrate(), MigrationError);

    write("0001_t.sql", "CREATE TABLE t (x int);");
    EXPECT_EQ(MigrationRunner(make_pool(), dir_).migrate(), 1);
}

TEST_F(LiveMigrationTest, LockIsReleasedAfterAFailure)
{
    write("0001_bad.sql", "SELECT * FROM no_such_table;");
    auto pool = make_pool(1);
    EXPECT_THROW(MigrationRunner(pool, dir_).migrate(), MigrationError);

    pqxx::connection other(connection_string_from_env());
    const auto got = pqxx::work(other)
                         .exec("SELECT pg_try_advisory_lock(" + std::to_string(MigrationRunner::kLockKey) + ")")[0][0]
                         .as<bool>();
    EXPECT_TRUE(got) << "a failed run must not leave the migration lock held";
    pqxx::nontransaction(other).exec("SELECT pg_advisory_unlock(" + std::to_string(MigrationRunner::kLockKey) + ")");
}

TEST_F(LiveMigrationTest, RejectsFilesThatManageTheirOwnTransaction)
{
    write("0001_old_style.sql", "BEGIN; CREATE TABLE t (x int); COMMIT;");
    EXPECT_THROW(MigrationRunner(make_pool(), dir_).discover(), MigrationError);
}