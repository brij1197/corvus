#include "corvus/gateway/request_size_limit.h"
#include "corvus/api/response.h"
#include "corvus/api/request_id.h"
#include <drogon/drogon.h>
#include <string>

namespace corvus::gateway
{

    static bool is_body_method(drogon::HttpMethod method)
    {
        return method != drogon::Get &&
               method != drogon::Head &&
               method != drogon::Delete &&
               method != drogon::Options;
    }

    static drogon::HttpResponsePtr make_413(
        const drogon::HttpRequestPtr &req,
        std::size_t max_bytes)
    {
        const auto request_id = corvus::api::get_request_id(req);
        auto resp = corvus::api::respond_error(
            corvus::api::ErrorCode::payload_too_large,
            "Request body exceeds the maximum allowed size of " +
                std::to_string(max_bytes) + " bytes.",
            request_id);
        resp->addHeader("X-Max-Body-Size", std::to_string(max_bytes));
        return resp;
    }

    void register_request_size_limit_middleware(RequestSizeLimitConfig config)
    {
        drogon::app().registerPreRoutingAdvice(
            [config](const drogon::HttpRequestPtr &req,
                     drogon::AdviceCallback &&cb,
                     drogon::AdviceChainCallback &&next)
            {
                if (!is_body_method(req->getMethod()))
                {
                    next();
                    return;
                }

                const auto cl_str = req->getHeader("content-length");
                if (!cl_str.empty())
                {
                    try
                    {
                        const auto cl = static_cast<std::size_t>(
                            std::stoull(std::string(cl_str)));
                        if (cl > config.max_body_bytes)
                        {
                            cb(make_413(req, config.max_body_bytes));
                            return;
                        }
                    }
                    catch (...)
                    {
                        // Malformed Content-Length - let preHandlingAdvice catch it
                    }
                }

                next();
            });

        drogon::app().registerPreHandlingAdvice(
            [config](const drogon::HttpRequestPtr &req,
                     drogon::AdviceCallback &&cb,
                     drogon::AdviceChainCallback &&next)
            {
                if (!is_body_method(req->getMethod()))
                {
                    next();
                    return;
                }

                if (req->getBody().size() > config.max_body_bytes)
                {
                    cb(make_413(req, config.max_body_bytes));
                    return;
                }

                next();
            });
    }

} // namespace corvus::gateway