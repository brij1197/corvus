import re

RESOURCES = "/v1/resources"


def scrape(client) -> str:
    resp = client.get("/metrics")
    assert resp.status_code == 200, resp.text
    return resp.text


def sample(text: str, name: str, **labels) -> float:
    """Value of one series, or 0.0 if it hasn't been recorded yet."""
    for line in text.splitlines():
        if not line.startswith(name + "{"):
            continue
        found = dict(re.findall(r'(\w+)="([^"]*)"', line))
        if all(found.get(k) == str(v) for k, v in labels.items()):
            return float(line.rsplit(" ", 1)[1])
    return 0.0


class TestMetricsEndpoint:
    def test_needs_no_credentials(self, client):
        assert client.get("/metrics").status_code == 200

    def test_is_prometheus_text_format(self, client):
        resp = client.get("/metrics")
        assert resp.headers["content-type"].startswith("text/plain; version=0.0.4")
        assert "# TYPE http_requests_total counter" in resp.text
        assert "# TYPE http_request_duration_seconds histogram" in resp.text

    def test_exposes_expected_metric_names(self, client):
        text = scrape(client)
        for name in (
            "http_requests_total",
            "http_request_duration_seconds_bucket",
            "http_request_duration_seconds_sum",
            "http_request_duration_seconds_count",
            "corvus_build_info",
        ):
            assert re.search(rf"^{name}{{", text, re.MULTILINE), f"{name} missing"

    def test_is_not_rate_limited(self, client):
        for _ in range(150):  # the bucket holds 100
            assert client.get("/metrics").status_code == 200


class TestHttpRequestMetrics:
    def test_counts_requests_by_route_method_and_status(self, api, client):
        labels = {"endpoint": "/health", "method": "GET", "status": "200"}
        before = sample(scrape(client), "http_requests_total", **labels)

        for _ in range(3):
            api.get("/health")

        after = sample(scrape(client), "http_requests_total", **labels)
        assert after - before == 3

    def test_endpoint_label_is_the_route_pattern_not_the_url(self, api, client, tenant):
        created = [
            api.post(
                RESOURCES,
                headers=tenant.headers,
                json={"kind": "server", "name": f"m-{i}"},
            ).json()["data"]["id"]
            for i in range(2)
        ]
        for rid in created:
            assert (
                api.get(f"{RESOURCES}/{rid}", headers=tenant.headers).status_code == 200
            )

        text = scrape(client)
        for rid in created:
            assert rid not in text, "a resource id leaked into a metric label"
        assert (
            sample(
                text,
                "http_requests_total",
                endpoint="/v1/resources/{id}",
                method="GET",
                status="200",
            )
            >= 2
        )

    def test_requests_rejected_before_routing_are_counted(self, api, client):
        """A 401 from the auth middleware never reaches a handler but must still show up."""
        labels = {"endpoint": "/v1/resources", "method": "GET", "status": "401"}
        before = sample(scrape(client), "http_requests_total", **labels)

        assert api.get(RESOURCES).status_code == 401

        assert sample(scrape(client), "http_requests_total", **labels) - before == 1

    def test_unknown_urls_share_one_series(self, api, client):
        labels = {"endpoint": "unmatched", "status": "404"}
        before = sample(scrape(client), "http_requests_total", **labels)

        for i in range(3):
            assert api.get(f"/no-such-page-{i}").status_code == 404

        text = scrape(client)
        assert "no-such-page" not in text
        assert sample(text, "http_requests_total", **labels) - before == 3

    def test_duration_histogram_counts_match_request_counts(self, api, client):
        api.get("/health")
        text = scrape(client)
        labels = {"endpoint": "/health", "method": "GET", "status": "200"}
        assert sample(text, "http_request_duration_seconds_count", **labels) == sample(
            text, "http_requests_total", **labels
        )
