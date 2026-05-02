#pragma once
#include <drogon/HttpController.h>

namespace corvus::gateway {

class HealthHandler : public drogon::HttpSimpleController<HealthHandler>
{
public:
    PATH_LIST_BEGIN
    PATH_ADD("/health", drogon::Get);
    PATH_ADD("/ready",  drogon::Get);
    PATH_LIST_END

    void asyncHandleHttpRequest(
        const drogon::HttpRequestPtr& req,
        std::function<void(const drogon::HttpResponsePtr&)>&& callback) override;
};

} // namespace corvus::gateway
