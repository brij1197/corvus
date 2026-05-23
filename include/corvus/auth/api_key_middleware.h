#pragma once
#include "corvus/auth/api_key_validator.h"
#include <memory>

namespace corvus::auth
{
    /// Runs after JWT middleware
    /// Registers a Drogon pre-handling advice that enforces X-Api-Key on /v1/* routes
    void register_api_key_middleware(std::shared_ptr<ApiKeyValidator> validator);
} // namespace corvus::auth