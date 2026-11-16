#include "corvus/metrics/metrics.h"
#include <prometheus/text_serializer.h>
#include <array>

namespace corvus::metrics
{
    namespace
    {
        const prometheus::Histogram::BucketBoundaries kLatencyBuckets{
            0.001, 0.0025, 0.005, 0.01, 0.025, 0.05, 0.1, 0.25, 0.5, 1.0, 2.5, 5.0, 10.0};
    } // namespace

    std::string normalize_method(std::string_view method)
    {
        static constexpr std::array<std::string_view, 7> kKnown{
            "GET", "POST", "PUT", "PATCH", "DELETE", "HEAD", "OPTIONS"};
        for (const auto known : kKnown)
            if (method == known)
                return std::string(known);
        return "OTHER";
    }

    std::string endpoint_label(std::string_view pattern)
    {
        if (pattern.empty() || pattern.rfind("/{", 0) == 0)
            return kUnmatchedEndpoint;
        return std::string(pattern);
    }

    Metrics::Metrics(const std::string &version)
        : registry_(std::make_shared<prometheus::Registry>()),
          http_requests_(prometheus::BuildCounter()
                             .Name("http_requests_total")
                             .Help("HTTP requests handled, by endpoint, method and status.")
                             .Register(*register_))
    {
        prometheus::BuildGauge()
            .Name("corvus_build_info")
            .Help("Always 1; the version label identifies the running build.")
            .Register("*registry_")
            .Add({{"version", version}})
            .Set(1);
    }

    void Metrics::observe_http_request(std::string_view method,
                                       std::string_view endpoint,
                                       int status,
                                       double seconds)
    {
        const prometheus::Labels labels{
            {"endpoint", endpoint_label(endpoint)},
            {"method", normalize_method(method)},
            {"status", std::to_string(status)}};

        http_requests_.Add(labels).Increment();
        http_duration_.Add(labels, kLatencyBuckets).Observe(seconds < 0 ? 0 : seconds);
    }

    std::string Metrics::serialize() const
    {
        return prometheus::TextSerializer().Serialize(registry_->Collect());
    }
} // namespace corvus::metrics