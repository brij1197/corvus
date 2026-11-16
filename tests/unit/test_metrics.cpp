#include <gtest/gtest.h>
#include "corvus/metrics/metrics.h"

using corvus::metrics::Metrics;

namespace
{
    bool contains(const std::string &text, const std::string &needle)
    {
        return text.find(needle) != std::string::npos;
    }
} // namespace

TEST(MetricsTest, ExposesBuildInfo)
{
    Metrics m("1.2.3");
    EXPECT_TRUE(contains(m.serialize(), "corvus_build_info{version=\"1.2.3\"} 1"));
}

TEST(MetricsTest, CountsRequestsByEndpointMethodAndStatus)
{
    Metrics m("test");
    m.observe_http_request("GET", "/v1/resources/{id}", 200, 0.01);
    m.observe_http_request("GET", "/v1/resources/{id}", 200, 0.02);
    m.observe_http_request("GET", "/v1/resources/{id}", 404, 0.01);
    m.observe_http_request("POST", "/v1/resources", 201, 0.01);

    const auto out = m.serialize();
    EXPECT_TRUE(contains(out, "# TYPE http_requests_total counter"));
    EXPECT_TRUE(contains(
        out, "http_requests_total{endpoint=\"/v1/resources/{id}\",method=\"GET\",status=\"200\"} 2"))
        << out;
    EXPECT_TRUE(contains(
        out, "http_requests_total{endpoint=\"/v1/resources/{id}\",method=\"GET\",status=\"404\"} 1"));
    EXPECT_TRUE(contains(
        out, "http_requests_total{endpoint=\"/v1/resources\",method=\"POST\",status=\"201\"} 1"));
}

TEST(MetricsTest, RecordsLatencyHistogram)
{
    Metrics m("test");
    m.observe_http_request("GET", "/health", 200, 0.004);
    m.observe_http_request("GET", "/health", 200, 0.3);

    const auto out = m.serialize();
    const std::string labels = "endpoint=\"/health\",method=\"GET\",status=\"200\"";
    EXPECT_TRUE(contains(out, "# TYPE http_request_duration_seconds histogram"));
    EXPECT_TRUE(contains(out, "http_request_duration_seconds_count{" + labels + "} 2")) << out;
    EXPECT_TRUE(contains(out, "http_request_duration_seconds_bucket{" + labels + ",le=\"0.0025\"} 0"));
    EXPECT_TRUE(contains(out, "http_request_duration_seconds_bucket{" + labels + ",le=\"0.005\"} 1"));
    EXPECT_TRUE(contains(out, "http_request_duration_seconds_bucket{" + labels + ",le=\"0.5\"} 2"));
    EXPECT_TRUE(contains(out, "http_request_duration_seconds_bucket{" + labels + ",le=\"+Inf\"} 2"));
}

TEST(MetricsTest, UnmatchedRoutesShareOneLabel)
{
    Metrics m("test");
    m.observe_http_request("GET", "", 404, 0.001);
    m.observe_http_request("GET", "", 404, 0.001);

    const auto out = m.serialize();
    EXPECT_TRUE(contains(
        out, "http_requests_total{endpoint=\"unmatched\",method=\"GET\",status=\"404\"} 2"))
        << out;
}

TEST(MetricsTest, CatchAllRoutePatternsCountAsUnmatched)
{
    using corvus::metrics::endpoint_label;
    EXPECT_EQ(endpoint_label(""), "unmatched");
    EXPECT_EQ(endpoint_label("/{a}"), "unmatched");
    EXPECT_EQ(endpoint_label("/{a}/{b}/{c}"), "unmatched");
    EXPECT_EQ(endpoint_label("/health"), "/health");
    EXPECT_EQ(endpoint_label("/v1/resources/{id}"), "/v1/resources/{id}");
}

TEST(MetricsTest, UnknownMethodsCollapseToOther)
{
    EXPECT_EQ(corvus::metrics::normalize_method("GET"), "GET");
    EXPECT_EQ(corvus::metrics::normalize_method("PATCH"), "PATCH");
    EXPECT_EQ(corvus::metrics::normalize_method("BREW"), "OTHER");
    EXPECT_EQ(corvus::metrics::normalize_method(""), "OTHER");
}

TEST(MetricsTest, NegativeDurationIsClampedToZero)
{
    Metrics m("test");
    m.observe_http_request("GET", "/health", 200, -5.0);
    EXPECT_TRUE(contains(
        m.serialize(),
        "http_request_duration_seconds_sum{endpoint=\"/health\",method=\"GET\",status=\"200\"} 0"));
}