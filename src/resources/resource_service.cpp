#include "corvus/resources/resource_service.h"
#include <drogon/drogon.h>
#include <iterator>
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

    std::uint64_t ResourceService::capture_epoch(const std::string &key)
    {
        std::lock_guard<std::mutex> lock(cache_mutex_);
        auto &entry = cache_epochs_[key];
        if (entry.value == 0)
            entry.value = ++epoch_counter_;
        entry.touched = std::chrono::steady_clock::now();
        return entry.value;
    }

    void ResourceService::cache_if_unchanged(const std::string &key,
                                             std::uint64_t captured,
                                             const std::string &value)
    {
        std::lock_guard<std::mutex> lock(cache_mutex_);

        const auto it = cache_epochs_.find(key);
        const bool unchanged =
            (it != cache_epochs_.end()) && (it->second.value == captured);

        if (!unchanged)
        {
            LOG_DEBUG << "skipping cache put for " << key
                      << ": superseded by a concurrent write";
            return;
        }

        cache_->put(key, value, kResourceCacheTtlSeconds);
    }

    void ResourceService::bump_epoch(const std::string &key)
    {
        std::lock_guard<std::mutex> lock(cache_mutex_);
        auto &entry = cache_epochs_[key];
        entry.value = ++epoch_counter_;
        entry.touched = std::chrono::steady_clock::now();

        if (cache_epochs_.size() > kEpochPruneThreshold)
            prune_epochs_locked();
    }

    void ResourceService::prune_epochs_locked()
    {
        const auto cutoff = std::chrono::steady_clock::now() - kEpochRetention;
        for (auto it = cache_epochs_.begin(); it != cache_epochs_.end();)
        {
            it = (it->second.touched < cutoff) ? cache_epochs_.erase(it)
                                               : std::next(it);
        }
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

        const auto epoch_at_read = capture_epoch(key);

        const auto found = repository_->find_by_id(client_id, id);
        if (!found)
            throw ResourceNotFound("Resource not found: " + id);

        cache_if_unchanged(key, epoch_at_read, to_json(*found).dump());
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
        bump_epoch(key);
        if (!cache_->invalidate(key))
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
        bump_epoch(key);
        if (!cache_->invalidate(key))
        {
            LOG_WARN << "delete committed for " << id
                     << " but cache eviction failed; stale until TTL";
        }
    }
} // namespace corvus::resources