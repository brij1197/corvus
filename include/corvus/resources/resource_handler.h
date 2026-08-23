#pragma once
#include "corvus/resources/resource_service.h"
#include <drogon/drogon.h>
#include <functional>
#include <memory>

namespace corvus::resources
{
    using HandlerCallback = std::function<void(const drogon::HttpResponsePtr &)>; 
    using ResourceHandler =
        std::function<void(const drogon::HttpRequestPtr &, HandlerCallback &&)>;

    ResourceHandler create_resource_handler(std::shared_ptr<ResourceService> service);

} // namespace corvus::resources