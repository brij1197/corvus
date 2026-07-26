#include "corvus/auth/api_key_middleware.h"
#include "corvus/auth/client_context.h"
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

                const auto request_id = corvus::api::get_request_id(req);
                const std::string raw_key = req->getHeader("X-Api-Key");
                if (raw_key.empty())
                {
                    auto resp = corvus::api::respond_error(
                        corvus::api::ErrorCode::unauthorized,
                        "Missing X-Api-Key header",
                        request_id);
                    resp->addHeader("X-Request-ID", request_id);
                    cb(resp);
                    return;
                }

                try
                {
                    const ApiKeyInfo info = validator->validate(raw_key);
                    req->getAttributes()->insert(kClientIdKey, info.client_id);

                    next();
                }
                catch (const ApiKeyValidationError &e)
                {
                    LOG_DEBUG << "API key rejected (request_id=" << request_id
                              << "): " << e.what();

                    auto resp = corvus::api::respond_error(
                        corvus::api::ErrorCode::unauthorized,
                        "Invalid or expired API key",
                        request_id);
                    resp->addHeader("X-Request-ID", request_id);
                    cb(resp);
                }
            });
    }
} // namespace corvus::auth