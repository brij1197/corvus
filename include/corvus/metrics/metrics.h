#pragma once
#include <prometheus/counter.h>
#include <prometheus/family.h>
#include <prometheus/gauge.h>
#include <prometheus/histogram.h>
#include <prometheus/registry.h>
#include <memory>
#include <string>
#include <string_view>

namespace corvus::metrics
{
    inline constexpr const char *kContentType = "text/plain; version=0.0.4; charset=utf-8";

    inline constexpr const char *kUnmatchedEndpoint = "unmatched";

    class Metrics
    {
    public:
        explicit Metrics(const std::string &version);

        /// Records one finished HTTP request.
        /// @param endpoint  the matched route pattern, e.g. "/v1/resources/{id}";
        ///                  empty means no route matched.
        void observe_http_request(std::string_view method,
                                  std::string_view endpoint,
                                  int status,
                                  double seconds);

        std::string serialize() const;

        prometheus::Registry &registry() noexcept { return *registry_; }

    private:
        std::shared_ptr<prometheus::Registry> registry_;
        prometheus::Family<prometheus::Counter> &http_requests_;
        prometheus::Family<prometheus::Histogram> &http_duration_;
    };

    /// The endpoint label for a matched route pattern. Empty patterns and the
    /// catch-all 404 routes ("/{a}", "/{a}/{b}", ...) become "unmatched"
    std::string endpoint_label(std::string_view pattern);

    /// Known methods pass through; anything else becomes "OTHER", so a client
    /// can't create series by inventing method names.
    std::string normalize_method(std::string_view method);

} // namespace corvus::metrics