#pragma once
#include "corvus/api/envelope.h"
#include <drogon/drogon.h>
#include <functional>

namespace corvus::gateway
{

    using HandlerFunc = std::function<void(
        const drogon::HttpRequestPtr &,
        std::function<void(const drogon::HttpResponsePtr &)> &&)>;

    HandlerFunc not_found_handler();

} // namespace corvus::gateway