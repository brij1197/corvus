#include <gtest/gtest.h>
#include "corvus/api/envelope.h"
#include "corvus/api/response.h"
#include "corvus/gateway/request_size_limit.h"

using namespace corvus::api;

TEST(ErrorCodeTest, ToStringPayloadTooLarge)
{
    EXPECT_EQ(to_string(ErrorCode::payload_too_large), "PAYLOAD_TOO_LARGE");
}

TEST(HttpStatusTest, PayloadTooLargeIs413)
{
    EXPECT_EQ(http_status(ErrorCode::payload_too_large),
              drogon::k413RequestEntityTooLarge);
}

TEST(RequestSizeLimitConfigTest, DefaultMaxBodyIs1MiB)
{
    corvus::gateway::RequestSizeLimitConfig cfg;
    EXPECT_EQ(cfg.max_body_bytes, 1024u * 1024u);
}

TEST(RequestSizeLimitConfigTest, CustomMaxBodyIsRespected)
{
    corvus::gateway::RequestSizeLimitConfig cfg{.max_body_bytes = 512};
    EXPECT_EQ(cfg.max_body_bytes, 512u);
}

TEST(RequestSizeLimitResponseTest, Returns413StatusCode)
{
    auto resp = respond_error(
        ErrorCode::payload_too_large,
        "Request body exceeds the maximum allowed size of 1048576 bytes.",
        "test-request-id");

    EXPECT_EQ(resp->statusCode(), drogon::k413RequestEntityTooLarge);
}

TEST(RequestSizeLimitResponseTest, ResponseBodyHasCorrectErrorCode)
{
    auto resp = respond_error(
        ErrorCode::payload_too_large,
        "Request body exceeds the maximum allowed size of 1048576 bytes.",
        "test-request-id");

    const auto body = resp->body();
    EXPECT_NE(body.find("PAYLOAD_TOO_LARGE"), std::string::npos);
}

TEST(RequestSizeLimitResponseTest, ResponseBodyHasErrorMessage)
{
    auto resp = respond_error(
        ErrorCode::payload_too_large,
        "Request body exceeds the maximum allowed size of 1048576 bytes.",
        "test-request-id");

    EXPECT_NE(resp->body().find("1048576"), std::string::npos);
}

TEST(RequestSizeLimitResponseTest, ResponseBodyHasNullData)
{
    auto resp = respond_error(
        ErrorCode::payload_too_large,
        "Too large.",
        "req-123");

    EXPECT_NE(resp->body().find("\"data\":null"), std::string::npos);
}

TEST(RequestSizeLimitResponseTest, ResponseBodyHasRequestIdInMeta)
{
    auto resp = respond_error(
        ErrorCode::payload_too_large,
        "Too large.",
        "my-request-id-xyz");

    EXPECT_NE(resp->body().find("my-request-id-xyz"), std::string::npos);
}

TEST(RequestSizeLimitResponseTest, ResponseContentTypeIsJson)
{
    auto resp = respond_error(
        ErrorCode::payload_too_large,
        "Too large.",
        "req-456");

    const auto ct = std::string(resp->getHeader("content-type"));
    EXPECT_NE(ct.find("application/json"), std::string::npos);
}