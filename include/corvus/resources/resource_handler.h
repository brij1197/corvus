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

    using ResourceIdHandler =
        std::function<void(const drogon::HttpRequestPtr &, HandlerCallback &&,
                           const std::string &)>;

    ResourceHandler create_resource_handler(std::shared_ptr<ResourceService> service);

    ResourceIdHandler get_resource_handler(std::shared_ptr<ResourceService> service);

    std::optional<int> parse_limit_param(const std::string &raw);

    ResourceHandler list_resources_handler(std::shared_ptr<ResourceService> service);

    ResourceIdHandler update_resource_handler(std::shared_ptr<ResourceService> service);

} // namespace corvus::resources