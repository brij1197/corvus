#pragma once
#include "corvus/auth/jwt_validator.h"
#include <drogon/drogon.h>
#include <memory>

namespace corvus::auth
{
    /// Drogon pre-handler that enforces JWT authentication on /v1/* routes
    void register_jwt_middleware(std::shared_ptr<JwtValidator> validator);
} // namespace corvus::auth