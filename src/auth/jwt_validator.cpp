#include "corvus/auth/jwt_validator.h"
#include <jwt-cpp/jwt.h>
#include <jwt-cpp/traits/nlohmann-json/defaults.h>
#include <cstdlib>

using jwt_traits = jwt::traits::nlohmann_json;

namespace corvus::auth
{

    static void verify_pem(const std::string &pem)
    {
        try
        {
            jwt::algorithm::rs256 algo{pem};
            (void)algo;
        }
        catch (const std::exception &e)
        {
            throw JwtConfigError(std::string("Invalid RS256 public key PEM: ") + e.what());
        }
    }
    JwtValidator::JwtValidator()
    {
        const char *raw = std::getenv("CORVUS_JWT_PUBLIC_KEY");
        if (!raw || std::string(raw).empty())
        {
            throw JwtConfigError(
                "CORVUS_JWT_PUBLIC_KEY is not set. "
                "Refusing to start without a JWT public key.");
        }

        verify_pem(raw);
        public_key_pem_ = raw;
    }

    JwtValidator::JwtValidator(const std::string &public_key_pem)
    {
        verify_pem(public_key_pem);
        public_key_pem_ = public_key_pem;
    }

    Claims JwtValidator::validate(const std::string &token) const
    {
        try
        {
            auto decoded = jwt::decode<jwt_traits>(token);
            if (!decoded.has_expires_at())
            {
                throw JwtValidationError("Token is missing required 'exp' claim");
            }

            jwt::verify<jwt_traits>(jwt::default_clock{})
                .allow_algorithm(jwt::algorithm::rs256{public_key_pem_})
                .with_type("JWT")
                .verify(decoded);

            Claims claims;
            if (decoded.has_subject())
            {
                claims.subject = decoded.get_subject();
            }
            if (decoded.has_issuer())
            {
                claims.issuer = decoded.get_issuer();
            }
            return claims;
        }
        catch (const JwtValidationError &)
        {
            throw;
        }
        catch (const jwt::error::token_verification_exception &e)
        {
            throw JwtValidationError(e.what());
        }
        catch (const std::exception &e)
        {
            throw JwtValidationError(std::string("Token decode failed: ") + e.what());
        }
    }
} // namespace corvus::auth