import pytest
import httpx
from conftest import make_jwt


class TestApiKeyMissing:
    def test_missing_api_key_with_valid_jwt_returns_401(self, client, valid_jwt):
        resp = client.get(
            "/v1/anything",
            headers={"Authorization": f"Bearer {valid_jwt}"},
        )
        assert resp.status_code == 401

    def test_missing_api_key_error_code(self, client, valid_jwt):
        body = client.get(
            "/v1/anything",
            headers={"Authorization": f"Bearer {valid_jwt}"},
        ).json()
        assert body["error"]["code"] == "UNAUTHORIZED"

    def test_missing_api_key_mentions_header_name(self, client, valid_jwt):
        body = client.get(
            "/v1/anything",
            headers={"Authorization": f"Bearer {valid_jwt}"},
        ).json()
        assert "X-Api-Key" in body["error"]["message"]

    def test_missing_api_key_data_is_null(self, client, valid_jwt):
        body = client.get(
            "/v1/anything",
            headers={"Authorization": f"Bearer {valid_jwt}"},
        ).json()
        assert body["data"] is None


class TestApiKeyInvalid:
    def test_unknown_api_key_returns_401(self, client, valid_jwt):
        resp = client.get(
            "/v1/anything",
            headers={
                "Authorization": f"Bearer {valid_jwt}",
                "X-Api-Key": "completely-unknown-key",
            },
        )
        assert resp.status_code == 401

    def test_unknown_api_key_error_code(self, client, valid_jwt):
        body = client.get(
            "/v1/anything",
            headers={
                "Authorization": f"Bearer {valid_jwt}",
                "X-Api-Key": "completely-unknown-key",
            },
        ).json()
        assert body["error"]["code"] == "UNAUTHORIZED"

    def test_empty_api_key_returns_401(self, client, valid_jwt):
        resp = client.get(
            "/v1/anything",
            headers={
                "Authorization": f"Bearer {valid_jwt}",
                "X-Api-Key": "",
            },
        )
        assert resp.status_code == 401


class TestApiKeyValid:
    def test_valid_api_key_with_valid_jwt_passes_auth(self, client, valid_jwt, api_key):
        """Both JWT and API key valid."""
        resp = client.get(
            "/v1/anything",
            headers={
                "Authorization": f"Bearer {valid_jwt}",
                "X-Api-Key": api_key,
            },
        )
        # Auth passes, route doesn't exist
        assert resp.status_code == 404

    def test_valid_api_key_response_has_envelope(self, client, valid_jwt, api_key):
        body = client.get(
            "/v1/anything",
            headers={
                "Authorization": f"Bearer {valid_jwt}",
                "X-Api-Key": api_key,
            },
        ).json()
        assert "data" in body
        assert "error" in body
        assert "meta" in body

    def test_valid_api_key_404_error_code(self, client, valid_jwt, api_key):
        body = client.get(
            "/v1/anything",
            headers={
                "Authorization": f"Bearer {valid_jwt}",
                "X-Api-Key": api_key,
            },
        ).json()
        assert body["error"]["code"] == "NOT_FOUND"

    def test_valid_api_key_request_id_consistent(self, client, valid_jwt, api_key):
        resp = client.get(
            "/v1/anything",
            headers={
                "Authorization": f"Bearer {valid_jwt}",
                "X-Api-Key": api_key,
            },
        )
        assert resp.headers["x-request-id"] == resp.json()["meta"]["request_id"]

    def test_api_key_not_required_on_health(self, client):
        """Health endpoint requires no API key."""
        resp = client.get("/health")
        assert resp.status_code == 200

    def test_revoked_api_key_returns_401(self, client, valid_jwt, redis):
        """Delete the key from Redis mid-test to simulate revocation."""
        from conftest import redis_key, sha256_hex
        import time

        raw_key = f"revoke-test-{time.time_ns()}"
        rkey = redis_key(raw_key)
        redis.hset(rkey, mapping={"client_id": "revoke-client", "scopes": "read"})

        resp = client.get(
            "/v1/anything",
            headers={
                "Authorization": f"Bearer {valid_jwt}",
                "X-Api-Key": raw_key,
            },
        )
        assert resp.status_code == 404

        redis.delete(rkey)

        resp = client.get(
            "/v1/anything",
            headers={
                "Authorization": f"Bearer {valid_jwt}",
                "X-Api-Key": raw_key,
            },
        )
        assert resp.status_code == 401
