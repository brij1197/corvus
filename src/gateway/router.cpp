#include "corvus/gateway/router.h"
#include "corvus/gateway/health_handler.h"
#include "corvus/gateway/not_found_handler.h"
#include <drogon/drogon.h>

namespace corvus::gateway
{

    void register_routes()
    {
        auto handler = health_handler();

        auto h = health_handler();
        drogon::app().registerHandler(
            "/health",
            [h](const drogon::HttpRequestPtr &req,
                std::function<void(const drogon::HttpResponsePtr &)> &&cb)
            { h(req, std::move(cb)); },
            {drogon::Get});

        drogon::app().registerHandler(
            "/ready",
            [h](const drogon::HttpRequestPtr &req,
                std::function<void(const drogon::HttpResponsePtr &)> &&cb)
            { h(req, std::move(cb)); },
            {drogon::Get});

        auto nf = not_found_handler();
        drogon::app().registerHandler(
            "/{catchall}",
            [nf](const drogon::HttpRequestPtr &req,
                 std::function<void(const drogon::HttpResponsePtr &)> &&cb)
            { nf(req, std::move(cb)); },
            {drogon::Get, drogon::Post, drogon::Put,
             drogon::Patch, drogon::Delete, drogon::Options});

        LOG_INFO << "Routes registered";
    }

} // namespace corvus::gateway