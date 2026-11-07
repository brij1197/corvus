#pragma once
#include "corvus/gateway/blocking_executor.h"
#include <drogon/drogon.h>
#include <functional>
#include <memory>

namespace corvus::gateway
{
    using ResponseCallback = std::function<void(const drogon::HttpResponsePtr &)>;

    void offload(const std::shared_ptr<BlockingExecutor> &executor,
                 const drogon::HttpRequestPtr &req,
                 ResponseCallback callback,
                 std::function<void(ResponseCallback)> work);

    drogon::HttpResponsePtr server_busy_response(const drogon::HttpRequestPtr &req);

} // namespace corvus::gateway