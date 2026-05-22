#include <drogon/drogon.h>
#include "corvus/gateway/router.h"
#include "corvus/auth/middleware.h"
#include "corvus/auth/jwt_validator.h"
#include "corvus/version.h"
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

    std::shared_ptr<corvus::auth::JwtValidator> validator;
    try
    {
        validator = std::make_shared<corvus::auth::JwtValidator>();
    }
    catch (const corvus::auth::JwtConfigError &e)
    {
        std::cerr << "FATAL: " << e.what() << "\n";
        return 1;
    }

    // Register auth middleware (runs before route handlers)
    corvus::auth::register_jwt_middleware(validator);

    LOG_INFO << "Corvus " << corvus::version::string() << " starting";

    // Register routes
    corvus::gateway::register_routes();

    // Configure and run Drogon
    drogon::app()
        .setLogPath("")
        .setLogLevel(trantor::Logger::kInfo)
        .addListener("0.0.0.0", 8080)
        .setThreadNum(4)
        .run();

    return 0;
}
