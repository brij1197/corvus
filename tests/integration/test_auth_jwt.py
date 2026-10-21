import pytest
import httpx
from conftest import make_jwt, make_expired_jwt


class TestJwtMissingHeader:
    def test_missing_auth_header_returns_401(self, api):
        resp = api.get("/v1/anything")
        assert resp.status_code == 401

    def test_missing_auth_header_error_code(self, api):
        body = api.get("/v1/anything").json()
        assert body["error"]["code"] == "UNAUTHORIZED"

    def test_missing_auth_header_error_message(self, api):
        body = api.get("/v1/anything").json()
        assert "Authorization" in body["error"]["message"]

    def test_missing_auth_header_has_request_id(self, api):
        body = api.get("/v1/anything").json()
        assert len(body["meta"]["request_id"]) == 36

    def test_missing_auth_header_x_request_id_in_response(self, api):
        resp = api.get("/v1/anything")
        assert "x-request-id" in resp.headers

    def test_missing_auth_header_data_is_null(self, api):
        body = api.get("/v1/anything").json()
        assert body["data"] is None


class TestJwtMalformedHeader:
    def test_malformed_bearer_returns_401(self, api):
        resp = api.get("/v1/anything", headers={"Authorization": "notbearer token"})
        assert resp.status_code == 401

    def test_no_bearer_prefix_returns_401(self, api):
        resp = api.get("/v1/anything", headers={"Authorization": "sometoken"})
        assert resp.status_code == 401

    def test_empty_bearer_returns_401(self, api):
        resp = api.get("/v1/anything", headers={"Authorization": "Bearer x"})
        assert resp.status_code == 401


class TestJwtInvalidToken:
    def test_garbage_token_returns_401(self, api):
        resp = api.get(
            "/v1/anything", headers={"Authorization": "Bearer notavalidtoken"}
        )
        assert resp.status_code == 401

    def test_garbage_token_error_code(self, api):
        body = api.get(
            "/v1/anything", headers={"Authorization": "Bearer notavalidtoken"}
        ).json()
        assert body["error"]["code"] == "UNAUTHORIZED"

    def test_expired_token_returns_401(self, api, expired_jwt):
        resp = api.get(
            "/v1/anything", headers={"Authorization": f"Bearer {expired_jwt}"}
        )
        assert resp.status_code == 401

    def test_expired_token_error_message_mentions_token(self, api, expired_jwt):
        body = api.get(
            "/v1/anything", headers={"Authorization": f"Bearer {expired_jwt}"}
        ).json()
        assert body["error"]["message"] != ""

    def test_wrong_algorithm_token_returns_401(self, api):
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
        resp = api.get("/v1/anything", headers={"Authorization": f"Bearer {hs_token}"})
        assert resp.status_code == 401


class TestJwtValidToken:
    def test_token_without_client_id_returns_401(self, api, valid_jwt):
        """A token that names no client authorizes nothing."""
        resp = api.get("/v1/anything", headers={"Authorization": f"Bearer {valid_jwt}"})
        assert resp.status_code == 401
        assert "client_id" in resp.json()["error"]["message"]

    def test_client_bound_token_passes_auth(self, api):
        token = make_jwt(client_id="jwt-only-client", scope="read")
        resp = api.get("/v1/anything", headers={"Authorization": f"Bearer {token}"})
        # Auth passes; the route itself doesn't exist.
        assert resp.status_code == 404
        assert resp.json()["error"]["code"] == "NOT_FOUND"

    def test_token_without_write_scope_cannot_write(self, api):
        token = make_jwt(client_id="jwt-only-client", scope="read")
        resp = api.post(
            "/v1/anything", headers={"Authorization": f"Bearer {token}"}, json={}
        )
        assert resp.status_code == 403
        assert resp.json()["error"]["code"] == "FORBIDDEN"

    def test_token_with_no_scope_cannot_read(self, api):
        token = make_jwt(client_id="jwt-only-client")
        resp = api.get("/v1/anything", headers={"Authorization": f"Bearer {token}"})
        assert resp.status_code == 403

    def test_valid_token_not_rate_limited_on_health(self, api, valid_jwt):
        """Health endpoint ignores JWT entirely."""
        resp = api.get("/health", headers={"Authorization": f"Bearer {valid_jwt}"})
        assert resp.status_code == 200

    def test_jwt_not_checked_on_health(self, api):
        """Health endpoint requires no auth."""
        resp = api.get("/health")
        assert resp.status_code == 200

    def test_jwt_not_checked_on_ready(self, api):
        """Ready endpoint requires no auth."""
        resp = api.get("/ready")
        assert resp.status_code == 200
