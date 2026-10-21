#include <gtest/gtest.h>
#include "corvus/auth/api_key_validator.h"
#include <hiredis/hiredis.h>
#include <cstdlib>
#include <string>
#include <type_traits>
#include <atomic>
#include <chrono>
#include <thread>
#include <unordered_map>
#include <vector>

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

    redisReply *run_authed(const std::string &password, const char *cmd)
    {
        redisContext *c = redisConnect(redis_host().c_str(), redis_port());
        redisReply *r = nullptr;
        if (c && !c->err)
        {
            if (auto *a = static_cast<redisReply *>(redisCommand(c, "AUTH %s", password.c_str())))
                freeReplyObject(a);
            r = static_cast<redisReply *>(redisCommand(c, cmd));
        }
        if (c)
            redisFree(c);
        return r;
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

    if (auto *r = run_authed("corvus-unit-test-pw", "CLIENT KILL TYPE normal SKIPME no"))
        freeReplyObject(r);

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

namespace
{
    long long redis_ttl(const std::string &key)
    {
        redisContext *c = redisConnect(redis_host().c_str(), redis_port());
        long long ttl = -3;
        if (c && !c->err)
            if (auto *r = static_cast<redisReply *>(redisCommand(c, "TTL %s", key.c_str())))
            {
                ttl = r->integer;
                freeReplyObject(r);
            }
        if (c)
            redisFree(c);
        return ttl;
    }

    void redis_del(const std::string &key)
    {
        if (auto *r = run(("DEL " + key).c_str()))
            freeReplyObject(r);
    }

    std::string cache_key_for(const std::string &raw)
    {
        return "corvus:apikeys:" + corvus::auth::ApiKeyValidator::hash_key(raw);
    }

    struct FakeKeyStore
    {
        std::unordered_map<std::string, corvus::auth::ApiKeyRecord> rows;
        std::atomic<int> calls{0};
        bool down{false};

        corvus::auth::ApiKeyLookup lookup()
        {
            return [this](const std::string &hash) -> std::optional<corvus::auth::ApiKeyRecord>
            {
                ++calls;
                if (down)
                    throw corvus::auth::ApiKeyBackendError("db down");
                const auto it = rows.find(hash);
                if (it == rows.end())
                    return std::nullopt;
                return it->second;
            };
        }

        void add(const std::string &raw, corvus::auth::ApiKeyRecord rec)
        {
            rows[corvus::auth::ApiKeyValidator::hash_key(raw)] = std::move(rec);
        }
    };

    std::string unique_raw(const char *tag)
    {
        return std::string("pg-mode-") + tag + "-" +
               std::to_string(std::chrono::steady_clock::now().time_since_epoch().count());
    }
} // namespace

TEST_F(ApiKeyValidatorTest, KeyKnownOnlyToPostgresValidatesAndIsCached)
{
    FakeKeyStore db;
    const auto raw = unique_raw("dbonly");
    db.add(raw, {"client-pg", "read write", std::nullopt});
    corvus::auth::ApiKeyValidator v(local_config(), db.lookup());

    EXPECT_EQ(v.validate(raw).client_id, "client-pg");
    EXPECT_EQ(db.calls, 1);

    const auto ttl = redis_ttl(cache_key_for(raw));
    EXPECT_GT(ttl, 0);
    EXPECT_LE(ttl, corvus::auth::ApiKeyValidator::kCacheTtl.count());

    EXPECT_EQ(v.validate(raw).scopes, "read write");
    EXPECT_EQ(db.calls, 1) << "second call should be served from the cache";
    redis_del(cache_key_for(raw));
}

TEST_F(ApiKeyValidatorTest, KeyUnknownToPostgresIsRejected)
{
    FakeKeyStore db;
    corvus::auth::ApiKeyValidator v(local_config(), db.lookup());
    EXPECT_THROW(v.validate(unique_raw("unknown")), corvus::auth::ApiKeyValidationError);
}

TEST_F(ApiKeyValidatorTest, RevocationTakesEffectOnceTheCacheEntryExpires)
{
    FakeKeyStore db;
    const auto raw = unique_raw("revoke");
    db.add(raw, {"client-pg", "read", std::nullopt});
    corvus::auth::ApiKeyValidator v(local_config(), db.lookup());
    ASSERT_NO_THROW(v.validate(raw));

    db.rows.clear();
    redis_del(cache_key_for(raw));

    EXPECT_THROW(v.validate(raw), corvus::auth::ApiKeyValidationError);
}

TEST_F(ApiKeyValidatorTest, CacheNeverOutlivesTheKeysExpiry)
{
    FakeKeyStore db;
    const auto raw = unique_raw("expiring");
    db.add(raw, {"client-pg", "read", std::chrono::system_clock::now() + std::chrono::seconds(5)});
    corvus::auth::ApiKeyValidator v(local_config(), db.lookup());

    ASSERT_NO_THROW(v.validate(raw));
    EXPECT_LE(redis_ttl(cache_key_for(raw)), 5);
    redis_del(cache_key_for(raw));
}

TEST_F(ApiKeyValidatorTest, HandWrittenRedisEntryCannotBypassPostgres)
{
    FakeKeyStore db;
    const std::string raw = seed_key("svc-handwritten");
    corvus::auth::ApiKeyValidator v(local_config(), db.lookup());

    EXPECT_THROW(v.validate(raw), corvus::auth::ApiKeyValidationError);
    EXPECT_EQ(db.calls, 1);
}

TEST_F(ApiKeyValidatorTest, RedisOutageFallsBackToPostgres)
{
    FakeKeyStore db;
    const auto raw = unique_raw("redis-down");
    db.add(raw, {"client-pg", "read", std::nullopt});
    corvus::auth::ApiKeyValidator v(local_config(), db.lookup());

    DenyAllKeys deny;
    if (!deny.active())
        GTEST_SKIP() << "Redis ACLs unavailable (needs Redis 6+)";

    EXPECT_EQ(v.validate(raw).client_id, "client-pg");
}

TEST_F(ApiKeyValidatorTest, PostgresOutageIsABackendError)
{
    FakeKeyStore db;
    db.down = true;
    corvus::auth::ApiKeyValidator v(local_config(), db.lookup());
    EXPECT_THROW(v.validate(unique_raw("db-down")), corvus::auth::ApiKeyBackendError);
}

TEST_F(ApiKeyValidatorTest, ReusesRedisConnections)
{
    const std::string raw = seed_key("svc-reuse");
    corvus::auth::ApiKeyValidator v(local_config());

    for (int i = 0; i < 20; ++i)
        ASSERT_NO_THROW(v.validate(raw));

    EXPECT_EQ(v.idle_connections(), 1u) << "sequential calls should share one connection";
}

TEST_F(ApiKeyValidatorTest, ConcurrentValidationsAreSafe)
{
    const std::string raw = seed_key("svc-concurrent");
    corvus::auth::ApiKeyValidator v(local_config());

    std::atomic<int> failures{0};
    std::vector<std::thread> threads;
    for (int t = 0; t < 8; ++t)
        threads.emplace_back([&]
                             {
            for (int i = 0; i < 50; ++i)
                try { if (v.validate(raw).client_id != "svc-concurrent") ++failures; }
                catch (...) { ++failures; } });
    for (auto &t : threads)
        t.join();

    EXPECT_EQ(failures, 0);
    EXPECT_LE(v.idle_connections(), 8u);
}

TEST_F(ApiKeyValidatorTest, RecoversWhenParkedConnectionsDie)
{
    const std::string raw = seed_key("svc-restart");
    corvus::auth::ApiKeyValidator v(local_config());
    ASSERT_NO_THROW(v.validate(raw));

    if (auto *r = run("CLIENT KILL TYPE normal SKIPME no"))
        freeReplyObject(r);

    EXPECT_EQ(v.validate(raw).client_id, "svc-restart");
}