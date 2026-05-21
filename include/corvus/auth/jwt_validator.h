#pragma once
#include <string>
#include <optional>
#include <stdexcept>

namespace corvus::auth
{

    /// Thrown at startup if CORVUS_JWT_PUBLIC_KEY is missing or unparseable
    struct JwtConfigError : std::runtime_error
    {
        using std::runtime_error::runtime_error;
    };

    /// Thrown when a token fails validation (missing, expired, wrong alg, etc.)
    struct JwtValidationError : std::runtime_error
    {
        using std::runtime_error::runtime_error;
    };

    /// Holds the validated claims extracted from a token
    struct Claims
    {
        std::string subject;
        std::string issuer;
    };

    /// Loads an RS256 public key from the environment and validates JWT Bearer tokens
    /// Construct once at startup; share across threads (immutable after construction)

    class JwtValidator
    {
    public:
        /// Reads CORVUS_JWT_PUBLIC_KEY from the environment
        /// Throws JwtConfigError if the variable is absent or the PEM is invalid
        JwtValidator();

        /// Exposed for testing - pass the PEM directly
        explicit JwtValidator(const std::string &public_key_pem);

        /// Validate a raw token string (without "Bearer " prefix)
        /// Returns Claims on success; throws JwtValidationError on any failure
        Claims validate(const std::string &token) const;

    private:
        std::string public_key_pem_;
    };
} // namespace corvus::auth