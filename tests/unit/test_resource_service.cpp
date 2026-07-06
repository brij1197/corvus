#include <gtest/gtest.h>
#include "corvus/resources/resource_service.h"
#include "corvus/db/pg_pool.h"
#include "corvus/db/redis_connection.h"
#include <cstdlib>
#include <random>
#include <sstream>

using namespace corvus::resources;
using namespace corvus::db;

namespace
{

    std::string random_suffix()
    {
        static std::random_device rd;
        static std::mt19937_64 gen(rd());
        std::ostringstream oss;
        oss << std::hex << gen() << gen();
        return oss.str();
    }

} // namespace

class ResourceServiceTest : public ::testing::Test
{
protected:
    void SetUp() override
    {
        try
        {
            PoolConfig cfg;
            cfg.connection_string = connection_string_from_env();
            cfg.min_connections = 1;
            cfg.max_connections = 3;
            cfg.acquire_timeout = std::chrono::milliseconds(1500);
            pool_ = std::make_shared<PgPool>(cfg);
        }
        catch (const std::exception &)
        {
            GTEST_SKIP() << "Postgres not reachable via CORVUS_DB_HOST/PORT "
                         << "env vars - skipping resource service tests.";
        }

        try
        {
            RedisConfig rcfg = redis_config_from_env();
            rcfg.connect_timeout_ms = 1000;
            redis_ = std::make_unique<RedisConnection>(rcfg);
        }
        catch (const std::exception &)
        {
            GTEST_SKIP() << "Redis not reachable via CORVUS_REDIS_HOST/PORT "
                         << "env vars - skipping resource service tests.";
        }

        repository_ = std::make_shared<ResourceRepository>(pool_);
        cache_ = std::make_shared<CacheAside>(*redis_);
        service_ = std::make_shared<ResourceService>(repository_, cache_);

        auto conn = pool_->acquire();
        pqxx::work txn(conn.get());
        const auto result = txn.exec_params(
            "INSERT INTO clients (name) VALUES ($1) RETURNING id",
            "resource-service-test-" + random_suffix());
        txn.commit();
        client_id_ = result[0][0].as<std::string>();
    }

    void TearDown() override
    {
        if (!pool_ || client_id_.empty())
            return;

        auto conn = pool_->acquire();
        pqxx::work txn(conn.get());
        txn.exec_params("DELETE FROM clients WHERE id = $1::uuid", client_id_);
        txn.commit();
    }

    CreateResourceRequest make_request(const std::string &name,
                                       const std::string &kind = "server")
    {
        CreateResourceRequest req;
        req.kind = kind;
        req.name = name;
        return req;
    }

    std::string cache_key_for(const std::string &id) const
    {
        return "resource:" + client_id_ + ":" + id;
    }

    std::shared_ptr<PgPool> pool_;
    std::unique_ptr<RedisConnection> redis_;
    std::shared_ptr<ResourceRepository> repository_;
    std::shared_ptr<CacheAside> cache_;
    std::shared_ptr<ResourceService> service_;
    std::string client_id_;
};

TEST_F(ResourceServiceTest, CreateReturnsResource)
{
    const auto created = service_->create(client_id_, make_request("web-01"));

    EXPECT_FALSE(created.id.empty());
    EXPECT_EQ(created.client_id, client_id_);
    EXPECT_EQ(created.name, "web-01");
}

TEST_F(ResourceServiceTest, CreateDuplicateThrowsAlreadyExists)
{
    service_->create(client_id_, make_request("dup-name"));
    EXPECT_THROW(service_->create(client_id_, make_request("dup-name")),
                 ResourceAlreadyExists);
}

TEST_F(ResourceServiceTest, GetReturnsCreatedResource)
{
    const auto created = service_->create(client_id_, make_request("web-02"));
    const auto fetched = service_->get(client_id_, created.id);

    EXPECT_EQ(fetched.id, created.id);
    EXPECT_EQ(fetched.name, "web-02");
}

TEST_F(ResourceServiceTest, GetThrowsNotFoundForMissingId)
{
    EXPECT_THROW(
        service_->get(client_id_, "00000000-0000-0000-0000-000000000000"),
        ResourceNotFound);
}

TEST_F(ResourceServiceTest, GetThrowsNotFoundForForeignClient)
{
    const auto created = service_->create(client_id_, make_request("web-03"));
    EXPECT_THROW(
        service_->get("00000000-0000-0000-0000-000000000000", created.id),
        ResourceNotFound);
}

