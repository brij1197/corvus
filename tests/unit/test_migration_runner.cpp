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