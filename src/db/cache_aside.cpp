#include "corvus/db/cache_aside.h"
#include <drogon/drogon.h>
#include <hiredis/hiredis.h>
#include <cstring>

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

    std::string CacheAside::version_key(const std::string &key) const
    {
        return config_.key_prefix + "ver:" + key;
    }

    namespace
    {
        constexpr const char *kPutIfVersion = R"lua(
local current = redis.call('GET', KEYS[2]) or '0'
if current ~= ARGV[1] then return 0 end
if tonumber(ARGV[3]) > 0 then
  redis.call('SET', KEYS[1], ARGV[2], 'EX', ARGV[3])
else
  redis.call('SET', KEYS[1], ARGV[2])
end
return 1
)lua";

        constexpr const char *kInvalidateVersioned = R"lua(
redis.call('INCR', KEYS[2])
redis.call('EXPIRE', KEYS[2], ARGV[1])
redis.call('DEL', KEYS[1])
return 1
)lua";
    } // namespace

    bool CacheAside::ensure_connected_locked()
    {
        if (redis_.is_connected())
            return true;

        const auto now = std::chrono::steady_clock::now();
        if (now < next_reconnect_)
            return false;

        try
        {
            redis_.reconnect();
            LOG_INFO << "cache: reconnected to Redis";
            return true;
        }
        catch (const std::exception &e)
        {
            next_reconnect_ = now + kReconnectBackoff;
            LOG_WARN << "cache: Redis reconnect failed, retrying in "
                     << kReconnectBackoff.count() << "s: " << e.what();
            return false;
        }
    }

    std::optional<std::string> CacheAside::get(const std::string &key)
    {
        const auto full_key = make_key(key);
        std::lock_guard<std::mutex> lock(mutex_);
        if (!ensure_connected_locked())
            return std::nullopt;
        try
        {
            const auto value = redis_.get(full_key);
            if (value.empty() && redis_.exists(full_key) == 0)
                return std::nullopt;
            return value;
        }
        catch (const std::exception &e)
        {
            LOG_WARN << "cache get failed for " << full_key
                     << ", treating as miss: " << e.what();
            return std::nullopt;
        }
    }

    bool CacheAside::put(const std::string &key,
                         const std::string &value,
                         int ttl_seconds)
    {
        const auto full_key = make_key(key);
        std::lock_guard<std::mutex> lock(mutex_);
        if (!ensure_connected_locked())
            return false;
        try
        {
            redis_.set(full_key, value, effective_ttl(ttl_seconds));
            return true;
        }
        catch (const std::exception &e)
        {
            LOG_WARN << "cache put failed for " << full_key
                     << ", entry not cached: " << e.what();
            return false;
        }
    }

    std::optional<std::string> CacheAside::get_or_fetch(
        const std::string &key,
        FetchFn fetch,
        int ttl_seconds)
    {
        if (auto cached = get(key))
            return cached;

        auto value = fetch();
        if (!value.has_value())
            return std::nullopt;

        put(key, *value, ttl_seconds);
        return value;
    }

    bool CacheAside::invalidate(const std::string &key)
    {
        const auto full_key = make_key(key);
        std::lock_guard<std::mutex> lock(mutex_);
        if (!ensure_connected_locked())
        {
            LOG_ERROR << "cache invalidate skipped for " << full_key
                      << ", Redis unreachable; entry is stale until its TTL expires";
            return false;
        }
        try
        {
            redis_.del(full_key);
            return true;
        }
        catch (const std::exception &e)
        {
            LOG_ERROR << "cache invalidate failed for " << full_key
                      << "; entry is stale until its TTL expires: " << e.what();
            return false;
        }
    }

    void CacheAside::invalidate_prefix(const std::string &prefix)
    {
        const auto pattern = make_key(prefix) + "*";
        std::lock_guard<std::mutex> lock(mutex_);
        if (!ensure_connected_locked())
        {
            LOG_ERROR << "cache invalidate_prefix skipped for " << pattern
                      << ", Redis unreachable; matching entries are stale until TTL";
            return;
        }

        long long cursor = 0;
        try
        {
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
        catch (const std::exception &e)
        {
            LOG_ERROR << "cache invalidate_prefix failed for " << pattern
                      << "; matching entries are stale until TTL: " << e.what();
        }
    }

    std::optional<std::string> CacheAside::version(const std::string &key)
    {
        const auto vkey = version_key(key);
        std::lock_guard<std::mutex> lock(mutex_);
        if (!ensure_connected_locked())
            return std::nullopt;
        try
        {
            const auto v = redis_.get(vkey);
            return v.empty() ? std::string{"0"} : v;
        }
        catch (const std::exception &e)
        {
            LOG_WARN << "cache version read failed for " << vkey
                     << ", not populating: " << e.what();
            return std::nullopt;
        }
    }

    bool CacheAside::put_if_version(const std::string &key,
                                    const std::string &expected_version,
                                    const std::string &value,
                                    int ttl_seconds)
    {
        const auto full_key = make_key(key);
        const auto vkey = version_key(key);
        const auto ttl = std::to_string(effective_ttl(ttl_seconds));

        std::lock_guard<std::mutex> lock(mutex_);
        if (!ensure_connected_locked())
            return false;

        const char *argv[] = {"EVAL", kPutIfVersion, "2",
                              full_key.c_str(), vkey.c_str(),
                              expected_version.c_str(), value.data(), ttl.c_str()};
        const std::size_t argvlen[] = {4, std::strlen(kPutIfVersion), 1,
                                       full_key.size(), vkey.size(),
                                       expected_version.size(), value.size(), ttl.size()};

        RedisReply reply(static_cast<redisReply *>(
            redisCommandArgv(redis_.ctx(), 8, argv, argvlen)));
        if (!reply || reply.is_error())
        {
            LOG_WARN << "cache versioned put failed for " << full_key << ": "
                     << (reply ? reply->str : redis_.ctx()->errstr);
            return false;
        }
        if (reply->type == REDIS_REPLY_INTEGER && reply->integer == 0)
        {
            LOG_DEBUG << "skipping cache put for " << full_key
                      << ": superseded by a concurrent write";
            return false;
        }
        return true;
    }

    bool CacheAside::invalidate_versioned(const std::string &key)
    {
        const auto full_key = make_key(key);
        const auto vkey = version_key(key);
        const auto ttl = std::to_string(config_.version_ttl_seconds);

        std::lock_guard<std::mutex> lock(mutex_);
        if (!ensure_connected_locked())
        {
            LOG_ERROR << "cache invalidate skipped for " << full_key
                      << ", Redis unreachable; entry is stale until its TTL expires";
            return false;
        }

        const char *argv[] = {"EVAL", kInvalidateVersioned, "2",
                              full_key.c_str(), vkey.c_str(), ttl.c_str()};
        const std::size_t argvlen[] = {4, std::strlen(kInvalidateVersioned), 1,
                                       full_key.size(), vkey.size(), ttl.size()};

        RedisReply reply(static_cast<redisReply *>(
            redisCommandArgv(redis_.ctx(), 6, argv, argvlen)));
        if (!reply || reply.is_error())
        {
            LOG_ERROR << "cache invalidate failed for " << full_key
                      << "; entry is stale until its TTL expires: "
                      << (reply ? reply->str : redis_.ctx()->errstr);
            return false;
        }
        return true;
    }

    bool CacheAside::exists(const std::string &key)
    {
        const auto full_key = make_key(key);
        std::lock_guard<std::mutex> lock(mutex_);
        if (!ensure_connected_locked())
            return false;
        try
        {
            return redis_.exists(full_key) == 1;
        }
        catch (const std::exception &e)
        {
            LOG_WARN << "cache exists failed for " << full_key
                     << ", reporting absent: " << e.what();
            return false;
        }
    }

} // namespace corvus::db