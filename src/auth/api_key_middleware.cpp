#include "corvus/auth/api_key_middleware.h"
#include "corvus/api/response.h"
#include "corvus/api/request_id.h"
#include <drogon/drogon.h>

namespace corvus::auth
{
    void register_api_key_middleware(std::shared_ptr<ApiKeyValidator> validator)
    {
        drogon::app().registerPreHandlingAdvice(
            [validator](const drogon::HttpRequestPtr &req,
                        drogon::AdviceCallback &&cb,
                        drogon::AdviceChainCallback &&next)
            {
                // Only enforces on /v1/*
                if (req->getPath().rfind("/v1/", 0) != 0)
                {
                    next();
                    return;
                }

                const std::string raw_key = req->getHeader("X-Api-Key");
                if (raw_key.empty())
                {
                    const auto request_id = corvus::api::get_request_id(req);
                    cb(corvus::api::respond_error(
                        corvus::api::ErrorCode::unauthorized,
                        "Missing X-Api-Key header",
                        request_id));
                    return;
                }

                try
                {
                    validator->validate(raw_key);
                    next();
                }
                catch (const ApiKeyValidationError &e)
                {
                    const auto request_id = corvus::api::get_request_id(req);
                    cb(corvus::api::respond_error(
                        corvus::api::ErrorCode::unauthorized,
                        std::string("Invalid API key: ") + e.what(),
                        request_id));
                }
            });
    }
} // namespace corvus::auth