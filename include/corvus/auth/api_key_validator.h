#pragma once
#include <string>
#include <stdexcept>
#include <memory>

struct redisContext;

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

    struct ApiKeyInfo
    {
        std::string client_id;
        std::string scopes;
    };

    /// Connects to Redis at startup and validates API keys on each request
    /// Thread-safe: each call opens its own connection

    class ApiKeyValidator
    {
    public:
        ApiKeyValidator();

        /// Exposed for testing
        ApiKeyValidator(const std::string &host, int port);

        ~ApiKeyValidator();

        /// Look up a raw API key. Returns ApiKeyInfo on success.
        ApiKeyInfo validate(const std::string &raw_key) const;

        /// SHA-256 hash a raw key
        static std::string hash_key(const std::string &raw_key);

    private:
        std::string host_;
        int port_;

        redisContext *connect() const;
    };

} // namespace corus::auth