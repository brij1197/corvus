#pragma once
#include <chrono>
#include <cstdint>
#include <drogon/drogon.h>
#include <memory>
#include <mutex>
#include <string>
#include <unordered_map>

namespace corvus::gateway
{

    struct RateLimitConfig
    {

        std::size_t max_tokens{100};
        double refill_rate{10.0};
        std::chrono::seconds cleanup_interval{300};
        std::chrono::seconds entry_ttl{600};
    };

    /// Per-client token bucket state
    struct TokenBucket
    {
        double tokens;
        std::chrono::steady_clock::time_point last_refill;
        std::chrono::steady_clock::time_point last_access;
    };

    /// Thread-safe in-memory token-bucket rate limiter.
    /// Keys clients by IP address. Each client gets an independent bucket
    /// that refills at a constant rate up to a maximum burst size.

    class RateLimiter
    {
    public:
        explicit RateLimiter(RateLimitConfig config = {});

        bool allow(const std::string &client_key);

        std::size_t remaining(const std::string &client_key) const;

        double retry_after(const std::string &client_key) const;

        const RateLimitConfig &config() const noexcept { return config_; }

        std::size_t client_count() const;

    private:
        void refill(TokenBucket &bucket,
                    std::chrono::steady_clock::time_point now) const;

        void maybe_cleanup(std::chrono::steady_clock::time_point now);

        RateLimitConfig config_;

        mutable std::mutex mutex_;
        std::unordered_map<std::string, TokenBucket> buckets_;
        std::chrono::steady_clock::time_point last_cleanup_;
    };

    std::string client_key_from_request(const drogon::HttpRequestPtr &req);

    void register_rate_limit_middleware(std::shared_ptr<RateLimiter> limiter);

} // namespace corvus::gateway