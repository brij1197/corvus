#include "corvus/gateway/router.h"
#include "corvus/gateway/health_handler.h"
#include "corvus/gateway/not_found_handler.h"
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

        // v1 API placeholder
        // Example:
        //   drogon::app().registerHandler(
        //       "/v1/resources", resources_handler(), {drogon::Get, drogon::Post});

        // Catch-all 404
        // Must be registered last — Drogon matches routes in registration order.
        drogon::app().registerHandler(
            "/{catchall}",
            not_found_handler(),
            {drogon::Get, drogon::Post, drogon::Put,
             drogon::Patch, drogon::Delete, drogon::Options});

        LOG_INFO << "Routes registered";
    }

} // namespace corvus::gateway