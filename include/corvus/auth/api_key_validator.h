#pragma once
#include "corvus/db/redis_connection.h"
#include <chrono>
#include <functional>
#include <memory>
#include <mutex>
#include <optional>
#include <stdexcept>
#include <string>
#include <vector>

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

    struct ApiKeyRecord
    {
        std::string client_id;
        std::string scopes;
        std::optional<std::chrono::system_clock::time_point> expires_at;
    };

    using ApiKeyLookup = std::function<std::optional<ApiKeyRecord>(const std::string &key_hash)>;

    class ApiKeyValidator
    {
    public:
        static constexpr std::chrono::seconds kCacheTtl{60};

        ApiKeyValidator();

        explicit ApiKeyValidator(db::RedisConfig config, ApiKeyLookup lookup = nullptr);

        ApiKeyValidator(const std::string &host, int port);

        ~ApiKeyValidator();

        ApiKeyInfo validate(const std::string &raw_key) const;

        static std::string hash_key(const std::string &raw_key);

        const db::RedisConfig &config() const noexcept { return config_; }

        std::size_t idle_connections() const;

        void set_lookup(ApiKeyLookup lookup) { lookup_ = std::move(lookup); }

    private:
        static constexpr std::size_t kMaxIdleConnections = 8;

        db::RedisConfig config_;
        ApiKeyLookup lookup_;

        mutable std::mutex idle_mutex_;
        mutable std::vector<std::unique_ptr<db::RedisConnection>> idle_;

        std::unique_ptr<db::RedisConnection> acquire() const;
        void release(std::unique_ptr<db::RedisConnection> conn) const;

        std::optional<ApiKeyInfo> from_cache(const std::string &rkey) const;
        void store_in_cache(const std::string &rkey, const ApiKeyRecord &record) const;
    };

} // namespace corvus::auth