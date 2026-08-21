#include "corvus/db/migration_runner.h"
#include <algorithm>
#include <fstream>
#include <regex>
#include <sstream>
#include <stdexcept>

namespace corvus::db
{
    MigrationRunner::MigrationRunner(std::shared_ptr<PgPool> pool,
                                     std::filesystem::path dir) : pool_(std::move(pool)), dir_(std::move(dir))
    {
        if (!std::filesystem::exists(dir_))
            throw MigrationError("Migration directory does not exist: " + dir_.string());
    }

    std::vector<Migration> MigrationRunner::discover() const
    {
        std::vector<Migration> migrations;

        const std::regex name_re(R"(^(\d+)_(.+)\.sql$)");
        for (const auto &entry : std::filesystem::directory_iterator(dir_))
        {
            if (!entry.is_regular_file())
                continue;

            const auto filename = entry.path().filename().string();
            std::smatch match;
            if (!std::regex_match(filename, match, name_re))
                continue;

            Migration m;
            m.version = std::stoll(match[1].str());
            m.name = entry.path().stem().string();
            m.path = entry.path();

            std::ifstream f(entry.path());
            if (!f.is_open())
                throw MigrationError("Cannot open migration file: " + entry.path().string());

            std::ostringstream ss;
            ss << f.rdbuf();
            m.sql = ss.str();

            if (m.sql.empty())
                throw MigrationError("Migration file is empty: " + entry.path().string());
            migrations.push_back(std::move(m));
        }
        std::sort(migrations.begin(), migrations.end(),
                  [](const Migration &a, const Migration &b)
                  {
                      return a.version < b.version;
                  });
        return migrations;
    }

    std::vector<long long> MigrationRunner::applied_versions()
    {
        auto conn = pool_->acquire();
        ensure_migrations_table(conn.get());

        pqxx::work txn(conn.get());
        std::vector<long long> versions;

        try
        {
            const auto result = txn.exec(
                "SELECT version FROM schema_migrations ORDER BY version");
            txn.commit();

            for (const auto &row : result)
                versions.push_back(row[0].as<long long>());
        }
        catch (const std::exception &e)
        {
            throw MigrationError(
                std::string("Failed to query schema_migrations: ") + e.what());
        }
        return versions;
    }

    long long MigrationRunner::current_version()
    {
        auto conn = pool_->acquire();
        ensure_migrations_table(conn.get());

        pqxx::work txn(conn.get());

        try
        {
            const auto result = txn.exec(
                "SELECT COALESCE(MAX(version), 0) FROM schema_migrations");
            txn.commit();
            return result[0][0].as<long long>();
        }
        catch (const std::exception &e)
        {
            throw MigrationError(
                std::string("Failed to query current version: ") + e.what());
        }
    }

    int MigrationRunner::migrate()
    {
        const auto all = discover();
        const auto applied = applied_versions();
        const auto applied_set = [&]()
        {
            std::vector<long long> s = applied;
            std::sort(s.begin(), s.end());
            return s;
        }();

        int count = 0;
        for (const auto &m : all)
        {
            const bool already_applied = std::binary_search(applied_set.begin(), applied_set.end(), m.version);
            if (already_applied)
                continue;

            auto conn = pool_->acquire();
            apply(conn.get(), m);
            ++count;
        }
        return count;
    }

    void MigrationRunner::ensure_migrations_table(pqxx::connection &conn)
    {
        pqxx::work txn(conn);
        try
        {
            txn.exec(R"(
                CREATE TABLE IF NOT EXISTS schema_migrations (
                    version    BIGINT      PRIMARY KEY,
                    name       TEXT        NOT NULL,
                    applied_at TIMESTAMPTZ NOT NULL DEFAULT now()
                )
            )");
            txn.commit();
        }
        catch (const std::exception &e)
        {
            throw MigrationError(
                std::string("Failed to create schema_migrations: ") + e.what());
        }
    }

    void MigrationRunner::apply(pqxx::connection &conn, const Migration &m)
    {
        {
            pqxx::nontransaction ntxn(conn);
            try
            {
                ntxn.exec(m.sql);
            }
            catch (const std::exception &e)
            {
                throw MigrationError(
                    "Migration " + m.name + " failed: " + e.what());
            }
        }

        pqxx::work txn(conn);
        try
        {
            txn.exec_params(
                "INSERT INTO schema_migrations (version, name) VALUES ($1, $2)"
                " ON CONFLICT (version) DO NOTHING",
                m.version, m.name);
            txn.commit();
        }
        catch (const std::exception &e)
        {
            throw MigrationError(
                "Failed to record migration " + m.name + ": " + e.what());
        }
    }
} // namespace corvus::db