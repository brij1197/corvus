#include "corvus/gateway/router.h"
#include "corvus/gateway/health_handler.h"
#include "corvus/gateway/not_found_handler.h"
#include "corvus/api/response.h"
#include "corvus/api/request_id.h"
#include <drogon/drogon.h>

namespace corvus::gateway
{

    void register_routes()
    {
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

        LOG_INFO << "Routes registered";
    }

    void register_catchall_routes()
    {
        auto nf = not_found_handler();
        const std::vector<drogon::internal::HttpConstraint> all_methods = {
            drogon::Get, drogon::Post, drogon::Put,
            drogon::Patch, drogon::Delete, drogon::Options};

        drogon::app().registerHandler(
            "/{a}",
            [nf](const drogon::HttpRequestPtr &req,
                std::function<void(const drogon::HttpResponsePtr &)> &&cb)
            { nf(req, std::move(cb)); },
            all_methods);

        drogon::app().registerHandler(
            "/{a}/{b}",
            [nf](const drogon::HttpRequestPtr &req,
                std::function<void(const drogon::HttpResponsePtr &)> &&cb)
            { nf(req, std::move(cb)); },
            all_methods);

        drogon::app().registerHandler(
            "/{a}/{b}/{c}",
            [nf](const drogon::HttpRequestPtr &req,
                std::function<void(const drogon::HttpResponsePtr &)> &&cb)
            { nf(req, std::move(cb)); },
            all_methods);

        drogon::app().registerHandler(
            "/{a}/{b}/{c}/{d}",
            [nf](const drogon::HttpRequestPtr &req,
                std::function<void(const drogon::HttpResponsePtr &)> &&cb)
            { nf(req, std::move(cb)); },
            all_methods);

        LOG_INFO << "Catch-all routes registered";
    }

} // namespace corvus::gateway