TEST_F(ResourceServiceTest, GetPopulatesCache)
{
    const auto created = service_->create(client_id_, make_request("web-04"));

    EXPECT_FALSE(cache_->exists(cache_key_for(created.id)));
    service_->get(client_id_, created.id);
    EXPECT_TRUE(cache_->exists(cache_key_for(created.id)));

    cache_->invalidate(cache_key_for(created.id)); // cleanup
}

TEST_F(ResourceServiceTest, GetServesFromCacheOnSecondCall)
{
    const auto created = service_->create(client_id_, make_request("web-05"));
    const auto first = service_->get(client_id_, created.id);
    const auto second = service_->get(client_id_, created.id);

    EXPECT_EQ(first.id, second.id);
    EXPECT_EQ(first.updated_at, second.updated_at);

    cache_->invalidate(cache_key_for(created.id)); // cleanup
}

TEST_F(ResourceServiceTest, UpdateAppliesChanges)
{
    const auto created = service_->create(client_id_, make_request("web-06"));

    UpdateResourceRequest upd;
    upd.status = "decommissioned";
    const auto updated = service_->update(client_id_, created.id, upd);

    EXPECT_EQ(updated.status, "decommissioned");
    EXPECT_EQ(updated.name, "web-06"); // unchanged
}

TEST_F(ResourceServiceTest, UpdateThrowsNotFoundForMissingId)
{
    UpdateResourceRequest upd;
    upd.status = "x";
    EXPECT_THROW(
        service_->update(client_id_, "00000000-0000-0000-0000-000000000000", upd),
        ResourceNotFound);
}

TEST_F(ResourceServiceTest, UpdateRenameConflictThrowsAlreadyExists)
{
    service_->create(client_id_, make_request("taken-name"));
    const auto other = service_->create(client_id_, make_request("other-name"));

    UpdateResourceRequest upd;
    upd.name = "taken-name";
    EXPECT_THROW(service_->update(client_id_, other.id, upd),
                 ResourceAlreadyExists);
}

TEST_F(ResourceServiceTest, UpdateInvalidatesCache)
{
    const auto created = service_->create(client_id_, make_request("web-07"));
    service_->get(client_id_, created.id); // populate cache
    ASSERT_TRUE(cache_->exists(cache_key_for(created.id)));

    UpdateResourceRequest upd;
    upd.status = "active";
    service_->update(client_id_, created.id, upd);

    EXPECT_FALSE(cache_->exists(cache_key_for(created.id)));

    // Next get() should reflect the update, not a stale cached copy
    const auto refetched = service_->get(client_id_, created.id);
    EXPECT_EQ(refetched.status, "active");

    cache_->invalidate(cache_key_for(created.id)); // cleanup
}

TEST_F(ResourceServiceTest, RemoveDeletesResource)
{
    const auto created = service_->create(client_id_, make_request("web-08"));
    service_->remove(client_id_, created.id);

    EXPECT_THROW(service_->get(client_id_, created.id), ResourceNotFound);
}

TEST_F(ResourceServiceTest, RemoveThrowsNotFoundForMissingId)
{
    EXPECT_THROW(
        service_->remove(client_id_, "00000000-0000-0000-0000-000000000000"),
        ResourceNotFound);
}

TEST_F(ResourceServiceTest, RemoveInvalidatesCache)
{
    const auto created = service_->create(client_id_, make_request("web-09"));
    service_->get(client_id_, created.id); // populate cache
    ASSERT_TRUE(cache_->exists(cache_key_for(created.id)));

    service_->remove(client_id_, created.id);

    EXPECT_FALSE(cache_->exists(cache_key_for(created.id)));
}

TEST_F(ResourceServiceTest, ListReturnsCreatedResources)
{
    service_->create(client_id_, make_request("list-a"));
    service_->create(client_id_, make_request("list-b"));

    ListFilter filter;
    const auto result = service_->list(client_id_, filter);

    EXPECT_GE(result.items.size(), 2u);
    for (const auto &r : result.items)
        EXPECT_EQ(r.client_id, client_id_);
}

TEST_F(ResourceServiceTest, ListPropagatesInvalidCursorError)
{
    ListFilter filter;
    filter.cursor = "not-valid-base64!!!";
    EXPECT_THROW(service_->list(client_id_, filter), InvalidCursorError);
}