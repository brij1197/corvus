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

namespace
{
    drogon::HttpRequestPtr make_get_request(
        const std::string &client_id = "11111111-1111-1111-1111-111111111111")
    {
        auto req = drogon::HttpRequest::newHttpRequest();
        req->setMethod(drogon::Get);
        req->setPath("/v1/resources/x");
        if (!client_id.empty())
            req->getAttributes()->insert(corvus::auth::kClientIdKey, client_id);
        return req;
    }

    drogon::HttpResponsePtr invoke_get(const ResourceIdHandler &handler,
                                       const drogon::HttpRequestPtr &req,
                                       const std::string &id)
    {
        drogon::HttpResponsePtr captured;
        handler(req, [&captured](const drogon::HttpResponsePtr &resp)
                { captured = resp; }, id);
        return captured;
    }
} // namespace

TEST(GetResourceHandlerTest, MissingClientIdReturns500)
{
    auto handler = get_resource_handler(nullptr);
    const auto resp = invoke_get(
        handler, make_get_request(""), "11111111-1111-1111-1111-111111111111");

    ASSERT_NE(resp, nullptr);
    EXPECT_EQ(resp->getStatusCode(), drogon::k500InternalServerError);
}

TEST(GetResourceHandlerTest, MalformedUuidReturns404)
{
    auto handler = get_resource_handler(nullptr);
    const auto resp = invoke_get(handler, make_get_request(), "not-a-uuid");

    ASSERT_NE(resp, nullptr);
    EXPECT_EQ(resp->getStatusCode(), drogon::k404NotFound);
}

TEST(GetResourceHandlerTest, EmptyIdReturns404)
{
    auto handler = get_resource_handler(nullptr);
    EXPECT_EQ(invoke_get(handler, make_get_request(), "")->getStatusCode(),
              drogon::k404NotFound);
}

TEST(GetResourceHandlerTest, WrongLengthUuidReturns404)
{
    auto handler = get_resource_handler(nullptr);
    // One character short of a valid UUID.
    EXPECT_EQ(invoke_get(handler, make_get_request(),
                         "11111111-1111-1111-1111-11111111111")
                  ->getStatusCode(),
              drogon::k404NotFound);
}

TEST(GetResourceHandlerTest, NonHexCharactersReturn404)
{
    auto handler = get_resource_handler(nullptr);
    EXPECT_EQ(invoke_get(handler, make_get_request(),
                         "zzzzzzzz-1111-1111-1111-111111111111")
                  ->getStatusCode(),
              drogon::k404NotFound);
}

TEST(GetResourceHandlerTest, MisplacedHyphensReturn404)
{
    auto handler = get_resource_handler(nullptr);
    EXPECT_EQ(invoke_get(handler, make_get_request(),
                         "111111111-111-1111-1111-111111111111")
                  ->getStatusCode(),
              drogon::k404NotFound);
}

TEST(GetResourceHandlerTest, SqlInjectionAttemptReturns404)
{
    // Never reaches the database - rejected by the UUID shape check.
    auto handler = get_resource_handler(nullptr);
    EXPECT_EQ(invoke_get(handler, make_get_request(),
                         "' OR 1=1 --")
                  ->getStatusCode(),
              drogon::k404NotFound);
}

TEST(GetResourceHandlerTest, NotFoundErrorCodeIsResourceNotFound)
{
    auto handler = get_resource_handler(nullptr);
    const auto body = body_of(invoke_get(handler, make_get_request(), "bad"));
    EXPECT_EQ(body["error"]["code"], "RESOURCE_NOT_FOUND");
}

TEST(GetResourceHandlerTest, NotFoundResponseHasEnvelopeShape)
{
    auto handler = get_resource_handler(nullptr);
    const auto body = body_of(invoke_get(handler, make_get_request(), "bad"));

    EXPECT_TRUE(body["data"].is_null());
    EXPECT_TRUE(body.contains("error"));
    EXPECT_TRUE(body["meta"].contains("request_id"));
}

TEST(ParseLimitParamTest, AcceptsPlainInteger)
{
    const auto result = parse_limit_param("50");
    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(*result, 50);
}

TEST(ParseLimitParamTest, AcceptsOne)
{
    const auto result = parse_limit_param("1");
    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(*result, 1);
}

TEST(ParseLimitParamTest, AcceptsValueAboveClampCeiling)
{
    // Clamping to 1..200 is the repository's job — the parser only decides
    // whether the value is an integer at all.
    const auto result = parse_limit_param("9999");
    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(*result, 9999);
}

TEST(ParseLimitParamTest, AcceptsNegative)
{
    // Also clamped downstream rather than rejected here.
    const auto result = parse_limit_param("-5");
    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(*result, -5);
}

TEST(ParseLimitParamTest, RejectsNonNumeric)
{
    EXPECT_FALSE(parse_limit_param("abc").has_value());
}

TEST(ParseLimitParamTest, RejectsTrailingGarbage)
{
    // std::stoi("50x") alone would return 50 and silently ignore the "x".
    EXPECT_FALSE(parse_limit_param("50x").has_value());
}

TEST(ParseLimitParamTest, RejectsLeadingGarbage)
{
    EXPECT_FALSE(parse_limit_param("x50").has_value());
}

TEST(ParseLimitParamTest, RejectsEmptyString)
{
    EXPECT_FALSE(parse_limit_param("").has_value());
}

TEST(ParseLimitParamTest, RejectsFloat)
{
    EXPECT_FALSE(parse_limit_param("1.5").has_value());
}

TEST(ParseLimitParamTest, RejectsOutOfRangeValue)
{
    // std::stoi throws out_of_range rather than returning a truncated value.
    EXPECT_FALSE(parse_limit_param("99999999999999999999").has_value());
}

TEST(ListResourcesHandlerTest, MissingClientIdReturns500)
{
    // The client_id guard fires before the service is dereferenced, so a
    // null service is safe here.
    auto handler = list_resources_handler(nullptr);

    auto req = drogon::HttpRequest::newHttpRequest();
    req->setMethod(drogon::Get);
    req->setPath("/v1/resources");

    const auto resp = invoke(handler, req);
    ASSERT_NE(resp, nullptr);
    EXPECT_EQ(resp->getStatusCode(), drogon::k500InternalServerError);
}