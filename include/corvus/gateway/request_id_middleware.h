#pragma once
#include <drogon/drogon.h>

namespace corvus::gateway
{
    /// Register a pre-routing advice that ensures every request has a
    /// stable request ID for its entire lifetime.
    void register_request_id_middleware();

} // namespace corvus::gateway