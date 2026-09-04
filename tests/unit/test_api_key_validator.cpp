#include <gtest/gtest.h>
#include "corvus/auth/api_key_validator.h"
#include <hiredis/hiredis.h>
#include <cstdlib>
#include <string>

static std::string redis_host()
{
    const char *h = std::getenv("CORVUS_REDIS_HOST");
    return (h && *h) ? h : "localhost";
}

static int redis_port()
{
    const char *p = std::getenv("CORVUS_REDIS_PORT");
    return (p && *p) ? std::stoi(p) : 6379;
}

static bool redis_available()
{
    try
    {
        corvus::auth::ApiKeyValidator v{redis_host(), redis_port()};
        return true;
    }
    catch (...)
    {
        return false;
    }
}

static std::string seed_key(const std::string &client_id,
                            const std::string &scopes = "read")
{
    const std::string raw_key = "test-key-" + client_id;
    const std::string hash = corvus::auth::ApiKeyValidator::hash_key(raw_key);
    const std::string rkey = "corvus:apikeys:" + hash;

    redisContext *c = redisConnect(redis_host().c_str(), redis_port());
    if (c && !c->err)
    {
        auto *r = static_cast<redisReply *>(
            redisCommand(c, "HSET %s client_id %s scopes %s",
                         rkey.c_str(), client_id.c_str(), scopes.c_str()));
        if (r)
            freeReplyObject(r);
    }
    if (c)
        redisFree(c);

    return raw_key;
}

class ApiKeyValidatorTest : public ::testing::Test
{
protected:
    void SetUp() override
    {
        if (!redis_available())
        {
            GTEST_SKIP() << "Redis not reachable at "
                         << redis_host() << ":" << redis_port()
                         << " - skipping API key tests.";
        }
        validator_ = std::make_shared<corvus::auth::ApiKeyValidator>(
            redis_host(), redis_port());
    }

    std::shared_ptr<corvus::auth::ApiKeyValidator> validator_;
};

TEST(ApiKeyHashTest, ProducesSha256Hex)
{
    EXPECT_EQ(
        corvus::auth::ApiKeyValidator::hash_key("abc"),
        "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad");
}

TEST(ApiKeyHashTest, DifferentInputsDifferentHashes)
{
    EXPECT_NE(corvus::auth::ApiKeyValidator::hash_key("key-a"),
              corvus::auth::ApiKeyValidator::hash_key("key-b"));
}

TEST(ApiKeyValidatorConstructTest, ThrowsOnUnreachableRedis)
{
    EXPECT_THROW(
        corvus::auth::ApiKeyValidator("127.0.0.1", 19999),
        corvus::auth::ApiKeyConfigError);
}

TEST_F(ApiKeyValidatorTest, RejectsEmptyKey)
{
    EXPECT_THROW(validator_->validate(""),
                 corvus::auth::ApiKeyValidationError);
}

TEST_F(ApiKeyValidatorTest, RejectsUnknownKey)
{
    EXPECT_THROW(validator_->validate("unknown-key-never-seeded"),
                 corvus::auth::ApiKeyValidationError);
}

TEST_F(ApiKeyValidatorTest, AcceptsValidKey)
{
    const std::string raw = seed_key("svc-payments", "read write");
    auto info = validator_->validate(raw);
    EXPECT_EQ(info.client_id, "svc-payments");
    EXPECT_EQ(info.scopes, "read write");
}

TEST_F(ApiKeyValidatorTest, HashIsDeterministic)
{
    const std::string raw = seed_key("svc-idempotent");
    EXPECT_NO_THROW(validator_->validate(raw));
    EXPECT_NO_THROW(validator_->validate(raw));
}