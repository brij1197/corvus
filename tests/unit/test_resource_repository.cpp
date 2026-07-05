#include <gtest/gtest.h>
#include "corvus/resources/resource_repository.h"
#include "corvus/db/pg_pool.h"
#include <cstdlib>
#include <random>
#include <sstream>

using namespace corvus::resources;
using namespace corvus::db;

namespace
{

    std::string env_or(const char *var, const char *fallback)
    {
        const char *v = std::getenv(var);
        return (v && *v) ? v : fallback;
    }

    std::string random_uuid_like()
    {
        static std::random_device rd;
        static std::mt19937_64 gen(rd());
        std::ostringstream oss;
        oss << std::hex << gen() << gen();
        return oss.str();
    }

    bool postgres_available(std::shared_ptr<PgPool> &pool)
    {
        try
        {
            PoolConfig cfg;
            cfg.connection_string = connection_string_from_env();
            cfg.min_connections = 1;
            cfg.max_connections = 3;
            cfg.acquire_timeout = std::chrono::milliseconds(1500);
            pool = std::make_shared<PgPool>(cfg);
            return true;
        }
        catch (const std::exception &)
        {
            return false;
        }
    }

} // namespace

class ResourceRepositoryTest : public ::testing::Test
{
protected:
    void SetUp() override
    {
        if (!postgres_available(pool_))
        {
            GTEST_SKIP() << "Postgres not reachable via "
                         << "CORVUS_DB_HOST/PORT env vars - "
                         << "skipping resource repository tests.";
        }
        repo_ = std::make_shared<ResourceRepository>(pool_);

        auto conn = pool_->acquire();
        pqxx::work txn(conn.get());
        const auto result = txn.exec_params(
            "INSERT INTO clients (name) VALUES ($1) RETURNING id",
            "resource-repo-test-" + random_uuid_like());
        txn.commit();
        client_id_ = result[0][0].as<std::string>();
    }

