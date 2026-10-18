#include <gtest/gtest.h>
#include "corvus/auth/authenticator.h"

using namespace corvus::auth;

namespace
{
    const KeyCheck keys = [](const std::string &raw) -> ApiKeyInfo
    {
        if (raw == "key-a-rw")
            return {"client-a", "read,write"};
        if (raw == "key-a-ro")
            return {"client-a", "read"};
        if (raw == "key-b-rw")
            return {"client-b", "read write"};
        if (raw == "key-outage")
            throw ApiKeyBackendError("redis down");
        throw ApiKeyValidationError("unknown");
    };

    const JwtCheck tokens = [](const std::string &token) -> Claims
    {
        if (token == "tok-a-rw")
            return {"user", "iss", "client-a", "read write"};
        if (token == "tok-a-ro")
            return {"user", "iss", "client-a", "read"};
        if (token == "tok-b-rw")
            return {"user", "iss", "client-b", "read write"};
        if (token == "tok-unbound")
            return {"user", "iss", std::nullopt, "read write"};
        throw JwtValidationError("bad token");
    };

    AuthResult run(std::string method, std::string authorization, std::string api_key)
    {
        return authenticate({std::move(method), std::move(authorization), std::move(api_key)},
                            tokens, keys);
    }

    Principal ok(const AuthResult &r)
    {
        EXPECT_TRUE(std::holds_alternative<Principal>(r))
            << std::get<AuthError>(r).message;
        return std::get<Principal>(r);
    }

    AuthFailure failure(const AuthResult &r)
    {
        EXPECT_TRUE(std::holds_alternative<AuthError>(r));
        return std::get<AuthError>(r).kind;
    }
} // namespace

TEST(AuthenticatorTest, ApiKeyAloneAuthenticates)
{
    const auto p = ok(run("GET", "", "key-a-rw"));
    EXPECT_EQ(p.client_id, "client-a");
    EXPECT_TRUE(p.via_api_key);
    EXPECT_FALSE(p.via_jwt);
}

TEST(AuthenticatorTest, ClientBoundJwtAloneAuthenticates)
{
    const auto p = ok(run("GET", "Bearer tok-a-rw", ""));
    EXPECT_EQ(p.client_id, "client-a");
    EXPECT_TRUE(p.via_jwt);
}

TEST(AuthenticatorTest, NoCredentialsIs401)
{
    const auto r = run("GET", "", "");
    EXPECT_EQ(failure(r), AuthFailure::unauthorized);
    EXPECT_NE(std::get<AuthError>(r).message.find("X-Api-Key"), std::string::npos);
    EXPECT_NE(std::get<AuthError>(r).message.find("Authorization"), std::string::npos);
}

TEST(AuthenticatorTest, JwtWithoutClientIdIs401)
{
    EXPECT_EQ(failure(run("GET", "Bearer tok-unbound", "")), AuthFailure::unauthorized);
}

TEST(AuthenticatorTest, InvalidJwtIs401)
{
    EXPECT_EQ(failure(run("GET", "Bearer garbage", "")), AuthFailure::unauthorized);
}

TEST(AuthenticatorTest, MalformedAuthorizationHeaderIs401)
{
    EXPECT_EQ(failure(run("GET", "tok-a-rw", "")), AuthFailure::unauthorized);
    EXPECT_EQ(failure(run("GET", "Bearer ", "")), AuthFailure::unauthorized);
    EXPECT_EQ(failure(run("GET", "Basic dXNlcjpwYXNz", "")), AuthFailure::unauthorized);
}

TEST(AuthenticatorTest, MatchingKeyAndJwtAuthenticate)
{
    const auto p = ok(run("POST", "Bearer tok-a-rw", "key-a-rw"));
    EXPECT_EQ(p.client_id, "client-a");
    EXPECT_TRUE(p.via_api_key && p.via_jwt);
}

TEST(AuthenticatorTest, KeyAndJwtForDifferentClientsIs401)
{
    EXPECT_EQ(failure(run("GET", "Bearer tok-b-rw", "key-a-rw")), AuthFailure::unauthorized);
}

TEST(AuthenticatorTest, BadJwtNextToGoodKeyIs401)
{
    // A presented credential is never silently ignored.
    EXPECT_EQ(failure(run("GET", "Bearer garbage", "key-a-rw")), AuthFailure::unauthorized);
}

TEST(AuthenticatorTest, BadKeyNextToGoodJwtIs401)
{
    EXPECT_EQ(failure(run("GET", "Bearer tok-a-rw", "nope")), AuthFailure::unauthorized);
}

TEST(AuthenticatorTest, BothCredentialsGetOnlyTheScopesBothGrant)
{
    const auto p = ok(run("GET", "Bearer tok-a-ro", "key-a-rw"));
    EXPECT_TRUE(p.has_scope("read"));
    EXPECT_FALSE(p.has_scope("write"));
    EXPECT_EQ(failure(run("DELETE", "Bearer tok-a-ro", "key-a-rw")), AuthFailure::forbidden);
}

TEST(AuthenticatorTest, KeyStoreOutageIs503)
{
    EXPECT_EQ(failure(run("GET", "", "key-outage")), AuthFailure::unavailable);
}

TEST(AuthenticatorTest, ReadScopeAllowsReads)
{
    for (const char *m : {"GET", "HEAD", "OPTIONS"})
        ok(run(m, "", "key-a-ro"));
}

TEST(AuthenticatorTest, ReadScopeCannotWrite)
{
    for (const char *m : {"POST", "PUT", "PATCH", "DELETE"})
    {
        const auto r = run(m, "", "key-a-ro");
        EXPECT_EQ(failure(r), AuthFailure::forbidden) << m;
        EXPECT_NE(std::get<AuthError>(r).message.find("write"), std::string::npos);
    }
}

TEST(AuthenticatorTest, JwtScopesAreEnforcedToo)
{
    EXPECT_EQ(failure(run("POST", "Bearer tok-a-ro", "")), AuthFailure::forbidden);
    ok(run("POST", "Bearer tok-a-rw", ""));
}

TEST(ParseScopesTest, AcceptsCommasAndSpaces)
{
    EXPECT_EQ(parse_scopes("read,write"), (std::set<std::string>{"read", "write"}));
    EXPECT_EQ(parse_scopes("read write"), (std::set<std::string>{"read", "write"}));
    EXPECT_EQ(parse_scopes(" read ,, write "), (std::set<std::string>{"read", "write"}));
    EXPECT_TRUE(parse_scopes("").empty());
}

TEST(RequiredScopeTest, MapsMethods)
{
    EXPECT_EQ(required_scope("GET"), "read");
    EXPECT_EQ(required_scope("HEAD"), "read");
    EXPECT_EQ(required_scope("POST"), "write");
    EXPECT_EQ(required_scope("PATCH"), "write");
    EXPECT_EQ(required_scope("DELETE"), "write");
}