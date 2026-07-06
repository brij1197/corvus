#pragma once
#include "corvus/db/cache_aside.h"
#include "corvus/resources/resource.h"
#include "corvus/resources/resource_repository.h"
#include <memory>
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

        std::shared_ptr<ResourceRepository> repository_;
        std::shared_ptr<db::CacheAside> cache_;
    };
} // namespace corvus::resources