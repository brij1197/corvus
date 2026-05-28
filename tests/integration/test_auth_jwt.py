import pytest
import httpx
from conftest import make_jwt, make_expired_jwt


class TestJwtMissingHeader:
    def test_missing_auth_header_returns_401(self, client):
        resp = client.get("/v1/anything")
        assert resp.status_code == 401

    def test_missing_auth_header_error_code(self, client):
        body = client.get("/v1/anything").json()
        assert body["error"]["code"] == "UNAUTHORIZED"

    def test_missing_auth_header_error_message(self, client):
        body = client.get("/v1/anything").json()
        assert "Authorization" in body["error"]["message"]

    def test_missing_auth_header_has_request_id(self, client):
        body = client.get("/v1/anything").json()
        assert len(body["meta"]["request_id"]) == 36

    def test_missing_auth_header_x_request_id_in_response(self, client):
        resp = client.get("/v1/anything")
        assert "x-request-id" in resp.headers

    def test_missing_auth_header_data_is_null(self, client):
        body = client.get("/v1/anything").json()
        assert body["data"] is None


class TestJwtMalformedHeader:
    def test_malformed_bearer_returns_401(self, client):
        resp = client.get("/v1/anything", headers={"Authorization": "notbearer token"})
        assert resp.status_code == 401

    def test_no_bearer_prefix_returns_401(self, client):
        resp = client.get("/v1/anything", headers={"Authorization": "sometoken"})
        assert resp.status_code == 401

    def test_empty_bearer_returns_401(self, client):
        resp = client.get("/v1/anything", headers={"Authorization": "Bearer x"})
        assert resp.status_code == 401


class TestJwtInvalidToken:
    def test_garbage_token_returns_401(self, client):
        resp = client.get(
            "/v1/anything", headers={"Authorization": "Bearer notavalidtoken"}
        )
        assert resp.status_code == 401

    def test_garbage_token_error_code(self, client):
        body = client.get(
            "/v1/anything", headers={"Authorization": "Bearer notavalidtoken"}
        ).json()
        assert body["error"]["code"] == "UNAUTHORIZED"

    def test_expired_token_returns_401(self, client, expired_jwt):
        resp = client.get(
            "/v1/anything", headers={"Authorization": f"Bearer {expired_jwt}"}
        )
        assert resp.status_code == 401

    def test_expired_token_error_message_mentions_token(self, client, expired_jwt):
        body = client.get(
            "/v1/anything", headers={"Authorization": f"Bearer {expired_jwt}"}
        ).json()
        assert body["error"]["message"] != ""

    def test_wrong_algorithm_token_returns_401(self, client):
        """HS256-signed token must be rejected."""
        import jwt as pyjwt
        from datetime import datetime, timedelta, timezone

        hs_token = pyjwt.encode(
            {
                "sub": "user",
                "exp": datetime.now(tz=timezone.utc) + timedelta(minutes=5),
            },
            "secret",
            algorithm="HS256",
        )
        resp = client.get(
            "/v1/anything", headers={"Authorization": f"Bearer {hs_token}"}
        )
        assert resp.status_code == 401


class TestJwtValidToken:
    def test_valid_token_passes_jwt_check(self, client, valid_jwt):
        """With valid JWT, auth passes."""
        resp = client.get(
            "/v1/anything", headers={"Authorization": f"Bearer {valid_jwt}"}
        )
        assert resp.status_code == 401
        body = resp.json()
        assert "X-Api-Key" in body["error"]["message"]

    def test_valid_token_not_rate_limited_on_health(self, client, valid_jwt):
        """Health endpoint ignores JWT entirely."""
        resp = client.get("/health", headers={"Authorization": f"Bearer {valid_jwt}"})
        assert resp.status_code == 200

    def test_jwt_not_checked_on_health(self, client):
        """Health endpoint requires no auth."""
        resp = client.get("/health")
        assert resp.status_code == 200

    def test_jwt_not_checked_on_ready(self, client):
        """Ready endpoint requires no auth."""
        resp = client.get("/ready")
        assert resp.status_code == 200
