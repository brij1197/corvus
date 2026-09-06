"""
Integration tests - health and readiness endpoints.
These run against a live corvus-core instance.
"""

import pytest
import httpx


class TestHealth:
    def test_health_returns_200(self, client):
        resp = client.get("/health")
        assert resp.status_code == 200

    def test_health_content_type_is_json(self, client):
        resp = client.get("/health")
        assert "application/json" in resp.headers["content-type"]

    def test_health_envelope_shape(self, client):
        body = client.get("/health").json()
        assert "data" in body
        assert "error" in body
        assert "meta" in body
        assert body["error"] is None

    def test_health_data_status_is_ok(self, client):
        body = client.get("/health").json()
        assert body["data"]["status"] == "ok"

    def test_health_data_has_version(self, client):
        body = client.get("/health").json()
        assert "version" in body["data"]
        assert body["data"]["version"] != ""

    def test_health_meta_has_request_id(self, client):
        body = client.get("/health").json()
        assert "request_id" in body["meta"]
        assert len(body["meta"]["request_id"]) == 36

    def test_health_meta_has_timestamp(self, client):
        body = client.get("/health").json()
        assert "timestamp" in body["meta"]
        assert body["meta"]["timestamp"] != ""

    def test_health_response_has_x_request_id_header(self, client):
        resp = client.get("/health")
        assert "x-request-id" in resp.headers

    def test_health_x_request_id_matches_body(self, client):
        resp = client.get("/health")
        body = resp.json()
        assert resp.headers["x-request-id"] == body["meta"]["request_id"]

    def test_health_not_rate_limited(self, client):
        """Health endpoint must never return 429 regardless of request count."""
        for _ in range(20):
            resp = client.get("/health")
            assert resp.status_code == 200


class TestReady:
    def test_ready_returns_200(self, client):
        resp = client.get("/ready")
        assert resp.status_code == 200

    def test_ready_envelope_shape(self, client):
        body = client.get("/ready").json()
        assert body["error"] is None
        assert body["data"]["status"] == "ready"

    def test_ready_reports_dependency_checks(self, client):
        checks = client.get("/ready").json()["data"]["checks"]
        assert checks["postgres"] == "ok"
        assert checks["redis"] == "ok"

    def test_ready_x_request_id_header_present(self, client):
        resp = client.get("/ready")
        assert "x-request-id" in resp.headers


class TestCustomRequestId:
    def test_custom_request_id_is_echoed_in_header(self, client):
        resp = client.get("/health", headers={"X-Request-ID": "my-trace-id-abc"})
        assert resp.headers["x-request-id"] == "my-trace-id-abc"

    def test_custom_request_id_is_in_body(self, client):
        resp = client.get("/health", headers={"X-Request-ID": "my-trace-id-xyz"})
        assert resp.json()["meta"]["request_id"] == "my-trace-id-xyz"

    def test_each_request_gets_unique_id_when_none_provided(self, client):
        ids = {client.get("/health").json()["meta"]["request_id"] for _ in range(5)}
        assert len(ids) == 5