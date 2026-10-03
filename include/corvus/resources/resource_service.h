#pragma once
#include "corvus/db/cache_aside.h"
#include "corvus/resources/resource.h"
#include "corvus/resources/resource_repository.h"
#include <memory>
#include <chrono>
#include <cstdint>
#include <mutex>
#include <unordered_map>
#include <stdexcept>
#include <string>

namespace corvus::resources
{
    struct ResourceNotFound : std::runtime_error
    {
        using std::runtime_error::runtime_error;
    };

    struct ResourceAlreadyExists : std::runtime_error
    {
        using std::runtime_error::runtime_error;
    };

    constexpr int kResourceCacheTtlSeconds = 60;

    class ResourceService
    {
    public:
        ResourceService(std::shared_ptr<ResourceRepository> repository, std::shared_ptr<db::CacheAside> cache);

        Resource create(const std::string &client_id, const CreateResourceRequest &req);

        Resource get(const std::string &client_id, const std::string &id);

        ListResult list(const std::string &client_id, const ListFilter &filter);

        Resource update(const std::string &client_id, const std::string &id, const UpdateResourceRequest &req);

        void remove(const std::string &client_id, const std::string &id);

    private:
        std::string cache_key(const std::string &client_id, const std::string &id) const;

        std::uint64_t capture_epoch(const std::string &key);

        void cache_if_unchanged(const std::string &key,
                                std::uint64_t captured,
                                const std::string &value);

        void bump_epoch(const std::string &key);

        void prune_epochs_locked();

        std::shared_ptr<ResourceRepository> repository_;
        std::shared_ptr<db::CacheAside> cache_;

        mutable std::mutex cache_mutex_;

        struct EpochEntry
        {
            std::uint64_t value{0};
            std::chrono::steady_clock::time_point touched{};
        };

        mutable std::unordered_map<std::string, EpochEntry> cache_epochs_;
        std::uint64_t epoch_counter_{0};

        static constexpr std::chrono::minutes kEpochRetention{5};

        static constexpr std::size_t kEpochPruneThreshold{1024};
    };
} // namespace corvus::resources