#pragma once
#include "corvus/resources/resource_service.h"
#include <memory>

namespace corvus::resources
{
    void register_resource_routes(std::shared_ptr<ResourceService> service);

} // namespace corvus::resources