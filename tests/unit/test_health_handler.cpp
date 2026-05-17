#include <gtest/gtest.h>
#include <gmock/gmock.h>
#include "corvus/version.h"
#include "corvus/gateway/health_handler.h"
#include <drogon/drogon.h>
#include <json/json.h>

namespace
{
    struct HandlerResult
    {
        drogon::HttpResponsePtr response;
        bool called{false};
    };

    HandlerResult invoke_handler(const std::string &path)
    {
        HandlerResult result;

        auto req = drogon::HttpRequest::newHttpRequest();
        req->setPath(path);
        req->setMethod(drogon::Get);

        auto handler = corvus::gateway::health_handler();
        handler(req, [&result](const drogon::HttpResponsePtr &resp)
                {
            result.response = resp;
            result.called = true; });
        return result;
    }

    Json::Value parse_envelope(const drogon::HttpResponsePtr &resp)
    {
        Json::Value root;
        Json::Reader reader;
        reader.parse(std::string{resp->getBody()}, root);
        return root;
    }

    Json::Value parse_data(const drogon::HttpResponsePtr &resp)
    {
        return parse_envelope(resp)["data"];
    }

    TEST(HealthHandlerTest, CallbackIsAlwaysInvoked)
    {
        const auto result = invoke_handler("/health");
        EXPECT_TRUE(result.called);
    }

    TEST(HealthHandlerTest, CallbackIsInvokedForReadyPath)
    {
        const auto result = invoke_handler("/ready");
        EXPECT_TRUE(result.called);
    }

    TEST(HealthHandlerTest, ReturnsHttp200ForHealth)
    {
        const auto result = invoke_handler("/health");
        ASSERT_TRUE(result.called);
        EXPECT_EQ(result.response->getStatusCode(), drogon::k200OK);
    }

    TEST(HealthHandlerTest, ReturnsHttp200ForReady)
    {
        const auto result = invoke_handler("/ready");
        ASSERT_TRUE(result.called);
        EXPECT_EQ(result.response->getStatusCode(), drogon::k200OK);
    }

    // Envelope shape

    TEST(HealthHandlerTest, ResponseHasEnvelopeShape)
    {
        const auto result = invoke_handler("/health");
        ASSERT_TRUE(result.called);
        const auto env = parse_envelope(result.response);
        EXPECT_TRUE(env.isMember("data"));
        EXPECT_TRUE(env.isMember("error"));
        EXPECT_TRUE(env.isMember("meta"));
    }

    TEST(HealthHandlerTest, ResponseErrorFieldIsNull)
    {
        const auto result = invoke_handler("/health");
        ASSERT_TRUE(result.called);
        const auto env = parse_envelope(result.response);
        EXPECT_TRUE(env["error"].isNull());
    }

    TEST(HealthHandlerTest, ResponseMetaHasRequestId)
    {
        const auto result = invoke_handler("/health");
        ASSERT_TRUE(result.called);
        const auto env = parse_envelope(result.response);
        EXPECT_TRUE(env["meta"].isMember("request_id"));
        EXPECT_FALSE(env["meta"]["request_id"].asString().empty());
    }

    TEST(HealthHandlerTest, ResponseMetaHasTimestamp)
    {
        const auto result = invoke_handler("/health");
        ASSERT_TRUE(result.called);
        const auto env = parse_envelope(result.response);
        EXPECT_TRUE(env["meta"].isMember("timestamp"));
        EXPECT_FALSE(env["meta"]["timestamp"].asString().empty());
    }

    TEST(HealthHandlerTest, BodyContainsStatusOk)
    {
        const auto result = invoke_handler("/health");
        ASSERT_TRUE(result.called);
        const auto data = parse_data(result.response);
        ASSERT_TRUE(data.isMember("status"));
        EXPECT_EQ(data["status"].asString(), "ok");
    }

    TEST(HealthHandlerTest, BodyStatusIsOkForReadyEndpoint)
    {
        const auto result = invoke_handler("/ready");
        ASSERT_TRUE(result.called);
        const auto data = parse_data(result.response);
        EXPECT_EQ(data["status"].asString(), "ok");
    }

    TEST(HealthHandlerTest, BodyContainsVersionField)
    {
        const auto result = invoke_handler("/health");
        ASSERT_TRUE(result.called);
        const auto data = parse_data(result.response);
        ASSERT_TRUE(data.isMember("version"));
        EXPECT_FALSE(data["version"].asString().empty());
    }

    TEST(HealthHandlerTest, BodyVersionMatchesCurrentVersion)
    {
        const auto result = invoke_handler("/health");
        ASSERT_TRUE(result.called);
        const auto data = parse_data(result.response);
        EXPECT_EQ(data["version"].asString(),
                  std::string{corvus::version::string()});
    }

    TEST(HealthHandlerTest, BodyContainsPathField)
    {
        const auto result = invoke_handler("/health");
        ASSERT_TRUE(result.called);
        const auto data = parse_data(result.response);
        EXPECT_TRUE(data.isMember("path"));
    }

    TEST(HealthHandlerTest, BodyPathMatchesRequestPathForHealth)
    {
        const auto result = invoke_handler("/health");
        ASSERT_TRUE(result.called);
        const auto data = parse_data(result.response);
        EXPECT_EQ(data["path"].asString(), "/health");
    }

    TEST(HealthHandlerTest, BodyPathMatchesRequestPathForReady)
    {
        const auto result = invoke_handler("/ready");
        ASSERT_TRUE(result.called);
        const auto data = parse_data(result.response);
        EXPECT_EQ(data["path"].asString(), "/ready");
    }

    TEST(HealthHandlerTest, BodyContainsAllRequiredFields)
    {
        const auto result = invoke_handler("/health");
        ASSERT_TRUE(result.called);
        const auto data = parse_data(result.response);
        EXPECT_TRUE(data.isMember("status"));
        EXPECT_TRUE(data.isMember("version"));
        EXPECT_TRUE(data.isMember("path"));
    }

    TEST(HealthHandlerTest, HandlerCanBeInvokedMultipleTimes)
    {
        auto handler = corvus::gateway::health_handler();

        for (int i = 0; i < 5; ++i)
        {
            bool called = false;
            auto req = drogon::HttpRequest::newHttpRequest();
            req->setPath("/health");
            handler(req, [&called](const drogon::HttpResponsePtr &resp)
                    {
            called = true;
            EXPECT_EQ(resp->getStatusCode(), drogon::k200OK); });
            EXPECT_TRUE(called) << "Handler not called on iteration " << i;
        }
    }

} // namespace
