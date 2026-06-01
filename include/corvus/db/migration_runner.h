#pragma once
#include "corvus/db/pg_pool.h"
#include <filesystem>
#include <string>
#include <vector>

namespace corvus::db
{
    struct Migration
    {
        long long version{0};
        std::string name;
        std::string sql;
        std::filesystem::path path;
    };

    struct MigrationError : std::runtime_error
    {
        using std::runtime_error::runtime_error;
    };

    class MigrationRunner
    {
    private:
        void ensure_migrations_table(pqxx::connection &conn);
        void apply(pqxx::connection &conn, const Migration &m);

        std::shared_ptr<PgPool> pool_;
        std::filesystem::path dir_;

    public:
        /// @param pool     Shared connection pool
        /// @param dir      Directory containing .sql migration files
        MigrationRunner(std::shared_ptr<PgPool> pool,
                        std::filesystem::path dir);

        int migrate();
        long long current_version();
        std::vector<Migration> discover() const;
        std::vector<long long> applied_versions();
    };
}