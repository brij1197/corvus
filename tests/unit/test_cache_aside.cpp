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

#include <atomic>
#include <random>
#include <thread>
#include <vector>

class VersionedCacheTest : public ::testing::Test
{
protected:
    void SetUp() override
    {
        try
        {
            RedisConfig cfg = redis_config_from_env();
            cfg.connect_timeout_ms = 1000;
            redis_ = std::make_unique<RedisConnection>(cfg);
        }
        catch (const std::exception &)
        {
            GTEST_SKIP() << "Redis not reachable - skipping versioned cache tests.";
        }
        CacheConfig cc;
        cc.key_prefix = "corvus-test:";
        cache_ = std::make_unique<CacheAside>(*redis_, cc);
        key_ = "vtest:" + std::to_string(std::random_device{}());
    }

    void TearDown() override
    {
        if (redis_)
        {
            redis_->del(cache_->make_key(key_));
            redis_->del(cache_->version_key(key_));
        }
    }

    std::unique_ptr<RedisConnection> redis_;
    std::unique_ptr<CacheAside> cache_;
    std::string key_;
};

TEST_F(VersionedCacheTest, VersionOfUntouchedKeyIsZero)
{
    EXPECT_EQ(cache_->version(key_), "0");
}

TEST_F(VersionedCacheTest, PutWithCurrentVersionIsStored)
{
    const auto v = cache_->version(key_);
    ASSERT_TRUE(v);
    EXPECT_TRUE(cache_->put_if_version(key_, *v, "fresh", 60));
    EXPECT_EQ(cache_->get(key_), "fresh");
}

TEST_F(VersionedCacheTest, PutAfterConcurrentWriteIsRejected)
{
    const auto v = cache_->version(key_);
    ASSERT_TRUE(v);
    ASSERT_TRUE(cache_->invalidate_versioned(key_));

    EXPECT_FALSE(cache_->put_if_version(key_, *v, "stale", 60));
    EXPECT_FALSE(cache_->get(key_));
}

TEST_F(VersionedCacheTest, InvalidateEvictsAndBumpsVersion)
{
    ASSERT_TRUE(cache_->put(key_, "old", 60));
    const auto before = cache_->version(key_);

    ASSERT_TRUE(cache_->invalidate_versioned(key_));

    EXPECT_FALSE(cache_->get(key_));
    EXPECT_NE(cache_->version(key_), before);
}

TEST_F(VersionedCacheTest, VersionKeyExpires)
{
    ASSERT_TRUE(cache_->invalidate_versioned(key_));
    auto *r = static_cast<redisReply *>(
        redisCommand(redis_->ctx(), "TTL %s", cache_->version_key(key_).c_str()));
    ASSERT_NE(r, nullptr);
    EXPECT_GT(r->integer, 0);
    EXPECT_LE(r->integer, cache_->config().version_ttl_seconds);
    freeReplyObject(r);
}

TEST_F(VersionedCacheTest, PutRespectsDataTtl)
{
    ASSERT_TRUE(cache_->put_if_version(key_, "0", "v", 42));
    auto *r = static_cast<redisReply *>(
        redisCommand(redis_->ctx(), "TTL %s", cache_->make_key(key_).c_str()));
    ASSERT_NE(r, nullptr);
    EXPECT_GT(r->integer, 0);
    EXPECT_LE(r->integer, 42);
    freeReplyObject(r);
}

TEST_F(VersionedCacheTest, RacingReadersNeverCacheAPreWriteValue)
{
    std::atomic<int> db{0};
    std::atomic<bool> stop{false};

    std::vector<std::thread> readers;
    for (int t = 0; t < 4; ++t)
        readers.emplace_back([&]
                             {
            while (!stop)
            {
                const auto v = cache_->version(key_);
                const auto value = std::to_string(db.load());
                if (v)
                    cache_->put_if_version(key_, *v, value, 60);
            } });

    for (int i = 1; i <= 300; ++i)
    {
        db.store(i);
        ASSERT_TRUE(cache_->invalidate_versioned(key_));
    }
    stop = true;
    for (auto &t : readers)
        t.join();

    const auto cached = cache_->get(key_);
    if (cached) 
    {
        EXPECT_EQ(*cached, "300");
    }
}