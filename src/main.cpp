#include <drogon/drogon.h>
#include "corvus/gateway/router.h"
#include "corvus/version.h"

int main()
{
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
