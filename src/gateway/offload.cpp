#include "corvus/gateway/offload.h"
#include "corvus/api/request_id.h"
#include "corvus/api/response.h"

namespace corvus::gateway
{
    drogon::HttpResponsePtr server_busy_response(const drogon::HttpRequestPtr &req)
    {
        const auto request_id = corvus::api::get_request_id(req);
        auto resp = corvus::api::respond_error(
            corvus::api::ErrorCode::service_unavailable,
            "Server is busy. Try again shortly.",
            request_id);
        resp->addHeader("X-Request-ID", request_id);
        resp->addHeader("Retry-After", "1");
        return resp;
    }

    void offload(const std::shared_ptr<BlockingExecutor> &executor,
                 const drogon::HttpRequestPtr &req,
                 ResponseCallback callback,
                 std::function<void(ResponseCallback)> work)
    {
        if (executor->on_worker_thread())
        {
            work(std::move(callback));
            return;
        }

        auto shared_cb = std::make_shared<ResponseCallback>(std::move(callback));
        const bool accepted = executor->try_submit(
            [shared_cb, work = std::move(work)]
            { work(*shared_cb); });

        if (!accepted)
        {
            LOG_WARN << "blocking executor full (" << executor->queued()
                     << " queued); shedding request "
                     << corvus::api::get_request_id(req);
            (*shared_cb)(server_busy_response(req));
        }
    }

} // namespace corvus::gateway