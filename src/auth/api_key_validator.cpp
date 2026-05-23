#include "corvus/auth/api_key_validator.h"
#include <hiredis/hiredis.h>
#include <openssl/evp.h>
#include <cstdlib>
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

    ApiKeyValidator::ApiKeyValidator()
    {
        const char *host = std::getenv("CORVUS_REDIS_HOST");
        const char *port = std::getenv("CORVUS_REDIS_PORT");

        host_ = (host && *host) ? host : "localhost";
        port_ = (port && *port) ? std::stoi(port) : 6379;

        // Retry up to 5 times with 1s backoff — Docker DNS may not resolve
        // immediately even after the Redis healthcheck passes
        std::string last_error;
        for (int attempt = 1; attempt <= 5; ++attempt)
        {
            try
            {
                redisContext *c = connect();
                redisFree(c);
                return; // Connected successfully
            }
            catch (const ApiKeyConfigError &e)
            {
                last_error = e.what();
                if (attempt < 5)
                {
                    std::this_thread::sleep_for(std::chrono::seconds(1));
                }
            }
        }
        throw ApiKeyConfigError("Redis unreachable after 5 attempts: " + last_error);
    }

    ApiKeyValidator::ApiKeyValidator(const std::string &host, int port)
        : host_(host), port_(port)
    {
        redisContext *c = connect();
        redisFree(c);
    }

    ApiKeyValidator::~ApiKeyValidator() = default;

    redisContext *ApiKeyValidator::connect() const
    {
        struct timeval timeout = {3, 0}; // 3 seconds
        redisContext *c = redisConnectWithTimeout(host_.c_str(), port_, timeout);

        if (!c || c->err)
        {
            std::string msg = "Redis connection failed";
            if (c)
            {
                msg += ": ";
                msg += c->errstr;
                redisFree(c);
            }
            throw ApiKeyConfigError(msg);
        }
        return c;
    }

    ApiKeyInfo ApiKeyValidator::validate(const std::string &raw_key) const
    {
        if (raw_key.empty())
        {
            throw ApiKeyValidationError("API key must not be empty");
        }

        const std::string hashed = hash_key(raw_key);
        const std::string rkey = redis_key(hashed);

        redisContext *c = nullptr;
        try
        {
            c = connect();
        }
        catch (const ApiKeyConfigError &e)
        {
            throw ApiKeyValidationError(
                std::string("Redis unavailable, cannot validate API key: ") + e.what());
        }

        // HGETALL corvus:apikeys:<hash>
        auto *reply = static_cast<redisReply *>(
            redisCommand(c, "HGETALL %s", rkey.c_str()));
        redisFree(c);

        if (!reply)
        {
            throw ApiKeyValidationError("Redis returned null reply");
        }

        // HGETALL returns flat array: [field, value, field, value, ...]
        if (reply->type != REDIS_REPLY_ARRAY || reply->elements == 0)
        {
            freeReplyObject(reply);
            throw ApiKeyValidationError("Unknown or revoked API key");
        }

        ApiKeyInfo info;
        for (size_t i = 0; i + 1 < reply->elements; i += 2)
        {
            std::string field = reply->element[i]->str;
            std::string value = reply->element[i + 1]->str;
            if (field == "client_id")
                info.client_id = value;
            else if (field == "scopes")
                info.scopes = value;
        }
        freeReplyObject(reply);

        if (info.client_id.empty())
        {
            throw ApiKeyValidationError("API key record missing client_id");
        }

        return info;
    }

} // namespace corvus::auth