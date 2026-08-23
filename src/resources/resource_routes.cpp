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
        LOG_INFO << "Resource routes registered (1 endpoint)";
    }
} // namespace corvus::resources