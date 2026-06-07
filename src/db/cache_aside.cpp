#include "corvus/db/cache_aside.h"
#include <hiredis/hiredis.h>

namespace corvus::db
{

    CacheAside::CacheAside(RedisConnection &redis, CacheConfig config)
        : redis_(redis), config_(std::move(config))
    {
    }

    std::string CacheAside::make_key(const std::string &key) const
    {
        return config_.key_prefix + key;
    }

    std::optional<std::string> CacheAside::get(const std::string &key)
    {
        const auto full_key = make_key(key);
        const auto value = redis_.get(full_key);

        if (value.empty() && redis_.exists(full_key) == 0)
            return std::nullopt;
        return value;
    }

    void CacheAside::put(const std::string &key,
                         const std::string &value,
                         int ttl_seconds)
    {
        redis_.set(make_key(key), value, effective_ttl(ttl_seconds));
    }

    std::optional<std::string> CacheAside::get_or_fetch(
        const std::string &key,
        FetchFn fetch,
        int ttl_seconds)
    {
        const auto full_key = make_key(key);
        const auto cached = redis_.get(full_key);
        if (!cached.empty() || redis_.exists(full_key) == 1)
            return cached;

        auto value = fetch();
        if (!value.has_value())
            return std::nullopt;

        redis_.set(full_key, *value, effective_ttl(ttl_seconds));
        return value;
    }

    void CacheAside::invalidate(const std::string &key)
    {
        redis_.del(make_key(key));
    }

    void CacheAside::invalidate_prefix(const std::string &prefix)
    {
        const auto pattern = make_key(prefix) + "*";

        long long cursor = 0;
        do
        {
            auto *r = static_cast<redisReply *>(
                redisCommand(redis_.ctx(),
                             "SCAN %lld MATCH %s COUNT 100",
                             cursor, pattern.c_str()));
            RedisReply reply(r);

            if (!reply || !reply.is_array() || reply->elements < 2)
                break;

            cursor = std::stoll(reply->element[0]->str);

            auto *keys = reply->element[1];
            for (std::size_t i = 0; i < keys->elements; ++i)
            {
                if (keys->element[i]->str)
                    redis_.del(keys->element[i]->str);
            }
        } while (cursor != 0);
    }

    bool CacheAside::exists(const std::string &key)
    {
        return redis_.exists(make_key(key)) == 1;
    }

} // namespace corvus::db