#include "corvus/auth/middleware.h"
#include "corvus/api/response.h"
#include "corvus/api/request_id.h"
#include <drogon/drogon.h>

namespace corvus::auth
{
    void register_jwt_middleware(std::shared_ptr<JwtValidator> validator)
    {
        drogon::app().registerPreHandlingAdvice(
            [validator](const drogon::HttpRequestPtr &req, drogon::AdviceCallback &&cb, drogon::AdviceChainCallback &&next)
            {
                // Only apply to /v1/* routes
                const std::string &path = req->getPath();
                if (path.rfind("/v1/", 0) != 0)
                {
                    next();
                    return;
                }

                const auto request_id = corvus::api::get_request_id(req);

                // Extract Bearer token
                const std::string auth_header = req->getHeader("Authorization");
                const std::string prefix = "Bearer ";
                if (auth_header.empty() || auth_header.rfind(prefix, 0) != 0)
                {
                    auto resp = corvus::api::respond_error(
                        corvus::api::ErrorCode::unauthorized,
                        "Missing or malformed Authorization header",
                        request_id);
                    resp->addHeader("X-Request-ID", request_id);
                    cb(resp);
                    return;
                }

                const std::string token = auth_header.substr(prefix.size());
                try
                {
                    validator->validate(token);
                    next();
                }
                catch (const JwtValidationError &e)
                {
                    LOG_DEBUG << "JWT rejected (request_id=" << request_id
                              << "): " << e.what();
                    auto resp = corvus::api::respond_error(
                        corvus::api::ErrorCode::unauthorized,
                        "Invalid or expired token",
                        request_id);
                    resp->addHeader("X-Request-ID", request_id);
                    cb(resp);
                }
            });
    }
} // namespace corvus::auth