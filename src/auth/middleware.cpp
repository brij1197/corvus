#include "corvus/auth/middleware.h"
#include "corvus/auth/authenticator.h"
#include "corvus/auth/api_key_validator.h"
#include "corvus/auth/client_context.h"
#include "corvus/api/response.h"
#include "corvus/api/request_id.h"
#include "corvus/gateway/offload.h"
#include <drogon/drogon.h>

namespace corvus::auth
{
    namespace
    {
        corvus::api::ErrorCode to_error_code(AuthFailure kind)
        {
            switch (kind)
            {
            case AuthFailure::forbidden:
                return corvus::api::ErrorCode::forbidden;
            case AuthFailure::unavailable:
                return corvus::api::ErrorCode::service_unavailable;
            case AuthFailure::unauthorized:
            default:
                return corvus::api::ErrorCode::unauthorized;
            }
        }
    } // namespace

    void register_auth_middleware(std::shared_ptr<JwtValidator> jwt_validator,
                                  std::shared_ptr<ApiKeyValidator> api_key_validator,
                                  std::shared_ptr<gateway::BlockingExecutor> executor)
    {
        drogon::app().registerPreHandlingAdvice(
            [jwt_validator, api_key_validator, executor](const drogon::HttpRequestPtr &req,
                                                         drogon::AdviceCallback &&cb,
                                                         drogon::AdviceChainCallback &&next)
            {
                if (req->getPath().rfind("/v1/", 0) != 0)
                {
                    next();
                    return;
                }

                auto reject = cb;

                auto check = [jwt_validator, api_key_validator, req,
                              cb = std::move(cb), next = std::move(next)]()
                {
                    const auto request_id = corvus::api::get_request_id(req);

                    const Credentials creds{
                        std::string(req->getMethodString()),
                        req->getHeader("Authorization"),
                        req->getHeader("X-Api-Key"),
                    };

                    const auto result = authenticate(
                        creds,
                        [&](const std::string &token)
                        { return jwt_validator->validate(token); },
                        [&](const std::string &key)
                        { return api_key_validator->validate(key); });

                    if (const auto *error = std::get_if<AuthError>(&result))
                    {
                        if (error->kind == AuthFailure::unavailable)
                            LOG_ERROR << "auth backend unavailable (request_id=" << request_id << ")";
                        else
                            LOG_DEBUG << "auth rejected (request_id=" << request_id
                                      << "): " << error->message;

                        auto resp = corvus::api::respond_error(
                            to_error_code(error->kind), error->message, request_id);
                        resp->addHeader("X-Request-ID", request_id);
                        if (error->kind == AuthFailure::unavailable)
                            resp->addHeader("Retry-After", "5");
                        cb(resp);
                        return;
                    }

                    const auto &principal = std::get<Principal>(result);
                    req->getAttributes()->insert(kClientIdKey, principal.client_id);
                    next();
                };

                if (!executor->try_submit(std::move(check)))
                    reject(gateway::server_busy_response(req));
            });
    }
} // namespace corvus::auth