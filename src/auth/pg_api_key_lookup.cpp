#include "corvus/auth/pg_api_key_lookup.h"
#include <pqxx/pqxx>

namespace corvus::auth
{
    ApiKeyLookup make_pg_api_key_lookup(std::shared_ptr<db::PgPool> pool)
    {
        return [pool = std::move(pool)](const std::string &key_hash) -> std::optional<ApiKeyRecord>
        {
            try
            {
                auto conn = pool->acquire();
                pqxx::read_transaction txn(conn.get());
                const auto rows = txn.exec_params(
                    "SELECT client_id::text, scopes, "
                    "       extract(epoch FROM expires_at)::bigint "
                    "FROM api_keys "
                    "WHERE key_hash = $1 "
                    "  AND is_active "
                    "  AND (expires_at IS NULL OR expires_at > now())",
                    key_hash);

                if (rows.empty())
                    return std::nullopt;

                const auto &row = rows[0];
                ApiKeyRecord record;
                record.client_id = row[0].as<std::string>();
                record.scopes = row[1].as<std::string>();
                if (!row[2].is_null())
                    record.expires_at = std::chrono::system_clock::time_point{
                        std::chrono::seconds{row[2].as<long long>()}};
                return record;
            }
            catch (const std::exception &e)
            {
                throw ApiKeyBackendError(
                    std::string("API key store unavailable: ") + e.what());
            }
        };
    }
} // namespace corvus::auth