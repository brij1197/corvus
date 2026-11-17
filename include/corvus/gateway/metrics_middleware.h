#pragma once
#include "corvus/metrics/metrics.h"
#include <memory>

namespace corvus::gateway
{
    void register_http_metrics(std::shared_ptr<metrics::Metrics> metrics);

    void register_metrics_route(std::shared_ptr<metrics::Metrics> metrics);

} // namespace corvus::gateway