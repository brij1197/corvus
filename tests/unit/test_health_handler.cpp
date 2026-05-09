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
    Json::Value parse_body(const drogon::HttpResponsePtr &resp)
    {

        Json::Value root;
        Json::Reader reader;
        reader.parse(std::string(resp->getBody()), root);
        return root;
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

    TEST(HealthHandlerTest, BodyContainsStatusOk)
    {
        const auto result = invoke_handler("/health");
        ASSERT_TRUE(result.called);
        const auto body = parse_body(result.response);
        ASSERT_TRUE(body.isMember("status"));
        EXPECT_EQ(body["status"].asString(), "ok");
    }

    TEST(HealthHandlerTest, BodyStatusIsOkForReadyEndpoint)
    {
        const auto result = invoke_handler("/ready");
        ASSERT_TRUE(result.called);
        const auto body = parse_body(result.response);
        EXPECT_EQ(body["status"].asString(), "ok");
    }

    TEST(HealthHandlerTest, BodyContainsVersionField)
    {
        const auto result = invoke_handler("/health");
        ASSERT_TRUE(result.called);
        const auto body = parse_body(result.response);
        ASSERT_TRUE(body.isMember("version"));
        EXPECT_FALSE(body["version"].asString().empty());
    }

    TEST(HealthHandlerTest, BodyVersionMatchesCurrentVersion)
    {
        const auto result = invoke_handler("/health");
        ASSERT_TRUE(result.called);
        const auto body = parse_body(result.response);
        EXPECT_EQ(body["version"].asString(),
                  std::string{corvus::version::string()});
    }

    TEST(HealthHandlerTest, BodyContainsPathField)
    {
        const auto result = invoke_handler("/health");
        ASSERT_TRUE(result.called);
        const auto body = parse_body(result.response);
        ASSERT_TRUE(body.isMember("path"));
    }

    TEST(HealthHandlerTest, BodyPathMatchesRequestPathForHealth)
    {
        const auto result = invoke_handler("/health");
        ASSERT_TRUE(result.called);
        const auto body = parse_body(result.response);
        EXPECT_EQ(body["path"].asString(), "/health");
    }

    TEST(HealthHandlerTest, BodyPathMatchesRequestPathForReady)
    {
        const auto result = invoke_handler("/ready");
        ASSERT_TRUE(result.called);
        const auto body = parse_body(result.response);
        EXPECT_EQ(body["path"].asString(), "/ready");
    }

    TEST(HealthHandlerTest, BodyContainsAllRequiredFields)
    {
        const auto result = invoke_handler("/health");
        ASSERT_TRUE(result.called);
        const auto body = parse_body(result.response);
        EXPECT_TRUE(body.isMember("status"));
        EXPECT_TRUE(body.isMember("version"));
        EXPECT_TRUE(body.isMember("path"));
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
