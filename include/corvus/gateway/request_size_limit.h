#pragma once
#include <cstdint>
#include <drogon/drogon.h>
#include <memory>

namespace corvus::gateway
{
    struct RequestSizeLimitConfig
    {
        /// Default: 1MB(1048576 bytes)
        std::size_t max_body_bytes{1024 * 1024};
    };

    /// Register a pre-routing advice that rejects requests whose
    /// Content-Length exceeds max_body_bytes with a 413 response.
    void register_request_size_limit_middleware(RequestSizeLimitConfig config = {});

} // namespace corvus::gateway