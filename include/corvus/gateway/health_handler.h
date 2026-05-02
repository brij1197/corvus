#pragma once
#include <drogon/drogon.h>

namespace corvus::gateway
{
    using HandlerFunc = std::function<void(
        const drogon::HttpRequestPtr &,
        std::function<void(const drogon::HttpResponsePtr &)> &&)>;

    HandlerFunc health_handler();

} // namespace corvus::gateway