#include "corvus/resources/resource_routes.h"
#include "corvus/resources/resource_handler.h"
#include <drogon/drogon.h>

namespace corvus::resources
{
    void register_resource_routes(std::shared_ptr<ResourceService> service)
    {
        (void)service;

        auto create = create_resource_handler(service);
        drogon::app().registerHandler(
            "/v1/resources",
            [create](const drogon::HttpRequestPtr &req,
                     std::function<void(const drogon::HttpResponsePtr &)> &&cb)
            { create(req, std::move(cb)); },
            {drogon::Post});

        auto list = list_resources_handler(service);
        drogon::app().registerHandler(
            "/v1/resources",
            [list](const drogon::HttpRequestPtr &req,
                   std::function<void(const drogon::HttpResponsePtr &)> &&cb)
            { list(req, std::move(cb)); },
            {drogon::Get});

        auto get_one = get_resource_handler(service);
        drogon::app().registerHandler(
            "/v1/resources/{id}",
            [get_one](const drogon::HttpRequestPtr &req,
                      std::function<void(const drogon::HttpResponsePtr &)> &&cb,
                      const std::string &id)
            { get_one(req, std::move(cb), id); },
            {drogon::Get});

        auto update = update_resource_handler(service);
        drogon::app().registerHandler(
            "/v1/resources/{id}",
            [update](const drogon::HttpRequestPtr &req,
                     std::function<void(const drogon::HttpResponsePtr &)> &&cb,
                     const std::string &id)
            { update(req, std::move(cb), id); },
            {drogon::Patch});

        auto remove = delete_resource_handler(service);
        drogon::app().registerHandler(
            "/v1/resources/{id}",
            [remove](const drogon::HttpRequestPtr &req,
                     std::function<void(const drogon::HttpResponsePtr &)> &&cb,
                     const std::string &id)
            { remove(req, std::move(cb), id); },
            {drogon::Delete});

        LOG_INFO << "Resource routes registered (5 endpoints)";
    }
} // namespace corvus::resources