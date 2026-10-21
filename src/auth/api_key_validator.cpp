#include "corvus/auth/api_key_validator.h"
#include <algorithm>
#include <openssl/evp.h>
#include <unordered_map>
#include <sstream>
#include <iomanip>
#include <thread>
#include <chrono>

namespace corvus::auth
{

    std::string ApiKeyValidator::hash_key(const std::string &raw_key)
    {
        unsigned char hash[EVP_MAX_MD_SIZE];
        unsigned int hash_len = 0;

        EVP_MD_CTX *ctx = EVP_MD_CTX_new();
        EVP_DigestInit_ex(ctx, EVP_sha256(), nullptr);
        EVP_DigestUpdate(ctx, raw_key.data(), raw_key.size());
        EVP_DigestFinal_ex(ctx, hash, &hash_len);
        EVP_MD_CTX_free(ctx);

        std::ostringstream hex;
        for (unsigned int i = 0; i < hash_len; ++i)
        {
            hex << std::hex << std::setw(2) << std::setfill('0')
                << static_cast<int>(hash[i]);
        }
        return hex.str();
    }

    static std::string redis_key(const std::string &hash)
    {
        return "corvus:apikeys:" + hash;
    }

    namespace
    {
        constexpr const char *kSourceField = "src";
        constexpr const char *kSourcePostgres = "pg";

        std::chrono::seconds cache_ttl_for(const ApiKeyRecord &record)
        {
            auto ttl = ApiKeyValidator::kCacheTtl;
            if (record.expires_at)
            {
                const auto left = std::chrono::duration_cast<std::chrono::seconds>(
                    *record.expires_at - std::chrono::system_clock::now());
                ttl = std::min(ttl, left);
            }
            return std::max(ttl, std::chrono::seconds{1});
        }
    } // namespace

    ApiKeyValidator::ApiKeyValidator() : config_(db::redis_config_from_env())
    {
        constexpr int kMaxAttempts = 5;
        std::string last_error;
        for (int attempt = 1; attempt <= kMaxAttempts; ++attempt)
        {
            try
            {
                release(acquire());
                return;
            }
            catch (const db::RedisConfigError &e)
            {
                last_error = e.what();
                if (attempt < kMaxAttempts)
                    std::this_thread::sleep_for(std::chrono::seconds(1));
            }
        }
        throw ApiKeyConfigError(
            "Redis unreachable after " + std::to_string(kMaxAttempts) +
            " attempts: " + last_error);
    }

    ApiKeyValidator::ApiKeyValidator(db::RedisConfig config, ApiKeyLookup lookup)
        : config_(std::move(config)), lookup_(std::move(lookup))
    {
        try
        {
            release(acquire());
        }
        catch (const db::RedisConfigError &e)
        {
            throw ApiKeyConfigError(e.what());
        }
    }

    ApiKeyValidator::ApiKeyValidator(const std::string &host, int port)
        : ApiKeyValidator([&]
                          {
              db::RedisConfig cfg;
              cfg.host = host;
              cfg.port = port;
              return cfg; }())
    {
    }

    ApiKeyValidator::~ApiKeyValidator() = default;

    std::unique_ptr<db::RedisConnection> ApiKeyValidator::acquire() const
    {
        {
            std::lock_guard<std::mutex> lock(idle_mutex_);
            while (!idle_.empty())
            {
                auto conn = std::move(idle_.back());
                idle_.pop_back();
                if (conn->is_connected())
                    return conn;
            }
        }
        return std::make_unique<db::RedisConnection>(config_);
    }

    void ApiKeyValidator::release(std::unique_ptr<db::RedisConnection> conn) const
    {
        if (!conn || !conn->is_connected())
            return;
        std::lock_guard<std::mutex> lock(idle_mutex_);
        if (idle_.size() < kMaxIdleConnections)
            idle_.push_back(std::move(conn));
    }

    std::size_t ApiKeyValidator::idle_connections() const
    {
        std::lock_guard<std::mutex> lock(idle_mutex_);
        return idle_.size();
    }

    std::optional<ApiKeyInfo> ApiKeyValidator::from_cache(const std::string &rkey) const
    {
        std::unordered_map<std::string, std::string> fields;
        auto conn = acquire(); // RedisConfigError propagates to the caller
        try
        {
            fields = conn->hgetall(rkey);
        }
        catch (const db::RedisCommandError &)
        {
            if (conn->is_connected())
                throw;
            conn = std::make_unique<db::RedisConnection>(config_);
            fields = conn->hgetall(rkey);
        }
        release(std::move(conn));

        if (fields.empty())
            return std::nullopt;

        if (lookup_)
        {
            const auto src = fields.find(kSourceField);
            if (src == fields.end() || src->second != kSourcePostgres)
                return std::nullopt;
        }

        ApiKeyInfo info;
        if (const auto it = fields.find("client_id"); it != fields.end())
            info.client_id = it->second;
        if (const auto it = fields.find("scopes"); it != fields.end())
            info.scopes = it->second;
        return info;
    }

    void ApiKeyValidator::store_in_cache(const std::string &rkey,
                                         const ApiKeyRecord &record) const
    {
        try
        {
            auto conn = acquire();
            conn->hmset(rkey, {{"client_id", record.client_id},
                               {"scopes", record.scopes},
                               {kSourceField, kSourcePostgres}});
            conn->expire(rkey, static_cast<int>(cache_ttl_for(record).count()));
            release(std::move(conn));
        }
        catch (const std::exception &)
        {
        }
    }

    ApiKeyInfo ApiKeyValidator::validate(const std::string &raw_key) const
    {
        if (raw_key.empty())
            throw ApiKeyValidationError("API key must not be empty");

        const std::string hashed = hash_key(raw_key);
        const std::string rkey = redis_key(hashed);

        std::optional<ApiKeyInfo> cached;
        try
        {
            cached = from_cache(rkey);
        }
        catch (const std::exception &e)
        {
            if (!lookup_)
                throw ApiKeyBackendError(
                    std::string("Redis unavailable, cannot validate API key: ") + e.what());
        }

        ApiKeyInfo info;
        if (cached)
        {
            info = *cached;
        }
        else
        {
            if (!lookup_)
                throw ApiKeyValidationError("Unknown or revoked API key");

            const auto record = lookup_(hashed);
            if (!record)
                throw ApiKeyValidationError("Unknown, revoked or expired API key");

            store_in_cache(rkey, *record);
            info = {record->client_id, record->scopes};
        }

        if (info.client_id.empty())
            throw ApiKeyValidationError("API key record missing client_id");

        return info;
    }

} // namespace corvus::auth