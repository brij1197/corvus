#include "corvus/gateway/router.h"
#include "corvus/gateway/health_handler.h"
#include "corvus/gateway/not_found_handler.h"
#include "corvus/api/response.h"
#include "corvus/api/request_id.h"
#include <drogon/drogon.h>

namespace corvus::gateway
{

    void register_routes()
    {
        auto handler = health_handler();

        auto h = health_handler();
        drogon::app().registerHandler(
            "/health",
            [h](const drogon::HttpRequestPtr &req,
                std::function<void(const drogon::HttpResponsePtr &)> &&cb)
            { h(req, std::move(cb)); },
            {drogon::Get});

        drogon::app().registerHandler(
            "/ready",
            [h](const drogon::HttpRequestPtr &req,
                std::function<void(const drogon::HttpResponsePtr &)> &&cb)
            { h(req, std::move(cb)); },
            {drogon::Get});

        auto nf = not_found_handler();
        drogon::app().setCustomErrorHandler([nf](drogon::HttpStatusCode code, const drogon::HttpRequestPtr &req) -> drogon::HttpResponsePtr
                                            {
        if(code ==drogon::k404NotFound){
            const auto request_id = corvus::api::get_request_id(req);
            return corvus::api::respond_error(
                corvus::api::ErrorCode::not_found,
                "The request path '" + req->getPath() + "' does not exist",
                request_id);
            }

        return drogon::HttpResponse::newHttpResponse(); });
        LOG_INFO << "Routes registered";
    }

} // namespace corvus::gateway