    void TearDown() override
    {
        if (!pool_)
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

    std::shared_ptr<PgPool> pool_;
    std::shared_ptr<ResourceRepository> repo_;
    std::string client_id_;
};

TEST_F(ResourceRepositoryTest, CreateAndFindById)
{
    const auto created = repo_->create(client_id_, make_request("web-01"));

    EXPECT_FALSE(created.id.empty());
    EXPECT_EQ(created.client_id, client_id_);
    EXPECT_EQ(created.kind, "server");
    EXPECT_EQ(created.name, "web-01");
    EXPECT_EQ(created.status, "unknown");
    EXPECT_TRUE(created.metadata.is_object());
    EXPECT_FALSE(created.created_at.empty());

    const auto found = repo_->find_by_id(client_id_, created.id);
    ASSERT_TRUE(found.has_value());
    EXPECT_EQ(found->id, created.id);
    EXPECT_EQ(found->name, "web-01");
}

TEST_F(ResourceRepositoryTest, CreateWithStatusAndMetadata)
{
    CreateResourceRequest req;
    req.kind = "database";
    req.name = "db-01";
    req.status = "active";
    req.metadata = nlohmann::json{{"engine", "postgres"}, {"version", 16}};

    const auto created = repo_->create(client_id_, req);
    EXPECT_EQ(created.status, "active");
    EXPECT_EQ(created.metadata["engine"], "postgres");
    EXPECT_EQ(created.metadata["version"], 16);
}

TEST_F(ResourceRepositoryTest, FindByIdReturnsNulloptForMissingId)
{
    const auto found = repo_->find_by_id(
        client_id_, "00000000-0000-0000-0000-000000000000");
    EXPECT_FALSE(found.has_value());
}

TEST_F(ResourceRepositoryTest, FindByIdEnforcesTenantIsolation)
{
    const auto created = repo_->create(client_id_, make_request("tenant-a-only"));

    const auto found = repo_->find_by_id(
        "00000000-0000-0000-0000-000000000000", created.id);
    EXPECT_FALSE(found.has_value());
}

TEST_F(ResourceRepositoryTest, CreateDuplicateNameThrowsUniqueViolation)
{
    repo_->create(client_id_, make_request("dup-name"));
    EXPECT_THROW(repo_->create(client_id_, make_request("dup-name")),
                 pqxx::unique_violation);
}

TEST_F(ResourceRepositoryTest, SameNameAllowedForDifferentClients)
{
    auto conn = pool_->acquire();
    pqxx::work txn(conn.get());
    const auto result = txn.exec_params(
        "INSERT INTO clients (name) VALUES ($1) RETURNING id",
        "resource-repo-test-2-" + random_uuid_like());
    txn.commit();
    const auto other_client_id = result[0][0].as<std::string>();

    repo_->create(client_id_, make_request("shared-name"));
    EXPECT_NO_THROW(repo_->create(other_client_id, make_request("shared-name")));

    pqxx::work cleanup(conn.get());
    cleanup.exec_params("DELETE FROM clients WHERE id = $1::uuid", other_client_id);
    cleanup.commit();
}

TEST_F(ResourceRepositoryTest, UpdateSingleField)
{
    const auto created = repo_->create(client_id_, make_request("to-update"));

    UpdateResourceRequest upd;
    upd.status = "decommissioned";

    const auto updated = repo_->update(client_id_, created.id, upd);
    ASSERT_TRUE(updated.has_value());
    EXPECT_EQ(updated->status, "decommissioned");
    EXPECT_EQ(updated->name, "to-update");
}

TEST_F(ResourceRepositoryTest, UpdateMultipleFields)
{
    const auto created = repo_->create(client_id_, make_request("multi-update"));

    UpdateResourceRequest upd;
    upd.name = "renamed";
    upd.metadata = nlohmann::json{{"tag", "updated"}};

    const auto updated = repo_->update(client_id_, created.id, upd);
    ASSERT_TRUE(updated.has_value());
    EXPECT_EQ(updated->name, "renamed");
    EXPECT_EQ(updated->metadata["tag"], "updated");
}

TEST_F(ResourceRepositoryTest, UpdateReturnsNulloptForMissingId)
{
    UpdateResourceRequest upd;
    upd.status = "x";
    const auto updated = repo_->update(
        client_id_, "00000000-0000-0000-0000-000000000000", upd);
    EXPECT_FALSE(updated.has_value());
}

TEST_F(ResourceRepositoryTest, UpdateEnforcesTenantIsolation)
{
    const auto created = repo_->create(client_id_, make_request("isolated"));

    UpdateResourceRequest upd;
    upd.status = "hacked";
    const auto updated = repo_->update(
        "00000000-0000-0000-0000-000000000000", created.id, upd);
    EXPECT_FALSE(updated.has_value());

    const auto still_there = repo_->find_by_id(client_id_, created.id);
    ASSERT_TRUE(still_there.has_value());
    EXPECT_EQ(still_there->status, "unknown");
}

TEST_F(ResourceRepositoryTest, UpdateRenameConflictThrowsUniqueViolation)
{
    repo_->create(client_id_, make_request("existing-name"));
    const auto other = repo_->create(client_id_, make_request("other-name"));

    UpdateResourceRequest upd;
    upd.name = "existing-name";
    EXPECT_THROW(repo_->update(client_id_, other.id, upd),
                 pqxx::unique_violation);
}

TEST_F(ResourceRepositoryTest, RemoveDeletesResource)
{
    const auto created = repo_->create(client_id_, make_request("to-delete"));

    EXPECT_TRUE(repo_->remove(client_id_, created.id));
    EXPECT_FALSE(repo_->find_by_id(client_id_, created.id).has_value());
}

TEST_F(ResourceRepositoryTest, RemoveReturnsFalseForMissingId)
{
    EXPECT_FALSE(repo_->remove(
        client_id_, "00000000-0000-0000-0000-000000000000"));
}

TEST_F(ResourceRepositoryTest, RemoveEnforcesTenantIsolation)
{
    const auto created = repo_->create(client_id_, make_request("protected"));

    EXPECT_FALSE(repo_->remove(
        "00000000-0000-0000-0000-000000000000", created.id));
    EXPECT_TRUE(repo_->find_by_id(client_id_, created.id).has_value());
}

TEST_F(ResourceRepositoryTest, ListReturnsOnlyOwnClientResources)
{
    repo_->create(client_id_, make_request("list-1"));
    repo_->create(client_id_, make_request("list-2"));

    ListFilter filter;
    const auto result = repo_->list(client_id_, filter);

    EXPECT_GE(result.items.size(), 2u);
    for (const auto &r : result.items)
        EXPECT_EQ(r.client_id, client_id_);
}

TEST_F(ResourceRepositoryTest, ListFiltersByKind)
{
    repo_->create(client_id_, make_request("srv-1", "server"));
    repo_->create(client_id_, make_request("db-1", "database"));

    ListFilter filter;
    filter.kind = "database";
    const auto result = repo_->list(client_id_, filter);

    ASSERT_EQ(result.items.size(), 1u);
    EXPECT_EQ(result.items[0].kind, "database");
}

TEST_F(ResourceRepositoryTest, ListFiltersByStatus)
{
    CreateResourceRequest active_req = make_request("active-1");
    active_req.status = "active";
    repo_->create(client_id_, active_req);
    repo_->create(client_id_, make_request("unknown-1"));

    ListFilter filter;
    filter.status = "active";
    const auto result = repo_->list(client_id_, filter);

    ASSERT_EQ(result.items.size(), 1u);
    EXPECT_EQ(result.items[0].status, "active");
}

TEST_F(ResourceRepositoryTest, ListPaginatesWithCursor)
{
    for (int i = 0; i < 5; ++i)
        repo_->create(client_id_, make_request("page-item-" + std::to_string(i)));

    ListFilter filter;
    filter.limit = 2;
    const auto page1 = repo_->list(client_id_, filter);

    EXPECT_EQ(page1.items.size(), 2u);
    EXPECT_TRUE(page1.has_more);
    ASSERT_TRUE(page1.next_cursor.has_value());

    filter.cursor = page1.next_cursor;
    const auto page2 = repo_->list(client_id_, filter);

    EXPECT_EQ(page2.items.size(), 2u);
    for (const auto &p1 : page1.items)
        for (const auto &p2 : page2.items)
            EXPECT_NE(p1.id, p2.id);
}

TEST_F(ResourceRepositoryTest, ListWithMalformedCursorThrows)
{
    ListFilter filter;
    filter.cursor = "not-valid-base64!!!";
    EXPECT_THROW(repo_->list(client_id_, filter), InvalidCursorError);
}

TEST_F(ResourceRepositoryTest, ListEmptyForClientWithNoResources)
{
    auto conn = pool_->acquire();
    pqxx::work txn(conn.get());
    const auto result = txn.exec_params(
        "INSERT INTO clients (name) VALUES ($1) RETURNING id",
        "empty-client-" + random_uuid_like());
    txn.commit();
    const auto empty_client_id = result[0][0].as<std::string>();

    ListFilter filter;
    const auto list_result = repo_->list(empty_client_id, filter);
    EXPECT_TRUE(list_result.items.empty());
    EXPECT_FALSE(list_result.has_more);

    pqxx::work cleanup(conn.get());
    cleanup.exec_params("DELETE FROM clients WHERE id = $1::uuid", empty_client_id);
    cleanup.commit();
}