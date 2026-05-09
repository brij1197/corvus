#include <drogon/drogon.h>
#include "corvus/gateway/router.h"
#include "corvus/version.h"
#include <cstring>
#include <iostream>

int main(int argc, char *argv[])
{
    if (argc > 1 && std::strcmp(argv[1], "--health-check") == 0)
    {
        std::cout << "ok\n";
        return 0;
    }

    LOG_INFO << "Corvus " << corvus::version::string() << " starting";

    corvus::gateway::register_routes();

    drogon::app()
        .setLogPath("./")
        .setLogLevel(trantor::Logger::kInfo)
        .addListener("0.0.0.0", 8080)
        .setThreadNum(4)
        .run();

    return 0;
}
