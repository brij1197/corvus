#include "corvus/gateway/request_id_middleware.h"
#include "corvus/api/request_id.h"
#include <drogon/drogon.h>
#include <string>

namespace corvus::gateway
{
    void register_request_id_middleware()
    {
        // Pre-routing: inject the request ID into attributes before
        // any other middleware or handler runs.
        drogon::app().registerPreRoutingAdvice(
            [](const drogon::HttpRequestPtr &req,
               drogon::AdviceCallback &&,
               drogon::AdviceChainCallback &&next)
            {
                std::string request_id;
                const auto header = req->getHeader("X-Request-ID");
                if (!header.empty())
                {
                    request_id = std::string(header);
                }
                else
                {
                    request_id = corvus::api::generate_uuid();
                }
                req->getAttributes()->insert(corvus::api::kRequestIdKey, request_id);

                next();
            });

        drogon::app().registerPostHandlingAdvice(
            [](const drogon::HttpRequestPtr &req,
               const drogon::HttpResponsePtr &resp)
            {
                const auto request_id = corvus::api::get_request_id(req);
                resp->addHeader("X-Request-ID", request_id);
            });
    }
} // namespace corvus::gateway