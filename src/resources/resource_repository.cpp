#include "corvus/resources/resource_repository.h"
#include "corvus/resources/cursor.h"
#include <algorithm>
#include <sstream>

namespace corvus::resources
{
    namespace
    {
        std::string timestamp_select(const std::string &column)
        {
            return "to_char(" + column +
                   " AT TIME ZONE 'UTC', 'YYYY-MM-DD\"T\"HH24:MI:SS.US\"Z\"')";
        }

        std::string select_columns()
        {
            return "id, client_id, kind, name, status, metadata::text, " + timestamp_select("created_at") + " AS created_at, " + timestamp_select("updated_at") + " AS updated_at";
        }

        Resource row_to_resource(const pqxx::row &row)
        {
            Resource r;
            r.id = row["id"].as<std::string>();
            r.client_id = row["client_id"].as<std::string>();
            r.kind = row["kind"].as<std::string>();
            r.name = row["name"].as<std::string>();
            r.status = row["status"].as<std::string>();
            r.metadata = nlohmann::json::parse(row["metadata"].as<std::string>());
            r.created_at = row["created_at"].as<std::string>();
            r.updated_at = row["updated_at"].as<std::string>();
            return r;
        }
    } // namespace

    ResourceRepository::ResourceRepository(std::shared_ptr<db::PgPool> pool) : pool_(std::move(pool)) {}

    Resource ResourceRepository::create(const std::string &client_id, const CreateResourceRequest &req)
    {
        auto conn = pool_->acquire();
        pqxx::work txn(conn.get());

        const auto status_val = req.status.value_or("unknown");
        const auto metadata_val = req.metadata.value_or(nlohmann::json::object()).dump();

        const auto query =
            "INSERT INTO resources (client_id, kind, name, status, metadata) "
            "VALUES ($1, $2, $3, $4, $5::jsonb) "
            "RETURNING " +
            select_columns();

        const auto result = txn.exec_params(
            query,
            client_id,
            req.kind,
            req.name,
            status_val,
            metadata_val);
        txn.commit();

        return row_to_resource(result[0]);
    }

    std::optional<Resource> ResourceRepository::find_by_id(const std::string &client_id, const std::string &id)
    {
        auto conn = pool_->acquire();
        pqxx::work txn(conn.get());

        const auto query =
            "SELECT " + select_columns() +
            " FROM resources WHERE client_id = $1 AND id = $2";

        const auto result = txn.exec_params(query, client_id, id);
        txn.commit();

        if (result.empty())
            return std::nullopt;
        return row_to_resource(result[0]);
    }

    ListResult ResourceRepository::list(const std::string &client_id, const ListFilter &filter)
    {
        auto conn = pool_->acquire();
        pqxx::work txn(conn.get());

        const int page_size = std::clamp(filter.limit, 1, 200);

        std::ostringstream sql;
        sql << "SELECT " << select_columns() << " FROM resources " << "WHERE client_id = " << txn.quote(client_id);

        if (filter.kind)
            sql << " AND kind = " << txn.quote(*filter.kind);

        if (filter.status)
            sql << " AND status = " << txn.quote(*filter.status);

        if (filter.cursor && !filter.cursor->empty())
        {
            const auto pos = decode_cursor(*filter.cursor);
            sql << " AND (created_at, id) < ("
                << txn.quote(pos.created_at) << "::timestamptz, "
                << txn.quote(pos.id) << "::uuid)";
        }

        sql << " ORDER BY created_at DESC, id DESC LIMIT " << (page_size + 1);

        const auto result = txn.exec(sql.str());
        txn.commit();

        ListResult out;
        const auto result_size = result.size();
        const bool has_more =
            result_size > static_cast<pqxx::result::size_type>(page_size);
        const pqxx::result::size_type count =
            has_more ? static_cast<pqxx::result::size_type>(page_size) : result_size;

        for (pqxx::result::size_type i = 0; i < count; ++i)
            out.items.push_back(row_to_resource(result[i]));

        out.has_more = has_more;
        if (has_more && !out.items.empty())
        {
            const auto &last = out.items.back();
            out.next_cursor = encode_cursor({last.created_at, last.id});
        }
        return out;
    }

    std::optional<Resource> ResourceRepository::update(const std::string &client_id, const std::string &id, const UpdateResourceRequest &req)
    {
        auto conn = pool_->acquire();
        pqxx::work txn(conn.get());

        std::ostringstream sql;
        sql << "UPDATE resources SET ";

        bool first = true;
        auto add_set = [&](const std::string &column, const std::string &value)
        {
            if (!first)
                sql << ", ";
            first = false;
            sql << column << " = " << txn.quote(value);
        };

        if (req.name)
            add_set("name", *req.name);
        if (req.status)
            add_set("status", *req.status);
        if (req.metadata)
        {
            if (!first)
                sql << ", ";
            first = false;
            sql << "metadata = " << txn.quote(req.metadata->dump()) << "::jsonb";
        }

        sql << " WHERE client_id = " << txn.quote(client_id)
            << " AND id = " << txn.quote(id)
            << " RETURNING " << select_columns();

        const auto result = txn.exec(sql.str());
        txn.commit();

        if (result.empty())
            return std::nullopt;
        return row_to_resource(result[0]);
    }

    bool ResourceRepository::remove(const std::string &client_id, const std::string &id)
    {

        auto conn = pool_->acquire();
        pqxx::work txn(conn.get());

        const auto result = txn.exec_params(
            "DELETE FROM resources WHERE client_id = $1 AND id = $2 "
            "RETURNING id",
            client_id, id);
        txn.commit();

        return !result.empty();
    }
} // namespace corvus::resources