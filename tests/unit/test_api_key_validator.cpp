#include <gtest/gtest.h>
#include "corvus/auth/api_key_validator.h"
#include <hiredis/hiredis.h>
#include <cstdlib>
#include <string>
#include <type_traits>

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

namespace
{
    redisReply *run(const char *cmd)
    {
        redisContext *c = redisConnect(redis_host().c_str(), redis_port());
        if (!c || c->err)
        {
            if (c)
                redisFree(c);
            return nullptr;
        }
        auto *r = static_cast<redisReply *>(redisCommand(c, cmd));
        redisFree(c);
        return r;
    }

    class DenyAllKeys
    {
    public:
        DenyAllKeys()
        {
            auto *r = run("ACL SETUSER default resetkeys");
            active_ = r && r->type == REDIS_REPLY_STATUS;
            if (r)
                freeReplyObject(r);
        }
        ~DenyAllKeys()
        {
            if (auto *r = run("ACL SETUSER default allkeys"))
                freeReplyObject(r);
        }
        bool active() const { return active_; }

    private:
        bool active_{false};
    };
} // namespace

TEST_F(ApiKeyValidatorTest, RedisErrorReplyIsBackendErrorNotRejection)
{
    const std::string raw = seed_key("svc-acl");

    DenyAllKeys deny;
    if (!deny.active())
        GTEST_SKIP() << "Redis ACLs unavailable (needs Redis 6+)";

    EXPECT_THROW(validator_->validate(raw), corvus::auth::ApiKeyBackendError);
}

TEST(ApiKeyBackendErrorTest, IsNotAValidationError)
{
    static_assert(!std::is_base_of_v<corvus::auth::ApiKeyValidationError,
                                     corvus::auth::ApiKeyBackendError>);
    SUCCEED();
}

TEST_F(ApiKeyValidatorTest, ValidatesAgainAfterRedisRecovers)
{
    const std::string raw = seed_key("svc-recover");
    {
        DenyAllKeys deny;
        if (!deny.active())
            GTEST_SKIP() << "Redis ACLs unavailable (needs Redis 6+)";
        EXPECT_THROW(validator_->validate(raw), corvus::auth::ApiKeyBackendError);
    }
    EXPECT_EQ(validator_->validate(raw).client_id, "svc-recover");
}

namespace
{
    std::string seed_key_in_db(int db, const std::string &client_id)
    {
        const std::string raw_key = "test-key-db" + std::to_string(db) + "-" + client_id;
        const std::string rkey =
            "corvus:apikeys:" + corvus::auth::ApiKeyValidator::hash_key(raw_key);

        redisContext *c = redisConnect(redis_host().c_str(), redis_port());
        if (c && !c->err)
        {
            if (auto *r = static_cast<redisReply *>(redisCommand(c, "SELECT %d", db)))
                freeReplyObject(r);
            if (auto *r = static_cast<redisReply *>(redisCommand(
                    c, "HSET %s client_id %s scopes read", rkey.c_str(), client_id.c_str())))
                freeReplyObject(r);
        }
        if (c)
            redisFree(c);
        return raw_key;
    }

    corvus::db::RedisConfig local_config()
    {
        corvus::db::RedisConfig cfg;
        cfg.host = redis_host();
        cfg.port = redis_port();
        return cfg;
    }

    class RequirePassword
    {
    public:
        explicit RequirePassword(std::string password) : password_(std::move(password))
        {
            auto *r = run(("CONFIG SET requirepass " + password_).c_str());
            active_ = r && r->type == REDIS_REPLY_STATUS;
            if (r)
                freeReplyObject(r);
        }
        ~RequirePassword()
        {
            redisContext *c = redisConnect(redis_host().c_str(), redis_port());
            if (c && !c->err)
            {
                if (auto *r = static_cast<redisReply *>(
                        redisCommand(c, "AUTH %s", password_.c_str())))
                    freeReplyObject(r);
                if (auto *r = static_cast<redisReply *>(
                        redisCommand(c, "CONFIG SET requirepass %s", "")))
                    freeReplyObject(r);
            }
            if (c)
                redisFree(c);
        }
        bool active() const { return active_; }

    private:
        std::string password_;
        bool active_{false};
    };
} // namespace

TEST_F(ApiKeyValidatorTest, LooksUpKeysInTheConfiguredDatabase)
{
    const std::string raw = seed_key_in_db(5, "svc-db5");

    auto db5 = local_config();
    db5.db = 5;
    EXPECT_EQ(corvus::auth::ApiKeyValidator(db5).validate(raw).client_id, "svc-db5");

    EXPECT_THROW(corvus::auth::ApiKeyValidator(local_config()).validate(raw),
                 corvus::auth::ApiKeyValidationError);
}

TEST_F(ApiKeyValidatorTest, AuthenticatesWithTheConfiguredPassword)
{
    const std::string raw = seed_key("svc-auth");

    RequirePassword guard("corvus-unit-test-pw");
    if (!guard.active())
        GTEST_SKIP() << "could not enable requirepass on the test Redis";

    auto with_pw = local_config();
    with_pw.password = "corvus-unit-test-pw";
    EXPECT_EQ(corvus::auth::ApiKeyValidator(with_pw).validate(raw).client_id, "svc-auth");
}

TEST_F(ApiKeyValidatorTest, MissingPasswordIsABackendErrorNotARejection)
{
    const std::string raw = seed_key("svc-noauth");

    // Built before the password exists, as a misconfigured deployment would be.
    corvus::auth::ApiKeyValidator without_pw(local_config());

    RequirePassword guard("corvus-unit-test-pw");
    if (!guard.active())
        GTEST_SKIP() << "could not enable requirepass on the test Redis";

    EXPECT_THROW(without_pw.validate(raw), corvus::auth::ApiKeyBackendError);
}

TEST_F(ApiKeyValidatorTest, WrongPasswordFailsAtStartup)
{
    RequirePassword guard("corvus-unit-test-pw");
    if (!guard.active())
        GTEST_SKIP() << "could not enable requirepass on the test Redis";

    auto wrong = local_config();
    wrong.password = "not-the-password";
    EXPECT_THROW(corvus::auth::ApiKeyValidator{wrong}, corvus::auth::ApiKeyConfigError);
}

TEST(ApiKeyValidatorEnvTest, DefaultConstructorUsesSharedRedisEnv)
{
    if (!redis_available())
        GTEST_SKIP() << "Redis not reachable";

    ::setenv("CORVUS_REDIS_DB", "7", 1);
    corvus::auth::ApiKeyValidator v;
    ::unsetenv("CORVUS_REDIS_DB");

    EXPECT_EQ(v.config().db, 7);
    EXPECT_EQ(v.config().host, corvus::db::redis_config_from_env().host);
}