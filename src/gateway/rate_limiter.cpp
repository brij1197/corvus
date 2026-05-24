#include "corvus/gateway/rate_limiter.h"
#include "corvus/api/response.h"
#include "corvus/api/request_id.h"
#include <drogon/drogon.h>
#include <algorithm>
#include <cmath>
#include <sstream>

namespace corvus::gateway
{

    RateLimiter::RateLimiter(RateLimitConfig config)
        : config_(config),
          last_cleanup_(std::chrono::steady_clock::now())
    {
    }

    bool RateLimiter::allow(const std::string &client_key)
    {
        const auto now = std::chrono::steady_clock::now();

        std::lock_guard<std::mutex> lock(mutex_);

        maybe_cleanup(now);

        auto [it, inserted] = buckets_.try_emplace(client_key, TokenBucket{
                                                                   static_cast<double>(config_.max_tokens),
                                                                   now,
                                                                   now});
        auto &bucket = it->second;

        if (!inserted)
        {
            refill(bucket, now);
        }

        bucket.last_access = now;

        if (bucket.tokens >= 1.0)
        {
            bucket.tokens -= 1.0;
            return true;
        }

        return false;
    }

    std::size_t RateLimiter::remaining(const std::string &client_key) const
    {
        std::lock_guard<std::mutex> lock(mutex_);

        auto it = buckets_.find(client_key);
        if (it == buckets_.end())
        {
            return config_.max_tokens;
        }

        auto bucket_copy = it->second;
        refill(bucket_copy, std::chrono::steady_clock::now());

        return static_cast<std::size_t>(std::max(0.0, std::floor(bucket_copy.tokens)));
    }

    double RateLimiter::retry_after(const std::string &client_key) const
    {
        std::lock_guard<std::mutex> lock(mutex_);

        auto it = buckets_.find(client_key);
        if (it == buckets_.end())
        {
            return 0.0;
        }

        auto bucket_copy = it->second;
        refill(bucket_copy, std::chrono::steady_clock::now());

        if (bucket_copy.tokens >= 1.0)
        {
            return 0.0;
        }

        const double deficit = 1.0 - bucket_copy.tokens;
        return std::ceil(deficit / config_.refill_rate);
    }

    std::size_t RateLimiter::client_count() const
    {
        std::lock_guard<std::mutex> lock(mutex_);
        return buckets_.size();
    }

    void RateLimiter::refill(TokenBucket &bucket,
                             std::chrono::steady_clock::time_point now) const
    {
        const auto elapsed =
            std::chrono::duration<double>(now - bucket.last_refill).count();

        if (elapsed <= 0.0)
        {
            return;
        }

        bucket.tokens = std::min(
            static_cast<double>(config_.max_tokens),
            bucket.tokens + elapsed * config_.refill_rate);
        bucket.last_refill = now;
    }

    void RateLimiter::maybe_cleanup(std::chrono::steady_clock::time_point now)
    {
        if (now - last_cleanup_ < config_.cleanup_interval)
        {
            return;
        }

        last_cleanup_ = now;

        for (auto it = buckets_.begin(); it != buckets_.end();)
        {
            if (now - it->second.last_access > config_.entry_ttl)
            {
                it = buckets_.erase(it);
            }
            else
            {
                ++it;
            }
        }
    }

    std::string client_key_from_request(const drogon::HttpRequestPtr &req)
    {
        const auto xff = req->getHeader("X-Forwarded-For");
        if (!xff.empty())
        {
            const auto comma = xff.find(',');
            auto ip = (comma != std::string::npos) ? xff.substr(0, comma) : std::string(xff);

            const auto start = ip.find_first_not_of(" \t");
            const auto end = ip.find_last_not_of(" \t");
            if (start != std::string::npos)
            {
                return ip.substr(start, end - start + 1);
            }
        }

        return req->getPeerAddr().toIp();
    }

    void register_rate_limit_middleware(std::shared_ptr<RateLimiter> limiter)
    {
        drogon::app().registerPreRoutingAdvice(
            [limiter](const drogon::HttpRequestPtr &req,
                      drogon::AdviceCallback &&cb,
                      drogon::AdviceChainCallback &&next)
            {
                const std::string &path = req->getPath();
                if (path == "/health" || path == "/ready")
                {
                    next();
                    return;
                }

                const auto key = client_key_from_request(req);

                if (limiter->allow(key))
                {
                    next();
                    return;
                }

                const auto request_id = corvus::api::get_request_id(req);
                const auto retry_secs = limiter->retry_after(key);

                auto resp = corvus::api::respond_error(
                    corvus::api::ErrorCode::too_many_requests,
                    "Rate limit exceeded. Try again later.",
                    request_id);

                const auto &cfg = limiter->config();
                resp->addHeader("X-RateLimit-Limit",
                                std::to_string(cfg.max_tokens));
                resp->addHeader("X-RateLimit-Remaining", "0");
                resp->addHeader("Retry-After",
                                std::to_string(static_cast<int>(std::ceil(retry_secs))));

                cb(resp);
            });
    }

} // namespace corvus::gateway