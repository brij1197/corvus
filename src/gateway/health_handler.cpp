#include "corvus/gateway/health_handler.h"
#include "corvus/version.h"
#include <drogon/drogon.h>

namespace corvus::gateway
{

    drogon::HttpHandlerCb health_handler()
    {
        return [](const drogon::HttpRequestPtr &req,
                  std::function<void(const drogon::HttpResponsePtr &)> &&callback)
        {
            auto body = Json::Value{};
            body["status"] = "ok";
            body["version"] = std::string{corvus::version::string()};
            body["path"] = req->getPath();

            auto resp = drogon::HttpResponse::newHttpJsonResponse(body);
            resp->setStatusCode(drogon::k200OK);
            callback(resp);
        };
    }

} // namespace corvus::gateway