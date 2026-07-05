#pragma once
#include "corvus/db/pg_pool.h"
#include "corvus/resources/resource.h"
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace corvus::resources
{
    struct ListFilter
    {
        std::optional<std::string> kind;
        std::optional<std::string> status;
        int limit{50};
        std::optional<std::string> cursor;
    };

    struct ListResult
    {
        std::vector<Resource> items;
        std::optional<std::string> next_cursor;
        bool has_more{false};
    };

    struct InvalidCursorError : std::runtime_error
    {
        using std::runtime_error::runtime_error;
    };

    class ResourceRepository
    {
    public:
        explicit ResourceRepository(std::shared_ptr<db::PgPool> pool);

        Resource create(const std::string &client_id, const CreateResourceRequest &req);
        std::optional<Resource> find_by_id(const std::string &client_id, const std::string &id);
        ListResult list(const std::string &client_id, const ListFilter &filter);
        std::optional<Resource> update(const std::string &client_id, const std::string &id, const UpdateResourceRequest &req);
        bool remove(const std::string &client_id, const std::string &id);

    private:
        std::shared_ptr<db::PgPool> pool_;
    };
} // namespace corvus::resources