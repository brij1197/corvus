#include <gtest/gtest.h>
#include "corvus/gateway/router.h"
#include "corvus/gateway/health_handler.h"
#include "corvus/gateway/not_found_handler.h"
#include "corvus/api/envelope.h"
#include <drogon/drogon.h>
#include <json/json.h>

namespace
{

    TEST(RouterTest, HealthHandlerFactoryReturnsNonNullCallable)
    {
        auto handler = corvus::gateway::health_handler();
        EXPECT_TRUE(static_cast<bool>(handler));
    }

    TEST(RouterTest, HealthHandlerReturns200)
    {
        auto handler = corvus::gateway::health_handler();
        auto req = drogon::HttpRequest::newHttpRequest();
        req->setPath("/health");

        bool called = false;
        handler(req, [&called](const drogon::HttpResponsePtr &resp)
                {
        called = true;
        EXPECT_EQ(resp->getStatusCode(), drogon::k200OK); });
        EXPECT_TRUE(called);
    }

    TEST(RouterTest, HealthHandlerBodyHasEnvelopeShape)
    {
        auto handler = corvus::gateway::health_handler();
        auto req = drogon::HttpRequest::newHttpRequest();
        req->setPath("/health");

        handler(req, [](const drogon::HttpResponsePtr &resp)
                {
        Json::Value  root;
        Json::Reader reader;
        reader.parse(std::string{resp->getBody()}, root);

        EXPECT_TRUE(root.isMember("data"));
        EXPECT_TRUE(root.isMember("error"));
        EXPECT_TRUE(root.isMember("meta"));

        // error must be null on success
        EXPECT_TRUE(root["error"].isNull());

        // data must have status ok
        EXPECT_EQ(root["data"]["status"].asString(), "ok");

        // meta must have request_id and timestamp
        EXPECT_TRUE(root["meta"].isMember("request_id"));
        EXPECT_TRUE(root["meta"].isMember("timestamp")); });
    }

    TEST(RouterTest, HealthHandlerBodyHasVersionField)
    {
        auto handler = corvus::gateway::health_handler();
        auto req = drogon::HttpRequest::newHttpRequest();
        req->setPath("/health");

        handler(req, [](const drogon::HttpResponsePtr &resp)
                {
        Json::Value  root;
        Json::Reader reader;
        reader.parse(std::string{resp->getBody()}, root);
        EXPECT_FALSE(root["data"]["version"].asString().empty()); });
    }

    // not_found_handler
    TEST(RouterTest, NotFoundHandlerReturns404)
    {
        auto handler = corvus::gateway::not_found_handler();
        auto req = drogon::HttpRequest::newHttpRequest();
        req->setPath("/v1/does-not-exist");

        bool called = false;
        handler(req, [&called](const drogon::HttpResponsePtr &resp)
                {
        called = true;
        EXPECT_EQ(resp->getStatusCode(), drogon::k404NotFound); });
        EXPECT_TRUE(called);
    }

    TEST(RouterTest, NotFoundHandlerBodyHasEnvelopeShape)
    {
        auto handler = corvus::gateway::not_found_handler();
        auto req = drogon::HttpRequest::newHttpRequest();
        req->setPath("/unknown");

        handler(req, [](const drogon::HttpResponsePtr &resp)
                {
        Json::Value  root;
        Json::Reader reader;
        reader.parse(std::string{resp->getBody()}, root);

        EXPECT_TRUE(root.isMember("data"));
        EXPECT_TRUE(root.isMember("error"));
        EXPECT_TRUE(root.isMember("meta"));
        EXPECT_TRUE(root["data"].isNull());
        EXPECT_FALSE(root["error"].isNull()); });
    }

    TEST(RouterTest, NotFoundHandlerErrorCodeIsNotFound)
    {
        auto handler = corvus::gateway::not_found_handler();
        auto req = drogon::HttpRequest::newHttpRequest();
        req->setPath("/unknown");

        handler(req, [](const drogon::HttpResponsePtr &resp)
                {
        Json::Value  root;
        Json::Reader reader;
        reader.parse(std::string{resp->getBody()}, root);
        EXPECT_EQ(root["error"]["code"].asString(), "NOT_FOUND"); });
    }

    TEST(RouterTest, NotFoundHandlerMessageContainsPath)
    {
        auto handler = corvus::gateway::not_found_handler();
        auto req = drogon::HttpRequest::newHttpRequest();
        req->setPath("/v1/missing-resource");

        handler(req, [](const drogon::HttpResponsePtr &resp)
                {
            Json::Value  root;
            Json::Reader reader;
            reader.parse(std::string{resp->getBody()}, root);
            const auto msg = root["error"]["message"].asString();
            EXPECT_NE(msg.find("/v1/missing-resource"), std::string::npos); });
    }
} // namespace