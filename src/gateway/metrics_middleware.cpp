#include "corvus/gateway/metrics_middleware.h"
#include <drogon/drogon.h>

namespace corvus::gateway
{
    void register_http_metrics(std::shared_ptr<metrics::Metrics> metrics)
    {
        drogon::app().registerPreSendingAdvice(
            [metrics](const drogon::HttpRequestPtr &req, const drogon::HttpResponsePtr &resp)
            {
                const auto elapsed_us =
                    trantor::Date::now().microSecondsSinceEpoch() -
                    req->creationDate().microSecondsSinceEpoch();

                metrics->observe_http_request(
                    req->getMethodString(),
                    req->getMatchedPathPattern(),
                    static_cast<int>(resp->getStatusCode()),
                    static_cast<double>(elapsed_us) / 1e6);
            });
    }

    void register_metrics_route(std::shared_ptr<metrics::Metrics> metrics)
    {
        drogon::app().registerHandler(
            "/metrics",
            [metrics](const drogon::HttpRequestPtr &,
                      std::function<void(const drogon::HttpResponsePtr &)> &&cb)
            {
                auto resp = drogon::HttpResponse::newHttpResponse();
                resp->setStatusCode(drogon::k200OK);
                resp->setContentTypeString(metrics::kContentType);
                resp->setBody(metrics->serialize());
                cb(resp);
            },
            {drogon::Get});

        LOG_INFO << "Metrics route registered";
    }

} // namespace corvus::gateway