#include <gtest/gtest.h>
#include "corvus/auth/pg_api_key_lookup.h"
#include "corvus/db/pg_pool.h"
#include <pqxx/pqxx>
#include <chrono>
#include <random>

using namespace corvus;

class PgApiKeyLookupTest : public ::testing::Test
{
protected:
    void SetUp() override
    {
        try
        {
            db::PoolConfig cfg;
            cfg.connection_string = db::connection_string_from_env();
            cfg.min_connections = 1;
            cfg.max_connections = 2;
            pool_ = std::make_shared<db::PgPool>(cfg);
        }
        catch (const std::exception &)
        {
            GTEST_SKIP() << "Postgres not reachable - skipping API key lookup tests.";
        }

        auto conn = pool_->acquire();
        pqxx::work txn(conn.get());
        const auto rows = txn.exec_params(
            "INSERT INTO clients (name) VALUES ($1) RETURNING id::text",
            "lookup-test-" + std::to_string(std::random_device{}()));
        client_id_ = rows[0][0].as<std::string>();
        txn.commit();
        lookup_ = auth::make_pg_api_key_lookup(pool_);
    }

    void TearDown() override
    {
        if (!pool_ || client_id_.empty())
            return;
        auto conn = pool_->acquire();
        pqxx::work txn(conn.get());
        txn.exec_params("DELETE FROM clients WHERE id = $1", client_id_);
        txn.commit();
    }

    std::string insert_key(const std::string &scopes, bool active, const std::string &expires)
    {
        const std::string hash = auth::ApiKeyValidator::hash_key(
            "lookup-key-" + std::to_string(std::random_device{}()));
        auto conn = pool_->acquire();
        pqxx::work txn(conn.get());
        txn.exec_params(
            "INSERT INTO api_keys (client_id, key_hash, scopes, is_active, expires_at) "
            "VALUES ($1, $2, $3, $4, " +
                expires + ")",
            client_id_, hash, scopes, active);
        txn.commit();
        return hash;
    }

    std::shared_ptr<db::PgPool> pool_;
    std::string client_id_;
    auth::ApiKeyLookup lookup_;
};

TEST_F(PgApiKeyLookupTest, FindsActiveKey)
{
    const auto rec = lookup_(insert_key("read,write", true, "NULL"));
    ASSERT_TRUE(rec);
    EXPECT_EQ(rec->client_id, client_id_);
    EXPECT_EQ(rec->scopes, "read,write");
    EXPECT_FALSE(rec->expires_at);
}

TEST_F(PgApiKeyLookupTest, UnknownKeyIsNotFound)
{
    EXPECT_FALSE(lookup_(auth::ApiKeyValidator::hash_key("never-issued")));
}

TEST_F(PgApiKeyLookupTest, InactiveKeyIsNotFound)
{
    EXPECT_FALSE(lookup_(insert_key("read", false, "NULL")));
}

TEST_F(PgApiKeyLookupTest, ExpiredKeyIsNotFound)
{
    EXPECT_FALSE(lookup_(insert_key("read", true, "now() - interval '1 minute'")));
}

TEST_F(PgApiKeyLookupTest, FutureExpiryIsReturned)
{
    const auto rec = lookup_(insert_key("read", true, "now() + interval '1 hour'"));
    ASSERT_TRUE(rec);
    ASSERT_TRUE(rec->expires_at);
    const auto left = *rec->expires_at - std::chrono::system_clock::now();
    EXPECT_GT(left, std::chrono::minutes(59));
    EXPECT_LE(left, std::chrono::minutes(61));
}

TEST(PgApiKeyLookupOutageTest, DatabaseFailureIsABackendError)
{
    db::PoolConfig cfg;
    cfg.connection_string = db::connection_string_from_env();
    cfg.min_connections = 1;
    cfg.max_connections = 1;
    cfg.acquire_timeout = std::chrono::milliseconds(200);
    std::shared_ptr<db::PgPool> pool;
    try
    {
        pool = std::make_shared<db::PgPool>(cfg);
    }
    catch (const std::exception &)
    {
        GTEST_SKIP() << "Postgres not reachable";
    }

    auto held = pool->acquire(); // exhaust the pool
    const auto lookup = auth::make_pg_api_key_lookup(pool);
    EXPECT_THROW(lookup(auth::ApiKeyValidator::hash_key("x")), auth::ApiKeyBackendError);
}