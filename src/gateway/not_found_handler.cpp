#include "corvus/gateway/not_found_handler.h"
#include "corvus/api/response.h"
#include "corvus/api/request_id.h"

namespace corvus::gateway
{

    HandlerFunc not_found_handler()
    {
        return [](const drogon::HttpRequestPtr &req,
                  std::function<void(const drogon::HttpResponsePtr &)> &&callback)
        {
            const auto request_id = corvus::api::get_request_id(req);

            callback(corvus::api::respond_error(
                corvus::api::ErrorCode::not_found,
                "The request path '" + req->getPath() + "' does not exist",
                request_id));
        };
    }

} // namespace corvus::gateway