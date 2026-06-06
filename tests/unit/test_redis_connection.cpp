#include <gtest/gtest.h>
#include "corvus/db/redis_connection.h"
#include <cstdlib>

using namespace corvus::db;

TEST(RedisConfigErrorTest, IsStdRuntimeError)
{
    RedisConfigError err("bad config");
    EXPECT_STREQ(err.what(), "bad config");
    EXPECT_TRUE((std::is_base_of<std::runtime_error, RedisConfigError>::value));
}

TEST(RedisCommandErrorTest, IsStdRuntimeError)
{
    RedisCommandError err("command failed");
    EXPECT_STREQ(err.what(), "command failed");
    EXPECT_TRUE((std::is_base_of<std::runtime_error, RedisCommandError>::value));
}

TEST(RedisConfigTest, DefaultHostIsLocalhost)
{
    RedisConfig cfg;
    EXPECT_EQ(cfg.host, "localhost");
}

TEST(RedisConfigTest, DefaultPortIs6379)
{
    RedisConfig cfg;
    EXPECT_EQ(cfg.port, 6379);
}

TEST(RedisConfigTest, DefaultDbIsZero)
{
    RedisConfig cfg;
    EXPECT_EQ(cfg.db, 0);
}

TEST(RedisConfigTest, DefaultPasswordIsEmpty)
{
    RedisConfig cfg;
    EXPECT_TRUE(cfg.password.empty());
}

TEST(RedisConfigTest, DefaultConnectTimeoutIs3000ms)
{
    RedisConfig cfg;
    EXPECT_EQ(cfg.connect_timeout_ms, 3000);
}

TEST(RedisConfigTest, DefaultCommandTimeoutIs1000ms)
{
    RedisConfig cfg;
    EXPECT_EQ(cfg.command_timeout_ms, 1000);
}

TEST(RedisConnectionTest, ThrowsOnUnreachableHost)
{
    RedisConfig cfg;
    cfg.host = "127.0.0.1";
    cfg.port = 19999;
    cfg.connect_timeout_ms = 100;

    EXPECT_THROW(RedisConnection conn(cfg), RedisConfigError);
}

TEST(RedisReplyTest, NullReplyIsFalsy)
{
    RedisReply reply(nullptr);
    EXPECT_FALSE(static_cast<bool>(reply));
    EXPECT_TRUE(reply.is_nil());
}

TEST(RedisReplyTest, TypeConstantsHaveExpectedValues)
{
    EXPECT_EQ(REDIS_REPLY_STRING, 1);
    EXPECT_EQ(REDIS_REPLY_ARRAY, 2);
    EXPECT_EQ(REDIS_REPLY_INTEGER, 3);
    EXPECT_EQ(REDIS_REPLY_NIL, 4);
    EXPECT_EQ(REDIS_REPLY_STATUS, 5);
    EXPECT_EQ(REDIS_REPLY_ERROR, 6);
}

TEST(RedisConfigFromEnvTest, ReadsHostFromEnv)
{
    setenv("CORVUS_REDIS_HOST", "myredis", 1);
    const auto cfg = redis_config_from_env();
    unsetenv("CORVUS_REDIS_HOST");
    EXPECT_EQ(cfg.host, "myredis");
}

TEST(RedisConfigFromEnvTest, ReadsPortFromEnv)
{
    setenv("CORVUS_REDIS_PORT", "6380", 1);
    const auto cfg = redis_config_from_env();
    unsetenv("CORVUS_REDIS_PORT");
    EXPECT_EQ(cfg.port, 6380);
}

TEST(RedisConfigFromEnvTest, ReadsDbFromEnv)
{
    setenv("CORVUS_REDIS_DB", "2", 1);
    const auto cfg = redis_config_from_env();
    unsetenv("CORVUS_REDIS_DB");
    EXPECT_EQ(cfg.db, 2);
}

TEST(RedisConfigFromEnvTest, DefaultsWhenEnvNotSet)
{
    unsetenv("CORVUS_REDIS_HOST");
    unsetenv("CORVUS_REDIS_PORT");
    unsetenv("CORVUS_REDIS_DB");

    const auto cfg = redis_config_from_env();
    EXPECT_EQ(cfg.host, "localhost");
    EXPECT_EQ(cfg.port, 6379);
    EXPECT_EQ(cfg.db, 0);
}