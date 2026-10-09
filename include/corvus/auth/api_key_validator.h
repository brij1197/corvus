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

        ApiKeyValidator(const std::string &host, int port);

        ~ApiKeyValidator();

        ApiKeyInfo validate(const std::string &raw_key) const;

        static std::string hash_key(const std::string &raw_key);

    private:
        std::string host_;
        int port_;

        redisContext *connect() const;
    };

} // namespace corvus::auth