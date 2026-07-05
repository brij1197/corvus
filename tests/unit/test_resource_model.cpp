#include <gtest/gtest.h>
#include "corvus/resources/resource.h"
#include <nlohmann/json.hpp>

using namespace corvus::resources;
using nlohmann::json;

TEST(ResourceToJsonTest, SerializesAllFields)
{
    Resource r;
    r.id = "11111111-1111-1111-1111-111111111111";
    r.client_id = "22222222-2222-2222-2222-222222222222";
    r.kind = "server";
    r.name = "web-01";
    r.status = "active";
    r.metadata = {{"region", "us-east-1"}, {"size", "m5.large"}};
    r.created_at = "2026-06-28T12:00:00Z";
    r.updated_at = "2026-06-28T12:30:00Z";

    const auto j = to_json(r);
    EXPECT_EQ(j["id"], r.id);
    EXPECT_EQ(j["client_id"], r.client_id);
    EXPECT_EQ(j["kind"], "server");
    EXPECT_EQ(j["name"], "web-01");
    EXPECT_EQ(j["status"], "active");
    EXPECT_EQ(j["metadata"]["region"], "us-east-1");
    EXPECT_EQ(j["created_at"], r.created_at);
    EXPECT_EQ(j["updated_at"], r.updated_at);
}

TEST(ResourceToJsonTest, EmptyMetadataSerializesAsEmptyObject)
{
    Resource r;
    r.metadata = json::object();

    const auto j = to_json(r);
    EXPECT_TRUE(j["metadata"].is_object());
    EXPECT_TRUE(j["metadata"].empty());
}

TEST(ResourceValidationErrorTest, IsStdRuntimeError)
{
    ResourceValidationError err("bad input");
    EXPECT_STREQ(err.what(), "bad input");
    EXPECT_TRUE((std::is_base_of<std::runtime_error,
                                 ResourceValidationError>::value));
}

TEST(ParseCreateRequestTest, AcceptsMinimalValidBody)
{
    const auto body = json{{"kind", "server"}, {"name", "web-01"}};
    const auto req = parse_create_request(body);

    EXPECT_EQ(req.kind, "server");
    EXPECT_EQ(req.name, "web-01");
    EXPECT_FALSE(req.status.has_value());
    EXPECT_FALSE(req.metadata.has_value());
}

TEST(ParseCreateRequestTest, AcceptsAllOptionalFields)
{
    const auto body = json{
        {"kind", "server"},
        {"name", "web-01"},
        {"status", "active"},
        {"metadata", {{"region", "us-east-1"}}},
    };
    const auto req = parse_create_request(body);

    EXPECT_EQ(req.kind, "server");
    EXPECT_EQ(req.name, "web-01");
    ASSERT_TRUE(req.status.has_value());
    EXPECT_EQ(*req.status, "active");
    ASSERT_TRUE(req.metadata.has_value());
    EXPECT_EQ((*req.metadata)["region"], "us-east-1");
}

TEST(ParseCreateRequestTest, RejectsNonObjectBody)
{
    EXPECT_THROW(parse_create_request(json::array()),
                 ResourceValidationError);
    EXPECT_THROW(parse_create_request(json("not an object")),
                 ResourceValidationError);
}

TEST(ParseCreateRequestTest, RejectsMissingKind)
{
    const auto body = json{{"name", "web-01"}};
    EXPECT_THROW(parse_create_request(body), ResourceValidationError);
}

TEST(ParseCreateRequestTest, RejectsMissingName)
{
    const auto body = json{{"kind", "server"}};
    EXPECT_THROW(parse_create_request(body), ResourceValidationError);
}

TEST(ParseCreateRequestTest, RejectsEmptyKind)
{
    const auto body = json{{"kind", ""}, {"name", "web-01"}};
    EXPECT_THROW(parse_create_request(body), ResourceValidationError);
}

TEST(ParseCreateRequestTest, RejectsEmptyName)
{
    const auto body = json{{"kind", "server"}, {"name", ""}};
    EXPECT_THROW(parse_create_request(body), ResourceValidationError);
}

TEST(ParseCreateRequestTest, RejectsOversizedName)
{
    const std::string huge_name(kNameMaxLen + 1, 'a');
    const auto body = json{{"kind", "server"}, {"name", huge_name}};
    EXPECT_THROW(parse_create_request(body), ResourceValidationError);
}

