#include <gtest/gtest.h>
#include "corvus/api/envelope.h"

namespace
{

    // ok() builder

    TEST(EnvelopeTest, OkHasNullError)
    {
        Json::Value data;
        data["id"] = "123";

        const auto env = corvus::api::ok(data, "req-1");
        EXPECT_FALSE(env.error.has_value());
    }

    TEST(EnvelopeTest, OkHasCorrectData)
    {
        Json::Value data;
        data["id"] = "123";

        const auto env = corvus::api::ok(data, "req-1");
        EXPECT_EQ(env.data["id"].asString(), "123");
    }

    TEST(EnvelopeTest, OkMetaHasRequestId)
    {
        const auto env = corvus::api::ok(Json::Value{}, "my-request-id");
        EXPECT_EQ(env.meta.request_id, "my-request-id");
    }

    TEST(EnvelopeTest, OkMetaHasTimestamp)
    {
        const auto env = corvus::api::ok(Json::Value{}, "req-1");
        EXPECT_FALSE(env.meta.timestamp.empty());
    }

    TEST(EnvelopeTest, OkMetaHasNoPagination)
    {
        const auto env = corvus::api::ok(Json::Value{}, "req-1");
        EXPECT_FALSE(env.meta.next_cursor.has_value());
        EXPECT_FALSE(env.meta.has_more.has_value());
    }

    // ok_list() builder

    TEST(EnvelopeTest, OkListHasPaginationCursor)
    {
        Json::Value items{Json::arrayValue};
        const auto env = corvus::api::ok_list(items, "req-1", "cursor-abc", true);
        ASSERT_TRUE(env.meta.next_cursor.has_value());
        EXPECT_EQ(env.meta.next_cursor.value(), "cursor-abc");
    }

    TEST(EnvelopeTest, OkListHasMoreFlag)
    {
        Json::Value items{Json::arrayValue};
        const auto env = corvus::api::ok_list(items, "req-1", "cursor-abc", true);
        ASSERT_TRUE(env.meta.has_more.has_value());
        EXPECT_TRUE(env.meta.has_more.value());
    }

    TEST(EnvelopeTest, OkListWithNoCursorHasNoMore)
    {
        Json::Value items{Json::arrayValue};
        const auto env = corvus::api::ok_list(items, "req-1", std::nullopt, false);
        ASSERT_TRUE(env.meta.has_more.has_value());
        EXPECT_FALSE(env.meta.has_more.value());
    }

    // error() builder

    TEST(EnvelopeTest, ErrorHasNullData)
    {
        const auto env = corvus::api::error(
            corvus::api::ErrorCode::not_found, "not found", "req-1");
        EXPECT_TRUE(env.data.isNull());
    }

    TEST(EnvelopeTest, ErrorHasErrorField)
    {
        const auto env = corvus::api::error(
            corvus::api::ErrorCode::not_found, "not found", "req-1");
        ASSERT_TRUE(env.error.has_value());
    }

    TEST(EnvelopeTest, ErrorCodeIsCorrect)
    {
        const auto env = corvus::api::error(
            corvus::api::ErrorCode::not_found, "not found", "req-1");
        EXPECT_EQ(env.error->code, corvus::api::ErrorCode::not_found);
    }

    TEST(EnvelopeTest, ErrorMessageIsCorrect)
    {
        const auto env = corvus::api::error(
            corvus::api::ErrorCode::bad_request, "invalid input", "req-1");
        EXPECT_EQ(env.error->message, "invalid input");
    }

    TEST(EnvelopeTest, ErrorMetaHasRequestId)
    {
        const auto env = corvus::api::error(
            corvus::api::ErrorCode::internal_error, "oops", "req-xyz");
        EXPECT_EQ(env.meta.request_id, "req-xyz");
    }

    // to_json() serialization

    TEST(EnvelopeTest, SuccessJsonHasDataField)
    {
        Json::Value data;
        data["key"] = "value";
        const auto json = corvus::api::ok(data, "req-1").to_json();
        EXPECT_TRUE(json.isMember("data"));
        EXPECT_EQ(json["data"]["key"].asString(), "value");
    }

    TEST(EnvelopeTest, SuccessJsonHasNullErrorField)
    {
        const auto json = corvus::api::ok(Json::Value{}, "req-1").to_json();
        EXPECT_TRUE(json.isMember("error"));
        EXPECT_TRUE(json["error"].isNull());
    }

    TEST(EnvelopeTest, SuccessJsonHasMetaField)
    {
        const auto json = corvus::api::ok(Json::Value{}, "req-1").to_json();
        EXPECT_TRUE(json.isMember("meta"));
        EXPECT_TRUE(json["meta"].isMember("request_id"));
        EXPECT_TRUE(json["meta"].isMember("timestamp"));
    }

    TEST(EnvelopeTest, ErrorJsonHasNullDataField)
    {
        const auto json = corvus::api::error(
                              corvus::api::ErrorCode::not_found, "not found", "req-1")
                              .to_json();
        EXPECT_TRUE(json["data"].isNull());
    }

    TEST(EnvelopeTest, ErrorJsonHasErrorObject)
    {
        const auto json = corvus::api::error(
                              corvus::api::ErrorCode::not_found, "Resource not found", "req-1")
                              .to_json();
        ASSERT_TRUE(json["error"].isObject());
        EXPECT_EQ(json["error"]["code"].asString(), "NOT_FOUND");
        EXPECT_EQ(json["error"]["message"].asString(), "Resource not found");
    }

    TEST(EnvelopeTest, PaginatedJsonHasCursorInMeta)
    {
        Json::Value items{Json::arrayValue};
        const auto json = corvus::api::ok_list(
                              items, "req-1", "next-cursor", true)
                              .to_json();
        EXPECT_EQ(json["meta"]["next_cursor"].asString(), "next-cursor");
        EXPECT_TRUE(json["meta"]["has_more"].asBool());
    }

    // ErrorCode to_string

    TEST(ErrorCodeTest, ToStringNotFound)
    {
        EXPECT_EQ(corvus::api::to_string(corvus::api::ErrorCode::not_found),
                  "NOT_FOUND");
    }

    TEST(ErrorCodeTest, ToStringBadRequest)
    {
        EXPECT_EQ(corvus::api::to_string(corvus::api::ErrorCode::bad_request),
                  "BAD_REQUEST");
    }

    TEST(ErrorCodeTest, ToStringInternalError)
    {
        EXPECT_EQ(corvus::api::to_string(corvus::api::ErrorCode::internal_error),
                  "INTERNAL_ERROR");
    }

    TEST(ErrorCodeTest, ToStringPolicyDenied)
    {
        EXPECT_EQ(corvus::api::to_string(corvus::api::ErrorCode::policy_denied),
                  "POLICY_DENIED");
    }

    TEST(ErrorCodeTest, ToStringQuotaExceeded)
    {
        EXPECT_EQ(corvus::api::to_string(corvus::api::ErrorCode::quota_exceeded),
                  "QUOTA_EXCEEDED");
    }

} // namespace