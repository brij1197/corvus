#include <gtest/gtest.h>
#include "corvus/db/pg_pool.h"

using namespace corvus::db;

TEST(PgPoolConfigTest, ThrowsOnEmptyConnectionString)
{
    PoolConfig cfg;
    cfg.connection_string = "";
    EXPECT_THROW(PgPool pool(cfg), DbConfigError);
}

TEST(PgPoolConfigTest, ThrowsOnZeroMinConnections)
{
    PoolConfig cfg;
    cfg.connection_string = "host=localhost dbname=test";
    cfg.min_connections = 0;
    cfg.max_connections = 5;
    EXPECT_THROW(PgPool pool(cfg), DbConfigError);
}

TEST(PgPoolConfigTest, ThrowsWhenMaxLessThanMin)
{
    PoolConfig cfg;
    cfg.connection_string = "host=localhost dbname=test";
    cfg.min_connections = 5;
    cfg.max_connections = 2;
    EXPECT_THROW(PgPool pool(cfg), DbConfigError);
}

TEST(PgPoolConfigTest, ThrowsOnUnreachableHost)
{
    PoolConfig cfg;
    cfg.connection_string = "host=127.0.0.1 port=19999 dbname=corvus "
                            "user=postgres connect_timeout=1";
    cfg.min_connections = 1;
    cfg.max_connections = 2;
    EXPECT_THROW(PgPool pool(cfg), DbConfigError);
}

TEST(PgPoolExhaustedTest, IsStdRuntimeError)
{
    DbPoolExhausted err("pool is full");
    EXPECT_STREQ(err.what(), "pool is full");
    EXPECT_TRUE((std::is_base_of<std::runtime_error, DbPoolExhausted>::value));
}

TEST(DbConfigErrorTest, IsStdRuntimeError)
{
    DbConfigError err("bad config");
    EXPECT_STREQ(err.what(), "bad config");
    EXPECT_TRUE((std::is_base_of<std::runtime_error, DbConfigError>::value));
}

TEST(ConnectionStringTest, ContainsHostField)
{
    const auto cs = connection_string_from_env();
    EXPECT_NE(cs.find("port="), std::string::npos);
}

TEST(ConnectionStringTest, ContainsPortField)
{
    const auto cs = connection_string_from_env();
    EXPECT_NE(cs.find("port="), std::string::npos);
}

TEST(ConnectionStringTest, ContainsDbNameField)
{
    const auto cs = connection_string_from_env();
    EXPECT_NE(cs.find("dbname="), std::string::npos);
}

TEST(ConnectionStringTest, ContainsUserField)
{
    const auto cs = connection_string_from_env();
    EXPECT_NE(cs.find("user="), std::string::npos);
}

TEST(ConnectionStringTest, DefaultHostIsLocalhost)
{
    const char *existing = std::getenv("CORVUS_DB_HOST");
    if (existing && *existing)
    {
        GTEST_SKIP() << "CORVUS_DB_HOST is set, skipping default host test";
    }
    EXPECT_NE(connection_string_from_env().find("host=localhost"), std::string::npos);
}

#include <atomic>
#include <chrono>
#include <future>
#include <thread>

namespace
{
    bool postgres_available()
    {
        try
        {
            pqxx::connection c(connection_string_from_env());
            return c.is_open();
        }
        catch (...)
        {
            return false;
        }
    }
} // namespace

TEST(PgPoolLiveTest, SlowConnectDoesNotBlockOtherAcquiresOrReturns)
{
    if (!postgres_available())
        GTEST_SKIP() << "Postgres not reachable";
    using namespace std::chrono_literals;

    std::atomic<bool> slow{false};
    PoolConfig cfg;
    cfg.connection_string = connection_string_from_env();
    cfg.min_connections = 1;
    cfg.max_connections = 3;
    cfg.connect = [&](const std::string &cs)
    {
        if (slow)
            std::this_thread::sleep_for(1500ms);
        return std::make_unique<pqxx::connection>(cs);
    };
    PgPool pool(cfg);

    auto held = std::make_unique<PgConnection>(pool.acquire());
    slow = true;

    auto opener = std::async(std::launch::async, [&]
                             { return pool.acquire(); });
    std::this_thread::sleep_for(100ms);
    const auto start = std::chrono::steady_clock::now();
    held.reset();
    auto again = pool.acquire();
    const auto waited = std::chrono::steady_clock::now() - start;

    EXPECT_LT(waited, 500ms) << "acquire waited behind another thread's connect";
    auto opened = opener.get();
    EXPECT_EQ(pool.size(), 2u);
}

TEST(PgPoolLiveTest, FailedConnectFreesItsSlot)
{
    if (!postgres_available())
        GTEST_SKIP() << "Postgres not reachable";

    std::atomic<bool> fail{false};
    PoolConfig cfg;
    cfg.connection_string = connection_string_from_env();
    cfg.min_connections = 1;
    cfg.max_connections = 2;
    cfg.connect = [&](const std::string &cs)
    {
        if (fail)
            throw std::runtime_error("connection refused");
        return std::make_unique<pqxx::connection>(cs);
    };
    PgPool pool(cfg);

    auto held = pool.acquire();
    fail = true;
    EXPECT_THROW(pool.acquire(), DbConfigError);
    EXPECT_EQ(pool.size(), 1u) << "the failed connect must not keep a slot";

    fail = false;
    EXPECT_NO_THROW(pool.acquire());
}