TEST(ParseCreateRequestTest, RejectsOversizedKind)
{
    const std::string huge_kind(kKindMaxLen + 1, 'a');
    const auto body = json{{"kind", huge_kind}, {"name", "web-01"}};
    EXPECT_THROW(parse_create_request(body), ResourceValidationError);
}

TEST(ParseCreateRequestTest, RejectsNonStringKind)
{
    const auto body = json{{"kind", 42}, {"name", "web-01"}};
    EXPECT_THROW(parse_create_request(body), ResourceValidationError);
}

TEST(ParseCreateRequestTest, RejectsNonObjectMetadata)
{
    const auto body = json{
        {"kind", "server"},
        {"name", "web-01"},
        {"metadata", "not an object"},
    };
    EXPECT_THROW(parse_create_request(body), ResourceValidationError);
}

TEST(ParseCreateRequestTest, RejectsOversizedMetadata)
{
    json big = json::object();
    const std::string huge(kMetadataMaxBytes, 'x');
    big["payload"] = huge;

    const auto body = json{
        {"kind", "server"},
        {"name", "web-01"},
        {"metadata", big},
    };
    EXPECT_THROW(parse_create_request(body), ResourceValidationError);
}

TEST(ParseCreateRequestTest, AcceptsNameAtBoundary)
{
    const std::string boundary_name(kNameMaxLen, 'a');
    const auto body = json{{"kind", "server"}, {"name", boundary_name}};
    EXPECT_NO_THROW(parse_create_request(body));
}

TEST(ParseCreateRequestTest, NullOptionalFieldsAreTreatedAsAbsent)
{
    const auto body = json{
        {"kind", "server"},
        {"name", "web-01"},
        {"status", nullptr},
        {"metadata", nullptr},
    };
    const auto req = parse_create_request(body);
    EXPECT_FALSE(req.status.has_value());
    EXPECT_FALSE(req.metadata.has_value());
}

TEST(ParseUpdateRequestTest, AcceptsSingleField)
{
    const auto body = json{{"status", "decommissioned"}};
    const auto req = parse_update_request(body);
    EXPECT_FALSE(req.name.has_value());
    ASSERT_TRUE(req.status.has_value());
    EXPECT_EQ(*req.status, "decommissioned");
    EXPECT_FALSE(req.metadata.has_value());
}

TEST(ParseUpdateRequestTest, AcceptsMultipleFields)
{
    const auto body = json{
        {"name", "web-01-renamed"},
        {"metadata", {{"foo", "bar"}}},
    };
    const auto req = parse_update_request(body);
    ASSERT_TRUE(req.name.has_value());
    EXPECT_EQ(*req.name, "web-01-renamed");
    ASSERT_TRUE(req.metadata.has_value());
    EXPECT_EQ((*req.metadata)["foo"], "bar");
}

TEST(ParseUpdateRequestTest, RejectsEmptyBody)
{
    EXPECT_THROW(parse_update_request(json::object()),
                 ResourceValidationError);
}

TEST(ParseUpdateRequestTest, RejectsBodyWithUnknownFieldsOnly)
{
    const auto body = json{{"unknown_field", "value"}};
    EXPECT_THROW(parse_update_request(body), ResourceValidationError);
}

TEST(ParseUpdateRequestTest, RejectsEmptyNameOnPatch)
{
    const auto body = json{{"name", ""}};
    EXPECT_THROW(parse_update_request(body), ResourceValidationError);
}

TEST(ParseUpdateRequestTest, RejectsOversizedNameOnPatch)
{
    const std::string huge_name(kNameMaxLen + 1, 'a');
    const auto body = json{{"name", huge_name}};
    EXPECT_THROW(parse_update_request(body), ResourceValidationError);
}

TEST(ParseUpdateRequestTest, RejectsNonObjectMetadataOnPatch)
{
    const auto body = json{{"metadata", "string"}};
    EXPECT_THROW(parse_update_request(body), ResourceValidationError);
}

TEST(ParseUpdateRequestTest, NullFieldsTreatedAsAbsent)
{
    const auto body = json{
        {"name", nullptr},
        {"status", nullptr},
        {"metadata", nullptr},
    };
    EXPECT_THROW(parse_update_request(body), ResourceValidationError);
}