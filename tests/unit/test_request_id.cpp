#include <gtest/gtest.h>
#include "corvus/api/request_id.h"
#include <drogon/drogon.h>
#include <regex>
#include <set>

using namespace corvus::api;

TEST(RequestIdTest, GenerateUuidIsNonEmpty)
{
    EXPECT_FALSE(generate_uuid().empty());
}

TEST(RequestIdTest, GenerateUuidHasCorrectFormat)
{
    const auto uuid = generate_uuid();
    const std::regex uuid_re(
        "[0-9a-f]{8}-[0-9a-f]{4}-4[0-9a-f]{3}-[89ab][0-9a-f]{3}-[0-9a-f]{12}");
    EXPECT_TRUE(std::regex_match(uuid, uuid_re))
        << "UUID was: " << uuid;
}

TEST(RequestIdTest, GenerateUuidVersion4Bit)
{
    const auto uuid = generate_uuid();
    EXPECT_EQ(uuid[14], '4');
}

TEST(RequestIdTest, GenerateUuidVariantBits)
{
    const auto uuid = generate_uuid();
    const char variant = uuid[19];
    EXPECT_TRUE(variant == '8' || variant == '9' ||
                variant == 'a' || variant == 'b')
        << "Variant char was: " << variant;
}

TEST(RequestIdTest, GenerateUuidIsUnique)
{
    std::set<std::string> ids;
    for (int i = 0; i < 1000; ++i)
    {
        ids.insert(generate_uuid());
    }
    EXPECT_EQ(ids.size(), 1000u);
}

TEST(RequestIdTest, GenerateUuidLength)
{
    EXPECT_EQ(generate_uuid().size(), 36u);
}

TEST(RequestIdTest, GetRequestIdFromAttributes)
{
    auto req = drogon::HttpRequest::newHttpRequest();
    req->getAttributes()->insert(kRequestIdKey, std::string("test-id-123"));

    EXPECT_EQ(get_request_id(req), "test-id-123");
}

TEST(RequestIdTest, GetRequestIdFromXRequestIdHeader)
{
    auto req = drogon::HttpRequest::newHttpRequest();
    req->addHeader("X-Request-ID", "header-id-456");

    EXPECT_EQ(get_request_id(req), "header-id-456");
}

TEST(RequestIdTest, GetRequestIdAttributesTakesPrecedenceOverHeader)
{
    auto req = drogon::HttpRequest::newHttpRequest();
    req->getAttributes()->insert(kRequestIdKey, std::string("attr-id"));
    req->addHeader("X-Request-ID", "header-id");

    EXPECT_EQ(get_request_id(req), "attr-id");
}

TEST(RequestIdTest, GetRequestIdGeneratesUuidWhenNonePresent)
{
    auto req = drogon::HttpRequest::newHttpRequest();
    const auto id = get_request_id(req);

    EXPECT_FALSE(id.empty());
    EXPECT_EQ(id.size(), 36u);
}

TEST(RequestIdTest, GetRequestIdIsStableOnceSetInAttributes)
{
    auto req = drogon::HttpRequest::newHttpRequest();
    req->getAttributes()->insert(kRequestIdKey, std::string("stable-id"));

    EXPECT_EQ(get_request_id(req), "stable-id");
    EXPECT_EQ(get_request_id(req), "stable-id");
    EXPECT_EQ(get_request_id(req), "stable-id");
}

TEST(RequestIdTest, GenerateUuidIsDifferentAcrossThreads)
{
    constexpr std::size_t num_threads = 4;
    constexpr std::size_t per_thread = 250;

    std::vector<std::vector<std::string>> results(num_threads);
    std::vector<std::thread> threads;
    threads.reserve(num_threads);

    for (std::size_t t = 0; t < num_threads; ++t)
    {
        threads.emplace_back([&results, t]()
                             {
            for (std::size_t i = 0; i < per_thread; ++i)
                results[t].push_back(generate_uuid()); });
    }

    for (auto &th : threads)
        th.join();

    std::set<std::string> all;
    for (const auto &v : results)
        all.insert(v.begin(), v.end());

    EXPECT_EQ(all.size(), num_threads * per_thread);
}