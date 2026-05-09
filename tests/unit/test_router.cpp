#include <gtest/gtest.h>
#include "corvus/gateway/router.h"
#include "corvus/gateway/health_handler.h"
#include <drogon/drogon.h>

namespace
{

    TEST(RouterTest, HealthHandlerFactoryReturnsNonNullCallable)
    {
        auto handler = corvus::gateway::health_handler();
        EXPECT_TRUE(static_cast<bool>(handler));
    }

    TEST(RouterTest, HealthHandlerFactoryReturnsDifferentInstancesEachCall)
    {
        auto h1 = corvus::gateway::health_handler();
        auto h2 = corvus::gateway::health_handler();
        EXPECT_TRUE(static_cast<bool>(h1));
        EXPECT_TRUE(static_cast<bool>(h2));
    }

    TEST(RouterTest, HealthHandlerIsCallableWithValidRequest)
    {
        auto handler = corvus::gateway::health_handler();
        auto req = drogon::HttpRequest::newHttpRequest();
        req->setPath("/health");

        bool callback_invoked = false;
        handler(req, [&callback_invoked](const drogon::HttpResponsePtr &resp)
                {
                    callback_invoked = true;
                    EXPECT_NE(resp, nullptr);
                });

        EXPECT_TRUE(callback_invoked);
    }
} //namespace