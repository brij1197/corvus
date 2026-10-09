#pragma once
#include <chrono>
#include <string>

namespace corvus::gateway
{
    struct HealthProbeResult
    {
        bool healthy{false};
        std::string detail;
    };

    HealthProbeResult probe_http_health(
        const std::string &host,
        int port,
        const std::string &path = "/health",
        std::chrono::milliseconds timeout = std::chrono::milliseconds{2000});

} // namespace corvus::gateway