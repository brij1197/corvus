#include <gtest/gtest.h>
#include "corvus/auth/client_context.h"
#include <drogon/drogon.h>

using namespace corvus::auth;

TEST(ClientContextTest, ReturnsEmptyWhenNotSet)
{
    auto req = drogon::HttpRequest::newHttpRequest();
    EXPECT_EQ(get_client_id(req), "");
}

TEST(ClientContextTest, ReturnsValueAfterInsert)
{
    auto req = drogon::HttpRequest::newHttpRequest();
    req->getAttributes()->insert(kClientIdKey, std::string("client-abc-123"));

    EXPECT_EQ(get_client_id(req), "client-abc-123");
}

TEST(ClientContextTest, RoundTripsUuidShapedValue)
{
    auto req = drogon::HttpRequest::newHttpRequest();
    const std::string uuid = "11111111-2222-3333-4444-555555555555";
    req->getAttributes()->insert(kClientIdKey, uuid);

    EXPECT_EQ(get_client_id(req), uuid);
}

TEST(ClientContextTest, KeyConstantIsStable)
{
    EXPECT_STREQ(kClientIdKey, "corvus-client-id");
}