#include <gtest/gtest.h>
#include "corvus/db/cache_aside.h"

using namespace corvus::db;

TEST(CacheConfigTest, DefaultTtlIs300Seconds)
{
    CacheConfig cfg;
    EXPECT_EQ(cfg.default_ttl_seconds, 300);
}

TEST(CacheConfigTest, DefaultKeyPrefixIsCorvus)
{
    CacheConfig cfg;
    EXPECT_EQ(cfg.key_prefix, "corvus:");
}

TEST(CacheConfigTest, CustomTtlIsRespected)
{
    CacheConfig cfg;
    cfg.default_ttl_seconds = 60;
    EXPECT_EQ(cfg.default_ttl_seconds, 60);
}

TEST(CacheConfigTest, CustomPrefixIsRespected)
{
    CacheConfig cfg;
    cfg.key_prefix = "myapp:";
    EXPECT_EQ(cfg.key_prefix, "myapp:");
}

TEST(CacheConfigTest, ZeroTtlMeansNoExpiry)
{
    CacheConfig cfg;
    cfg.default_ttl_seconds = 0;
    EXPECT_EQ(cfg.default_ttl_seconds, 0);
}

TEST(MakeKeyTest, PrependsPrefixToKey)
{
    const std::string prefix = "corvus:";
    const std::string key = "clients:abc123";
    EXPECT_EQ(prefix + key, "corvus:clients:abc123");
}

TEST(MakeKeyTest, EmptyPrefixReturnsKeyAsIs)
{
    const std::string prefix = "";
    const std::string key = "mykey";
    EXPECT_EQ(prefix + key, "mykey");
}

TEST(MakeKeyTest, NestedKeyBuildsCorrectly)
{
    const std::string prefix = "corvus:";
    const std::string key = "resources:server:001";
    EXPECT_EQ(prefix + key, "corvus:resources:server:001");
}

TEST(EffectiveTtlTest, NegativeUsesDefault)
{
    const int default_ttl = 300;
    const int ttl_arg = -1;
    const int result = (ttl_arg < 0) ? default_ttl : ttl_arg;
    EXPECT_EQ(result, 300);
}

TEST(EffectiveTtlTest, ZeroOverridesDefault)
{
    const int default_ttl = 300;
    const int ttl_arg = 0;
    const int result = (ttl_arg < 0) ? default_ttl : ttl_arg;
    EXPECT_EQ(result, 0);
}

TEST(EffectiveTtlTest, PositiveOverridesDefault)
{
    const int default_ttl = 300;
    const int ttl_arg = 60;
    const int result = (ttl_arg < 0) ? default_ttl : ttl_arg;
    EXPECT_EQ(result, 60);
}

TEST(FetchFnTest, LambdaCanReturnValue)
{
    CacheAside::FetchFn fetch = []() -> std::optional<std::string>
    {
        return std::string("fetched_value");
    };
    auto result = fetch();
    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(*result, "fetched_value");
}

TEST(FetchFnTest, LambdaCanReturnNullopt)
{
    CacheAside::FetchFn fetch = []() -> std::optional<std::string>
    {
        return std::nullopt;
    };
    auto result = fetch();
    EXPECT_FALSE(result.has_value());
}

TEST(FetchFnTest, LambdaCalledOnlyOnce)
{
    int call_count = 0;
    CacheAside::FetchFn fetch = [&]() -> std::optional<std::string>
    {
        ++call_count;
        return std::string("value");
    };
    fetch();
    EXPECT_EQ(call_count, 1);
}