#pragma once
#include "corvus/db/redis_connection.h"
#include <string>
#include <stdexcept>
#include <memory>

namespace corvus::auth
{

    struct ApiKeyConfigError : std::runtime_error
    {
        using std::runtime_error::runtime_error;
    };

    struct ApiKeyValidationError : std::runtime_error
    {
        using std::runtime_error::runtime_error;
    };

    struct ApiKeyBackendError : std::runtime_error
    {
        using std::runtime_error::runtime_error;
    };

    struct ApiKeyInfo
    {
        std::string client_id;
        std::string scopes;
    };

    class ApiKeyValidator
    {
    public:
        ApiKeyValidator();

        explicit ApiKeyValidator(db::RedisConfig config);

        ApiKeyValidator(const std::string &host, int port);

        ~ApiKeyValidator();

        ApiKeyInfo validate(const std::string &raw_key) const;

        static std::string hash_key(const std::string &raw_key);

        const db::RedisConfig &config() const noexcept { return config_; }

    private:
        db::RedisConfig config_;
        db::RedisConnection connect() const;
    };

} // namespace corvus::auth