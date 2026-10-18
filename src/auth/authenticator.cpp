#include "corvus/auth/authenticator.h"
#include <algorithm>
#include <iterator>

namespace corvus::auth
{
    namespace
    {
        AuthError unauthorized(std::string msg) { return {AuthFailure::unauthorized, std::move(msg)}; }

        constexpr std::string_view kBearer = "Bearer ";
    } // namespace

    std::set<std::string> parse_scopes(std::string_view raw)
    {
        std::set<std::string> out;
        std::string current;
        for (const char ch : raw)
        {
            if (ch == ',' || ch == ' ' || ch == '\t')
            {
                if (!current.empty())
                    out.insert(std::move(current));
                current.clear();
            }
            else
            {
                current.push_back(ch);
            }
        }
        if (!current.empty())
            out.insert(std::move(current));
        return out;
    }

    std::string required_scope(std::string_view method)
    {
        return (method == "GET" || method == "HEAD" || method == "OPTIONS") ? "read" : "write";
    }

    AuthResult authenticate(const Credentials &creds,
                            const JwtCheck &check_jwt,
                            const KeyCheck &check_key)
    {
        const bool has_key = !creds.api_key.empty();
        const bool has_jwt = !creds.authorization.empty();

        if (!has_key && !has_jwt)
            return unauthorized(
                "Missing credentials: send an X-Api-Key header or an "
                "Authorization: Bearer token");

        Principal principal;

        if (has_key)
        {
            try
            {
                const auto info = check_key(creds.api_key);
                principal.client_id = info.client_id;
                principal.scopes = parse_scopes(info.scopes);
                principal.via_api_key = true;
            }
            catch (const ApiKeyBackendError &)
            {
                return AuthError{AuthFailure::unavailable,
                                 "Authentication is temporarily unavailable. Try again later."};
            }
            catch (const ApiKeyValidationError &)
            {
                return unauthorized("Invalid or expired API key");
            }
        }

        if (has_jwt)
        {
            if (creds.authorization.rfind(kBearer, 0) != 0 ||
                creds.authorization.size() == kBearer.size())
                return unauthorized("Malformed Authorization header: expected 'Bearer <token>'");

            Claims claims;
            try
            {
                claims = check_jwt(creds.authorization.substr(kBearer.size()));
            }
            catch (const JwtValidationError &)
            {
                return unauthorized("Invalid or expired token");
            }

            if (!claims.client_id || claims.client_id->empty())
                return unauthorized("Token is not bound to a client (missing client_id claim)");

            const auto token_scopes = parse_scopes(claims.scope);
            if (principal.via_api_key)
            {
                if (*claims.client_id != principal.client_id)
                    return unauthorized("API key and token belong to different clients");

                std::set<std::string> both;
                std::set_intersection(principal.scopes.begin(), principal.scopes.end(),
                                      token_scopes.begin(), token_scopes.end(),
                                      std::inserter(both, both.begin()));
                principal.scopes = std::move(both);
            }
            else
            {
                principal.client_id = *claims.client_id;
                principal.scopes = token_scopes;
            }
            principal.via_jwt = true;
        }

        const auto needed = required_scope(creds.method);
        if (!principal.has_scope(needed))
            return AuthError{AuthFailure::forbidden,
                             "This credential lacks the '" + needed + "' scope"};

        return principal;
    }

} // namespace corvus::auth