#pragma once
#include "corvus/auth/jwt_validator.h"
#include <drogon/drogon.h>
#include <memory>

namespace corvus::auth
{
    class ApiKeyValidator;

    /// Drogon pre-handler that enforces JWT authentication on /v1/* routes
    void register_auth_middleware(std::shared_ptr<JwtValidator> jwt_validator,
                                  std::shared_ptr<ApiKeyValidator> api_key_validator);
} // namespace corvus::auth