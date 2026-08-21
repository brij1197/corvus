#include <drogon/drogon.h>
#include "corvus/gateway/router.h"
#include "corvus/gateway/rate_limiter.h"
#include "corvus/gateway/request_size_limit.h"
#include "corvus/gateway/request_id_middleware.h"
#include "corvus/auth/middleware.h"
#include "corvus/auth/jwt_validator.h"
#include "corvus/auth/api_key_middleware.h"
#include "corvus/auth/api_key_validator.h"
#include "corvus/db/pg_pool.h"
#include "corvus/db/redis_connection.h"
#include "corvus/db/cache_aside.h"
#include "corvus/db/migration_runner.h"
#include "corvus/resources/resource_repository.h"
#include "corvus/resources/resource_service.h"
#include "corvus/resources/resource_routes.h"
#include "corvus/version.h"
#include "corvus/api/response.h"
#include "corvus/api/request_id.h"
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <memory>
#include <string>

namespace
{

    std::string env_or(const char *var, const char *fallback)
    {
        const char *v = std::getenv(var);
        return (v && *v) ? v : fallback;
    }

    bool env_flag(const char *var, bool fallback)
    {
        const char *v = std::getenv(var);
        if (!v || !*v)
            return fallback;
        const std::string value{v};
        return value == "1" || value == "true" || value == "TRUE";
    }

} // namespace

int main(int argc, char *argv[])
{
    if (argc > 1 && std::strcmp(argv[1], "--health-check") == 0)
    {
        std::cout << "ok\n";
        return 0;
    }

    std::shared_ptr<corvus::auth::JwtValidator> jwt_validator;
    try
    {
        jwt_validator = std::make_shared<corvus::auth::JwtValidator>();
    }
    catch (const corvus::auth::JwtConfigError &e)
    {
        std::cerr << "FATAL: " << e.what() << "\n";
        return 1;
    }

    std::shared_ptr<corvus::auth::ApiKeyValidator> api_key_validator;
    try
    {
        api_key_validator = std::make_shared<corvus::auth::ApiKeyValidator>();
    }
    catch (const corvus::auth::ApiKeyConfigError &e)
    {
        std::cerr << "FATAL: " << e.what() << "\n";
        return 1;
    }

    std::shared_ptr<corvus::db::PgPool> pg_pool;
    try
    {
        corvus::db::PoolConfig pool_config;
        pool_config.connection_string =
            corvus::db::connection_string_from_env();
        pool_config.min_connections = 2;
        pool_config.max_connections = 10;

        pg_pool = std::make_shared<corvus::db::PgPool>(pool_config);
        LOG_INFO << "PostgreSQL pool ready (" << pg_pool->size()
                 << " connections)";
    }
    catch (const std::exception &e)
    {
        std::cerr << "FATAL: PostgreSQL unavailable: " << e.what() << "\n";
        return 1;
    }

    if (env_flag("CORVUS_RUN_MIGRATIONS", true))
    {
        try
        {
            const auto migrations_dir =
                env_or("CORVUS_MIGRATIONS_DIR",
                       "/usr/local/share/corvus/migrations");

            corvus::db::MigrationRunner runner(pg_pool, migrations_dir);
            const int applied = runner.migrate();

            if (applied > 0)
                LOG_INFO << "Applied " << applied << " migration(s), schema at "
                         << "version " << runner.current_version();
            else
                LOG_INFO << "Schema up to date at version "
                         << runner.current_version();
        }
        catch (const std::exception &e)
        {
            std::cerr << "FATAL: migrations failed: " << e.what() << "\n";
            return 1;
        }
    }
    else
    {
        LOG_INFO << "Skipping migrations (CORVUS_RUN_MIGRATIONS=false)";
    }

    std::shared_ptr<corvus::db::RedisConnection> redis;
    std::shared_ptr<corvus::db::CacheAside> cache;
    try
    {
        redis = std::make_shared<corvus::db::RedisConnection>(
            corvus::db::redis_config_from_env());
        cache = std::make_shared<corvus::db::CacheAside>(*redis);
        LOG_INFO << "Redis cache ready";
    }
    catch (const std::exception &e)
    {
        std::cerr << "FATAL: Redis unavailable: " << e.what() << "\n";
        return 1;
    }

    auto resource_repository =
        std::make_shared<corvus::resources::ResourceRepository>(pg_pool);
    auto resource_service =
        std::make_shared<corvus::resources::ResourceService>(
            resource_repository, cache);

    corvus::gateway::register_request_id_middleware();
    corvus::gateway::register_request_size_limit_middleware();

    auto rate_limiter = std::make_shared<corvus::gateway::RateLimiter>();
    corvus::gateway::register_rate_limit_middleware(rate_limiter);

    // Register auth middleware (runs before route handlers)
    corvus::auth::register_jwt_middleware(jwt_validator);
    corvus::auth::register_api_key_middleware(api_key_validator);

    LOG_INFO << "Corvus " << corvus::version::string() << " starting";

    // Register routes
    corvus::gateway::register_routes();
    corvus::resources::register_resource_routes(resource_service);

    // Configure and run Drogon
    drogon::app()
        .setLogPath("")
        .setLogLevel(trantor::Logger::kInfo)
        .addListener("0.0.0.0", 8080)
        .setThreadNum(4)
        .setClientMaxBodySize(10 * 1024 * 1024)
        .setClientMaxMemoryBodySize(10 * 1024 * 1024)
        .setCustomErrorHandler(
            [](drogon::HttpStatusCode code,
               const drogon::HttpRequestPtr &req) -> drogon::HttpResponsePtr
            {
                const auto request_id = corvus::api::get_request_id(req);

                drogon::HttpResponsePtr resp;

                if (code == drogon::k404NotFound)
                {
                    resp = corvus::api::respond_error(
                        corvus::api::ErrorCode::not_found,
                        "The requested resource does not exist.",
                        request_id);
                }
                else if (code == drogon::k405MethodNotAllowed)
                {
                    resp = corvus::api::respond_error(
                        corvus::api::ErrorCode::bad_request,
                        "Method not allowed.",
                        request_id);
                }
                else
                {
                    resp = corvus::api::respond_error(
                        corvus::api::ErrorCode::internal_error,
                        "An unexpected error occurred.",
                        request_id);
                }

                resp->addHeader("X-Request-ID", request_id);
                return resp;
            })
        .run();

    return 0;
}
