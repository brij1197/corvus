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

                // Extract Bearer token
                const std::string auth_header = req->getHeader("Authorization");
                const std::string prefix = "Bearer ";
                if (auth_header.empty() || auth_header.rfind(prefix, 0) != 0)
                {
                    const auto request_id = corvus::api::get_request_id(req);
                    cb(corvus::api::respond_error(
                        corvus::api::ErrorCode::unauthorized,
                        "Missing or malformed Authorization header",
                        request_id));
                    return;
                }

                const std::string token = auth_header.substr(prefix.size());
                try
                {
                    validator->validate(token);
                    next(); // Token valid, proceed to the next handler
                }
                catch (const JwtValidationError &e)
                {
                    const auto request_id = corvus::api::get_request_id(req);
                    cb(corvus::api::respond_error(
                        corvus::api::ErrorCode::unauthorized,
                        std::string("Invalid token: ") + e.what(),
                        request_id));
                }
            });
    }
} // namespace corvus::auth