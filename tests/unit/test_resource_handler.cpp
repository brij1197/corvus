#include <gtest/gtest.h>
#include "corvus/resources/resource_handler.h"
#include "corvus/auth/client_context.h"
#include "corvus/api/response.h"
#include <drogon/drogon.h>
#include <nlohmann/json.hpp>

using namespace corvus::resources;
using nlohmann::json;

namespace
{

    drogon::HttpRequestPtr make_request(const std::string &body,
                                        const std::string &client_id =
                                            "11111111-1111-1111-1111-111111111111")
    {
        auto req = drogon::HttpRequest::newHttpRequest();
        req->setMethod(drogon::Post);
        req->setPath("/v1/resources");
        req->setBody(body);
        if (!client_id.empty())
            req->getAttributes()->insert(corvus::auth::kClientIdKey, client_id);
        return req;
    }

    drogon::HttpResponsePtr invoke(const ResourceHandler &handler,
                                   const drogon::HttpRequestPtr &req)
    {
        drogon::HttpResponsePtr captured;
        handler(req, [&captured](const drogon::HttpResponsePtr &resp)
                { captured = resp; });
        return captured;
    }

    json body_of(const drogon::HttpResponsePtr &resp)
    {
        return json::parse(std::string{resp->getBody()});
    }

} // namespace

static ResourceHandler handler_without_service()
{
    return create_resource_handler(nullptr);
}

TEST(CreateResourceHandlerTest, MissingClientIdReturns500)
{
    auto handler = handler_without_service();
    auto req = make_request(R"({"kind":"server","name":"web-01"})", "");

    const auto resp = invoke(handler, req);
    ASSERT_NE(resp, nullptr);
    EXPECT_EQ(resp->getStatusCode(), drogon::k500InternalServerError);
}

TEST(CreateResourceHandlerTest, MissingClientIdDoesNotLeakDetail)
{
    auto handler = handler_without_service();
    auto req = make_request(R"({"kind":"server","name":"web-01"})", "");

    const auto body = body_of(invoke(handler, req));
    EXPECT_EQ(body["error"]["code"], "INTERNAL_ERROR");

    const std::string message = body["error"]["message"];
    EXPECT_EQ(message.find("client_id"), std::string::npos);
    EXPECT_FALSE(message.empty());
}

TEST(CreateResourceHandlerTest, MalformedJsonReturns400)
{
    auto handler = handler_without_service();
    auto req = make_request("{not valid json");

    const auto resp = invoke(handler, req);
    ASSERT_NE(resp, nullptr);
    EXPECT_EQ(resp->getStatusCode(), drogon::k400BadRequest);
}

TEST(CreateResourceHandlerTest, EmptyBodyReturns400)
{
    auto handler = handler_without_service();
    auto req = make_request("");

    const auto resp = invoke(handler, req);
    EXPECT_EQ(resp->getStatusCode(), drogon::k400BadRequest);
}

TEST(CreateResourceHandlerTest, MalformedJsonErrorCodeIsBadRequest)
{
    auto handler = handler_without_service();
    const auto body = body_of(invoke(handler, make_request("[[[")));
    EXPECT_EQ(body["error"]["code"], "BAD_REQUEST");
}

TEST(CreateResourceHandlerTest, MissingKindIsRejected)
{
    auto handler = handler_without_service();
    const auto resp = invoke(handler, make_request(R"({"name":"web-01"})"));
    EXPECT_EQ(resp->getStatusCode(), drogon::k422UnprocessableEntity);
}

TEST(CreateResourceHandlerTest, MissingNameIsRejected)
{
    auto handler = handler_without_service();
    const auto resp = invoke(handler, make_request(R"({"kind":"server"})"));
    EXPECT_EQ(resp->getStatusCode(), drogon::k422UnprocessableEntity);
}

TEST(CreateResourceHandlerTest, EmptyNameIsRejected)
{
    auto handler = handler_without_service();
    const auto resp =
        invoke(handler, make_request(R"({"kind":"server","name":""})"));
    EXPECT_EQ(resp->getStatusCode(), drogon::k422UnprocessableEntity);
}

TEST(CreateResourceHandlerTest, OversizedNameIsRejected)
{
    auto handler = handler_without_service();
    const std::string huge(kNameMaxLen + 1, 'a');
    const json body{{"kind", "server"}, {"name", huge}};

    const auto resp = invoke(handler, make_request(body.dump()));
    EXPECT_EQ(resp->getStatusCode(), drogon::k422UnprocessableEntity);
}

TEST(CreateResourceHandlerTest, NonObjectMetadataIsRejected)
{
    auto handler = handler_without_service();
    const auto resp = invoke(
        handler,
        make_request(R"({"kind":"server","name":"web-01","metadata":"nope"})"));
    EXPECT_EQ(resp->getStatusCode(), drogon::k422UnprocessableEntity);
}

TEST(CreateResourceHandlerTest, ValidationErrorCodeIsUnprocessableEntity)
{
    auto handler = handler_without_service();
    const auto body = body_of(invoke(handler, make_request(R"({"name":"x"})")));
    EXPECT_EQ(body["error"]["code"], "UNPROCESSABLE_ENTITY");
}

TEST(CreateResourceHandlerTest, ValidationErrorMessageIsActionable)
{
    auto handler = handler_without_service();
    const auto body = body_of(invoke(handler, make_request(R"({"name":"x"})")));

    const std::string message = body["error"]["message"];
    EXPECT_NE(message.find("kind"), std::string::npos);
}

TEST(CreateResourceHandlerTest, ErrorResponseHasEnvelopeShape)
{
    auto handler = handler_without_service();
    const auto body = body_of(invoke(handler, make_request("bad json")));

    EXPECT_TRUE(body.contains("data"));
    EXPECT_TRUE(body.contains("error"));
    EXPECT_TRUE(body.contains("meta"));
    EXPECT_TRUE(body["data"].is_null());
    EXPECT_TRUE(body["meta"].contains("request_id"));
}

TEST(CreateResourceHandlerTest, ResponseContentTypeIsJson)
{
    auto handler = handler_without_service();
    const auto resp = invoke(handler, make_request("bad json"));
    EXPECT_NE(std::string{resp->getHeader("content-type")}.find("application/json"),
              std::string::npos);
}