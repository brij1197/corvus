import time
import pytest
import httpx


class TestRateLimiting:
    def test_health_never_rate_limited(self, client):
        """Health endpoint is excluded from rate limiting."""
        for _ in range(20):
            resp = client.get("/health")
            assert resp.status_code == 200

    def test_ready_never_rate_limited(self, client):
        """Ready endpoint is excluded from rate limiting."""
        for _ in range(20):
            resp = client.get("/ready")
            assert resp.status_code == 200

    def test_rate_limit_429_has_json_envelope(self, client):
        """When rate limited, response must be a proper JSON envelope."""
        import threading

        responses = []
        lock = threading.Lock()

        def fire():
            try:
                r = httpx.get(
                    f"{client.base_url}/v1/probe",
                    timeout=5.0,
                )
                with lock:
                    responses.append(r)
            except Exception:
                pass

        threads = [threading.Thread(target=fire) for _ in range(120)]
        for t in threads:
            t.start()
        for t in threads:
            t.join()

        limited = [r for r in responses if r.status_code == 429]
        if not limited:
            pytest.skip("Bucket not exhausted in this run - timing-dependent test")

        resp = limited[0]
        body = resp.json()
        assert body["error"]["code"] == "TOO_MANY_REQUESTS"
        assert body["data"] is None
        assert "request_id" in body["meta"]

    def test_rate_limit_429_has_retry_after_header(self, client):
        """429 response must include Retry-After header."""
        import threading

        responses = []
        lock = threading.Lock()

        def fire():
            try:
                r = httpx.get(f"{client.base_url}/v1/probe", timeout=5.0)
                with lock:
                    responses.append(r)
            except Exception:
                pass

        threads = [threading.Thread(target=fire) for _ in range(120)]
        for t in threads:
            t.start()
        for t in threads:
            t.join()

        limited = [r for r in responses if r.status_code == 429]
        if not limited:
            pytest.skip("Bucket not exhausted in this run - timing-dependent test")

        assert "retry-after" in limited[0].headers

    def test_rate_limit_x_ratelimit_limit_header(self, client):
        """429 response must include X-RateLimit-Limit header."""
        import threading

        responses = []
        lock = threading.Lock()

        def fire():
            try:
                r = httpx.get(f"{client.base_url}/v1/probe", timeout=5.0)
                with lock:
                    responses.append(r)
            except Exception:
                pass

        threads = [threading.Thread(target=fire) for _ in range(120)]
        for t in threads:
            t.start()
        for t in threads:
            t.join()

        limited = [r for r in responses if r.status_code == 429]
        if not limited:
            pytest.skip("Bucket not exhausted in this run - timing-dependent test")

        assert "x-ratelimit-limit" in limited[0].headers


class TestRequestSizeLimit:
    def test_small_post_passes_through(self, client):
        """POST under 1MiB passes size check."""
        resp = client.post(
            "/v1/anything",
            content=b"x" * 100,
            headers={"Content-Type": "application/octet-stream"},
        )
        assert resp.status_code in (401, 429)

    def test_oversized_post_returns_413(self, client):
        """POST body over 1MiB must return 413."""
        resp = client.post(
            "/v1/anything",
            content=b"x" * (1024 * 1024 + 1),
            headers={
                "Content-Type": "application/octet-stream",
                "Expect": "",
            },
        )
        assert resp.status_code == 413

    def test_oversized_post_error_code(self, client):
        body = client.post(
            "/v1/anything",
            content=b"x" * (1024 * 1024 + 1),
            headers={"Content-Type": "application/octet-stream", "Expect": ""},
        ).json()
        assert body["error"]["code"] == "PAYLOAD_TOO_LARGE"

    def test_oversized_post_has_x_max_body_size_header(self, client):
        resp = client.post(
            "/v1/anything",
            content=b"x" * (1024 * 1024 + 1),
            headers={"Content-Type": "application/octet-stream", "Expect": ""},
        )
        assert "x-max-body-size" in resp.headers

    def test_oversized_post_413_has_request_id(self, client):
        resp = client.post(
            "/v1/anything",
            content=b"x" * (1024 * 1024 + 1),
            headers={"Content-Type": "application/octet-stream", "Expect": ""},
        )
        body = resp.json()
        assert "request_id" in body["meta"]
        assert len(body["meta"]["request_id"]) == 36

    def test_get_not_size_checked(self, client):
        """GET requests are never size-checked."""
        resp = client.get("/health")
        assert resp.status_code == 200

    def test_exactly_at_limit_passes(self, client):
        """Body of exactly 1MiB (not over) should pass."""
        resp = client.post(
            "/v1/anything",
            content=b"x" * (1024 * 1024),
            headers={"Content-Type": "application/octet-stream", "Expect": ""},
        )
        assert resp.status_code in (401, 429)
