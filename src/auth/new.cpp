#include <gtest/gtest.h>
#include "corvus/auth/jwt_validator.h"
#include <jwt-cpp/jwt.h>
#include <jwt-cpp/traits/nlohmann-json/defaults.h>

using jwt_traits = jwt::traits::nlohmann_json;
#include <cstdlib>

// Keys are provided via environment variables:
//   CORVUS_TEST_PRIVATE_KEY - RSA private key PEM (for minting test tokens)
//   CORVUS_TEST_PUBLIC_KEY  - RSA public key PEM  (passed to JwtValidator)
//
// Generate once:
//   openssl genrsa -out /tmp/corvus_test.pem 2048
//   openssl rsa -in /tmp/corvus_test.pem -pubout -out /tmp/corvus_test_pub.pem
//   export CORVUS_TEST_PRIVATE_KEY="$(cat /tmp/corvus_test.pem)"
//   export CORVUS_TEST_PUBLIC_KEY="$(cat /tmp/corvus_test_pub.pem)"

static std::string get_env_or_skip(const char *var)
{
    const char *val = std::getenv(var);
    if (!val || std::string(val).empty())
    {
        return "";
    }
    return val;
}

class JwtValidatorTest : public ::testing::Test
{
protected:
    void SetUp() override
    {
        private_key_ = get_env_or_skip("CORVUS_TEST_PRIVATE_KEY");
        public_key_ = get_env_or_skip("CORVUS_TEST_PUBLIC_KEY");

        if (private_key_.empty() || public_key_.empty())
        {
            GTEST_SKIP() << "Set CORVUS_TEST_PRIVATE_KEY and CORVUS_TEST_PUBLIC_KEY "
                            "to run JWT tests. See tests/unit/test_jwt_validator.cpp.";
        }
    }

    std::string make_token(const std::string &subject = "test-user",
                           int expires_in_seconds = 3600) const
    {
        auto now = std::chrono::system_clock::now();
        return jwt::create<jwt_traits>()
            .set_type("JWT")
            .set_issuer("corvus-test")
            .set_subject(subject)
            .set_issued_at(now)
            .set_expires_at(now + std::chrono::seconds(expires_in_seconds))
            .sign(jwt::algorithm::rs256{public_key_, private_key_});
    }

    std::string private_key_;
    std::string public_key_;
};

TEST_F(JwtValidatorTest, ThrowsOnEmptyPem)
{
    EXPECT_THROW(corvus::auth::JwtValidator(""), corvus::auth::JwtConfigError);
}

TEST_F(JwtValidatorTest, ThrowsOnInvalidPem)
{
    EXPECT_THROW(corvus::auth::JwtValidator("not-a-pem"),
                 corvus::auth::JwtConfigError);
}

TEST_F(JwtValidatorTest, ConstructsWithValidPem)
{
    EXPECT_NO_THROW({ auto v = corvus::auth::JwtValidator(public_key_); (void)v; });
}

TEST_F(JwtValidatorTest, AcceptsValidToken)
{
    corvus::auth::JwtValidator v{public_key_};
    auto claims = v.validate(make_token("alice"));
    EXPECT_EQ(claims.subject, "alice");
    EXPECT_EQ(claims.issuer, "corvus-test");
}

TEST_F(JwtValidatorTest, RejectsExpiredToken)
{
    corvus::auth::JwtValidator v{public_key_};
    EXPECT_THROW(v.validate(make_token("alice", -1)),
                 corvus::auth::JwtValidationError);
}

TEST_F(JwtValidatorTest, RejectsTokenWithNoExpClaim)
{
    // jwt-cpp verifies exp only if the claim is present, so a token minted
    // without one would otherwise be accepted as never-expiring. Guards the
    // explicit has_expires_at() check in JwtValidator::validate.
    corvus::auth::JwtValidator v{public_key_};
    const auto no_exp_token =
        jwt::create<jwt_traits>()
            .set_type("JWT")
            .set_issuer("corvus-test")
            .set_subject("alice")
            .sign(jwt::algorithm::rs256{public_key_, private_key_});

    EXPECT_THROW(v.validate(no_exp_token), corvus::auth::JwtValidationError);
}

TEST_F(JwtValidatorTest, RejectsMalformedToken)
{
    corvus::auth::JwtValidator v{public_key_};
    EXPECT_THROW(v.validate("not.a.jwt"), corvus::auth::JwtValidationError);
}

TEST_F(JwtValidatorTest, RejectsEmptyToken)
{
    corvus::auth::JwtValidator v{public_key_};
    EXPECT_THROW(v.validate(""), corvus::auth::JwtValidationError);
}

TEST_F(JwtValidatorTest, RejectsWrongAlgorithm)
{
    corvus::auth::JwtValidator v{public_key_};
    auto hs_token = jwt::create<jwt_traits>()
                        .set_type("JWT")
                        .set_subject("attacker")
                        .set_expires_at(std::chrono::system_clock::now() + std::chrono::hours(1))
                        .sign(jwt::algorithm::hs256{"supersecret"});
    EXPECT_THROW(v.validate(hs_token), corvus::auth::JwtValidationError);
}

TEST(JwtValidatorEnvTest, ThrowsWhenEnvVarMissing)
{
    unsetenv("CORVUS_JWT_PUBLIC_KEY");
    EXPECT_THROW(corvus::auth::JwtValidator(), corvus::auth::JwtConfigError);
}

TEST(JwtValidatorEnvTest, LoadsKeyFromEnvVar)
{
    const char *pub = std::getenv("CORVUS_TEST_PUBLIC_KEY");
    if (!pub || std::string(pub).empty())
    {
        GTEST_SKIP() << "CORVUS_TEST_PUBLIC_KEY not set";
    }
    setenv("CORVUS_JWT_PUBLIC_KEY", pub, 1);
    EXPECT_NO_THROW(corvus::auth::JwtValidator());
    unsetenv("CORVUS_JWT_PUBLIC_KEY");
}