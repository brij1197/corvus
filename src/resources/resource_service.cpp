#include "corvus/resources/resource_service.h"
#include <drogon/drogon.h>
#include <pqxx/pqxx>

namespace corvus::resources
{

    ResourceService::ResourceService(std::shared_ptr<ResourceRepository> repository, std::shared_ptr<db::CacheAside> cache)
        : repository_(std::move(repository)), cache_(std::move(cache))
    {
    }

    std::string ResourceService::cache_key(const std::string &client_id, const std::string &id) const
    {
        return "resource:" + client_id + ":" + id;
    }

    Resource ResourceService::create(const std::string &client_id, const CreateResourceRequest &req)
    {
        try
        {
            return repository_->create(client_id, req);
        }
        catch (const pqxx::unique_violation &)
        {
            throw ResourceAlreadyExists("A resource named '" + req.name + "' already exists");
        }
    }

    Resource ResourceService::get(const std::string &client_id, const std::string &id)
    {
        const auto key = cache_key(client_id, id);
        if (const auto cached = cache_->get(key))
            return from_json(nlohmann::json::parse(*cached));

        // Taken before the read so a write that lands during it is detected.
        const auto version = cache_->version(key);

        const auto found = repository_->find_by_id(client_id, id);
        if (!found)
            throw ResourceNotFound("Resource not found: " + id);

        if (version)
            cache_->put_if_version(key, *version, to_json(*found).dump(),
                                   kResourceCacheTtlSeconds);
        return *found;
    }

    ListResult ResourceService::list(const std::string &client_id, const ListFilter &filter)
    {
        return repository_->list(client_id, filter);
    }

    Resource ResourceService::update(const std::string &client_id, const std::string &id, const UpdateResourceRequest &req)
    {
        std::optional<Resource> updated;
        try
        {
            updated = repository_->update(client_id, id, req);
        }
        catch (const pqxx::unique_violation &)
        {
            throw ResourceAlreadyExists(req.name ? "A resource named '" + *req.name + "' already exists" : "Update would violate a uniqueness constraint");
        }

        if (!updated)
            throw ResourceNotFound("Resource not found: " + id);

        const auto key = cache_key(client_id, id);
        if (!cache_->invalidate_versioned(key))
        {
            LOG_WARN << "update committed for " << id
                     << " but cache eviction failed; stale until TTL";
        }
        return *updated;
    }

    void ResourceService::remove(const std::string &client_id, const std::string &id)
    {
        const bool deleted = repository_->remove(client_id, id);
        if (!deleted)
            throw ResourceNotFound("Resource not found: " + id);

        const auto key = cache_key(client_id, id);
        if (!cache_->invalidate_versioned(key))
        {
            LOG_WARN << "delete committed for " << id
                     << " but cache eviction failed; stale until TTL";
        }
    }
} // namespace corvus::resources