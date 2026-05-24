#include <drogon/drogon.h>
#include "corvus/gateway/router.h"
#include "corvus/gateway/rate_limiter.h"
#include "corvus/gateway/request_size_limit.h"
#include "corvus/auth/middleware.h"
#include "corvus/auth/jwt_validator.h"
#include "corvus/auth/api_key_middleware.h"
#include "corvus/auth/api_key_validator.h"
#include "corvus/version.h"
#include "corvus/api/response.h"
#include "corvus/api/request_id.h"
#include <cstring>
#include <iostream>
#include <memory>

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

    corvus::gateway::register_request_size_limit_middleware();

    auto rate_limiter = std::make_shared<corvus::gateway::RateLimiter>();
    corvus::gateway::register_rate_limit_middleware(rate_limiter);

    // Register auth middleware (runs before route handlers)
    corvus::auth::register_jwt_middleware(jwt_validator);
    corvus::auth::register_api_key_middleware(api_key_validator);

    LOG_INFO << "Corvus " << corvus::version::string() << " starting";

    // Register routes
    corvus::gateway::register_routes();

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

                if (code == drogon::k404NotFound)
                {
                    return corvus::api::respond_error(
                        corvus::api::ErrorCode::not_found,
                        "The requested resource does not exist.",
                        request_id);
                }

                if (code == drogon::k405MethodNotAllowed)
                {
                    return corvus::api::respond_error(
                        corvus::api::ErrorCode::bad_request,
                        "Method not allowed.",
                        request_id);
                }

                return corvus::api::respond_error(
                    corvus::api::ErrorCode::internal_error,
                    "An unexpected error occurred.",
                    request_id);
            })
        .run();

    return 0;
}
