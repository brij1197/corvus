#include "corvus/resources/resource_routes.h"
#include "corvus/gateway/offload.h"
#include "corvus/resources/resource_handler.h"
#include <drogon/drogon.h>

namespace corvus::resources
{
    namespace
    {
        using gateway::ResponseCallback;

        auto on_worker(std::shared_ptr<gateway::BlockingExecutor> executor, ResourceHandler handler)
        {
            return [executor, handler](const drogon::HttpRequestPtr &req, ResponseCallback &&cb)
            {
                gateway::offload(executor, req, std::move(cb),
                                 [handler, req](ResponseCallback reply)
                                 { handler(req, std::move(reply)); });
            };
        }

        auto on_worker(std::shared_ptr<gateway::BlockingExecutor> executor, ResourceIdHandler handler)
        {
            return [executor, handler](const drogon::HttpRequestPtr &req, ResponseCallback &&cb,
                                       const std::string &id)
            {
                gateway::offload(executor, req, std::move(cb),
                                 [handler, req, id](ResponseCallback reply)
                                 { handler(req, std::move(reply), id); });
            };
        }
    } // namespace

    void register_resource_routes(std::shared_ptr<ResourceService> service,
                                  std::shared_ptr<gateway::BlockingExecutor> executor)
    {
        drogon::app().registerHandler(
            "/v1/resources", on_worker(executor, create_resource_handler(service)), {drogon::Post});

        drogon::app().registerHandler(
            "/v1/resources", on_worker(executor, list_resources_handler(service)), {drogon::Get});

        drogon::app().registerHandler(
            "/v1/resources/{id}", on_worker(executor, get_resource_handler(service)), {drogon::Get});

        drogon::app().registerHandler(
            "/v1/resources/{id}", on_worker(executor, update_resource_handler(service)), {drogon::Patch});

        drogon::app().registerHandler(
            "/v1/resources/{id}", on_worker(executor, delete_resource_handler(service)), {drogon::Delete});

        LOG_INFO << "Resource routes registered (5 endpoints, on "
                 << executor->threads() << " worker threads)";
    }
} // namespace corvus::resources