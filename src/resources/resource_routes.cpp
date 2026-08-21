#include "corvus/resources/resource_routes.h"
#include <drogon/drogon.h>

namespace corvus::resources
{
    void register_resource_routes(std::shared_ptr<ResourceService> service)
    {
        (void)service;

        LOG_INFO << "Resource routes registered (0 endpoints - pending CORV-37..41)";
    }
} // namespace corvus::resources