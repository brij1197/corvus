#pragma once
#include "corvus/db/redis_connection.h"
#include <chrono>
#include <functional>
#include <optional>
#include <string>

namespace corvus::db
{
    struct CacheConfig
    {
        int default_ttl_seconds{300};
        std::string key_prefix{"corvus:"};
    };

    class CacheAside
    {
    public:
        using FetchFn = std::function<std::optional<std::string>()>;

        explicit CacheAside(RedisConnection &redis,
                            CacheConfig config = {});

        std::optional<std::string> get_or_fetch(const std::string &key,
                                                FetchFn fetch,
                                                int ttl_seconds = -1);

        std::optional<std::string> get(const std::string &key);

        void put(const std::string &key, const std::string &value, int ttl_seconds = -1);

        void invalidate(const std::string &key);

        void invalidate_prefix(const std::string &prefix);

        bool exists(const std::string &key);

        const CacheConfig &config() const noexcept { return config_; }

        std::string make_key(const std::string &key) const;

    private:
        int effective_ttl(int ttl_seconds) const noexcept
        {
            return (ttl_seconds < 0) ? config_.default_ttl_seconds : ttl_seconds;
        }

        RedisConnection &redis_;
        CacheConfig config_;
    };

} // namespace corvus::db