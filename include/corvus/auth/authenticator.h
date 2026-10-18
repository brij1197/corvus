#pragma once
#include "corvus/auth/api_key_validator.h"
#include "corvus/auth/jwt_validator.h"
#include <functional>
#include <set>
#include <string>
#include <string_view>
#include <variant>

namespace corvus::auth
{
    struct Principal
    {
        std::string client_id;
        std::set<std::string> scopes;
        bool via_api_key{false};
        bool via_jwt{false};

        bool has_scope(const std::string &scope) const { return scopes.count(scope) > 0; }
    };

    enum class AuthFailure
    {
        unauthorized, // 401: no credential, or a bad one
        forbidden,    // 403: authenticated, but missing the scope
        unavailable,  // 503: the key store couldn't be consulted
    };

    struct AuthError
    {
        AuthFailure kind;
        std::string message;
    };

    using AuthResult = std::variant<Principal, AuthError>;

    struct Credentials
    {
        std::string method;
        std::string authorization;
        std::string api_key;
    };

    using JwtCheck = std::function<Claims(const std::string &token)>;
    using KeyCheck = std::function<ApiKeyInfo(const std::string &raw_key)>;

    std::set<std::string> parse_scopes(std::string_view raw);

    std::string required_scope(std::string_view method);

    AuthResult authenticate(const Credentials &creds,
                            const JwtCheck &check_jwt,
                            const KeyCheck &check_key);

} // namespace corvus::auth