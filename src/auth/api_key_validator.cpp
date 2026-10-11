#include "corvus/auth/api_key_validator.h"
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

    ApiKeyValidator::ApiKeyValidator() : config_(db::redis_config_from_env())
    {
        constexpr int kMaxAttempts = 5;
        std::string last_error;
        for (int attempt = 1; attempt <= kMaxAttempts; ++attempt)
        {
            try
            {
                connect();
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

    ApiKeyValidator::ApiKeyValidator(db::RedisConfig config)
        : config_(std::move(config))
    {
        try
        {
            connect();
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

    db::RedisConnection ApiKeyValidator::connect() const
    {
        return db::RedisConnection(config_);
    }

    ApiKeyInfo ApiKeyValidator::validate(const std::string &raw_key) const
    {
        if (raw_key.empty())
        {
            throw ApiKeyValidationError("API key must not be empty");
        }

        const std::string hashed = hash_key(raw_key);
        const std::string rkey = redis_key(hashed);

        std::unordered_map<std::string, std::string> fields;
        try
        {
            auto conn = connect();
            fields = conn.hgetall(rkey);
        }
        catch (const db::RedisConfigError &e)
        {
            throw ApiKeyBackendError(
                std::string("Redis unavailable, cannot validate API key: ") + e.what());
        }
        catch (const db::RedisCommandError &e)
        {
            throw ApiKeyBackendError(
                std::string("Redis could not answer API key lookup: ") + e.what());
        }

        if (fields.empty())
            throw ApiKeyValidationError("Unknown or revoked API key");

        ApiKeyInfo info;
        if (const auto it = fields.find("client_id"); it != fields.end())
            info.client_id = it->second;
        if (const auto it = fields.find("scopes"); it != fields.end())
            info.scopes = it->second;

        if (info.client_id.empty())
        {
            throw ApiKeyValidationError("API key record missing client_id");
        }

        return info;
    }

} // namespace corvus::auth