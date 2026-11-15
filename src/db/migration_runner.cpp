#include "corvus/db/migration_runner.h"
#include <algorithm>
#include <cctype>
#include <fstream>
#include <regex>
#include <sstream>
#include <stdexcept>

namespace corvus::db
{
    bool manages_own_transaction(const std::string &sql)
    {
        static const std::regex kControl(
            R"(^\s*(BEGIN|COMMIT|ROLLBACK|ABORT|END|START\s+TRANSACTION)\b)",
            std::regex::icase);

        std::string stripped;
        stripped.reserve(sql.size());
        for (std::size_t i = 0; i < sql.size();)
        {
            if (sql.compare(i, 2, "--") == 0)
            {
                i = sql.find('\n', i);
                if (i == std::string::npos)
                    break;
                continue;
            }
            if (sql.compare(i, 2, "/*") == 0)
            {
                const auto end = sql.find("*/", i + 2);
                i = end == std::string::npos ? sql.size() : end + 2;
                stripped.push_back(' ');
                continue;
            }
            if (sql[i] == '\'' || sql[i] == '"')
            {
                const char q = sql[i];
                const auto end = sql.find(q, i + 1);
                i = end == std::string::npos ? sql.size() : end + 1;
                stripped.push_back(' ');
                continue;
            }
            if (sql[i] == '$')
            {
                const auto close = sql.find('$', i + 1);
                if (close != std::string::npos)
                {
                    const auto tag = sql.substr(i, close - i + 1);
                    const bool valid_tag = std::all_of(
                        tag.begin() + 1, tag.end() - 1,
                        [](char c)
                        { return std::isalnum(static_cast<unsigned char>(c)) || c == '_'; });
                    if (valid_tag)
                    {
                        const auto end = sql.find(tag, close + 1);
                        i = end == std::string::npos ? sql.size() : end + tag.size();
                        stripped.push_back(' ');
                        continue;
                    }
                }
            }
            stripped.push_back(sql[i]);
            ++i;
        }

        std::size_t start = 0;
        while (start <= stripped.size())
        {
            const auto end = stripped.find(';', start);
            const auto stmt = stripped.substr(start, end == std::string::npos ? std::string::npos : end - start);
            if (std::regex_search(stmt, kControl))
                return true;
            if (end == std::string::npos)
                break;
            start = end + 1;
        }
        return false;
    }

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
            if (manages_own_transaction(m.sql))
                throw MigrationError(
                    "Migration " + filename + " contains BEGIN/COMMIT; remove them. "
                    "The runner applies each file in its own transaction.");
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
        return applied_versions(conn.get());
    }

    std::vector<long long> MigrationRunner::applied_versions(pqxx::connection &conn)
    {
        pqxx::work txn(conn);
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

        // One session for the whole run: advisory locks belong to a session,
        // so the lock, the reads and every apply must share this connection.
        auto conn = pool_->acquire();
        pqxx::connection &c = conn.get();
        const auto key = std::to_string(kLockKey);

        try
        {
            pqxx::nontransaction(c).exec("SELECT pg_advisory_lock(" + key + ")");
        }
        catch (const std::exception &e)
        {
            throw MigrationError(std::string("Failed to take migration lock: ") + e.what());
        }

        // The connection goes back to the pool afterwards; it must not keep
        // holding the lock, even if a migration failed.
        struct Unlock
        {
            pqxx::connection &c;
            const std::string &key;
            ~Unlock()
            {
                try
                {
                    pqxx::nontransaction(c).exec("SELECT pg_advisory_unlock(" + key + ")");
                }
                catch (...)
                {
                    // A broken connection drops the lock with the session.
                }
            }
        } unlock{c, key};

        // Read what's applied only once we hold the lock: another instance may
        // have just finished the same migrations.
        ensure_migrations_table(c);
        auto applied = applied_versions(c);
        std::sort(applied.begin(), applied.end());

        int count = 0;
        for (const auto &m : all)
        {
            if (std::binary_search(applied.begin(), applied.end(), m.version))
                continue;
            apply(c, m);
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
        try
        {
            pqxx::work txn(conn);
            txn.exec(m.sql);
            // 0001 records itself (it creates schema_migrations), hence DO NOTHING.
            txn.exec_params(
                "INSERT INTO schema_migrations (version, name) VALUES ($1, $2)"
                " ON CONFLICT (version) DO NOTHING",
                m.version, m.name);
            txn.commit();
        }
        catch (const std::exception &e)
        {
            throw MigrationError("Migration " + m.name + " failed and was rolled back: " + e.what());
        }
    }
} // namespace corvus::db