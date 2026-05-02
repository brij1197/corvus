#include "corvus/gateway/router.h"
#include "corvus/gateway/health_handler.h"
#include <drogon/drogon.h>

namespace corvus::gateway
{

    void register_routes()
    {
        auto handler = health_handler();

        drogon::app().registerHandler(
            "/health",
            handler,
            {drogon::Get});

        drogon::app().registerHandler(
            "/ready",
            handler,
            {drogon::Get});

        LOG_INFO << "Routes registered";
    }

} // namespace corvus::gateway