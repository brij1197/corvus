#pragma once
#include "corvus/gateway/blocking_executor.h"
#include "corvus/resources/resource_service.h"
#include <memory>

namespace corvus::resources
{
    void register_resource_routes(std::shared_ptr<ResourceService> service,
                                  std::shared_ptr<gateway::BlockingExecutor> executor);

} // namespace corvus::resources