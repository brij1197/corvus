import pytest
import httpx


class TestNotFoundEnvelope:
    def test_unknown_path_returns_404(self, client):
        resp = client.get("/nonexistent")
        assert resp.status_code == 404

    def test_unknown_nested_path_returns_404(self, client):
        resp = client.get("/a/b/c")
        assert resp.status_code == 404

    def test_404_has_json_content_type(self, client):
        resp = client.get("/nonexistent")
        assert "application/json" in resp.headers["content-type"]

    def test_404_envelope_shape(self, client):
        body = client.get("/nonexistent").json()
        assert "data" in body
        assert "error" in body
        assert "meta" in body

    def test_404_error_code_is_not_found(self, client):
        body = client.get("/nonexistent").json()
        assert body["error"]["code"] == "NOT_FOUND"

    def test_404_data_is_null(self, client):
        body = client.get("/nonexistent").json()
        assert body["data"] is None

    def test_404_has_request_id_in_meta(self, client):
        body = client.get("/nonexistent").json()
        assert len(body["meta"]["request_id"]) == 36

    def test_404_has_x_request_id_header(self, client):
        resp = client.get("/nonexistent")
        assert "x-request-id" in resp.headers

    def test_404_header_matches_body_request_id(self, client):
        resp = client.get("/nonexistent")
        assert resp.headers["x-request-id"] == resp.json()["meta"]["request_id"]

    def test_404_has_timestamp_in_meta(self, client):
        body = client.get("/nonexistent").json()
        assert "timestamp" in body["meta"]
        assert body["meta"]["timestamp"] != ""


class TestEnvelopeConsistency:
    """All error types must return consistent envelope shape."""

    def _assert_envelope(self, resp, expected_status: int, expected_code: str):
        assert resp.status_code == expected_status
        assert "application/json" in resp.headers["content-type"]
        body = resp.json()
        assert body["data"] is None
        assert body["error"]["code"] == expected_code
        assert len(body["meta"]["request_id"]) == 36
        assert "x-request-id" in resp.headers
        assert resp.headers["x-request-id"] == body["meta"]["request_id"]

    def test_401_envelope(self, client):
        self._assert_envelope(client.get("/v1/anything"), 401, "UNAUTHORIZED")

    def test_404_envelope(self, client):
        self._assert_envelope(client.get("/does-not-exist"), 404, "NOT_FOUND")

    def test_413_envelope(self, client):
        resp = client.post(
            "/v1/anything",
            content=b"x" * (1024 * 1024 + 1),
            headers={"Content-Type": "application/octet-stream", "Expect": ""},
        )
        assert resp.status_code == 413
        assert "application/json" in resp.headers["content-type"]
        body = resp.json()
        assert body["data"] is None
        assert body["error"]["code"] == "PAYLOAD_TOO_LARGE"
        assert len(body["meta"]["request_id"]) == 36
        assert body["meta"]["request_id"] != ""
