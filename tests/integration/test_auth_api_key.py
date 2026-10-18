import time

from conftest import make_jwt, redis_key


class TestNoCredentials:
    def test_no_credentials_returns_401(self, client):
        assert client.get("/v1/anything").status_code == 401

    def test_message_names_both_options(self, client):
        message = client.get("/v1/anything").json()["error"]["message"]
        assert "X-Api-Key" in message
        assert "Authorization" in message

    def test_empty_api_key_counts_as_missing(self, client):
        resp = client.get("/v1/anything", headers={"X-Api-Key": ""})
        assert resp.status_code == 401

    def test_data_is_null(self, client):
        assert client.get("/v1/anything").json()["data"] is None


class TestApiKeyInvalid:
    def test_unknown_api_key_returns_401(self, client):
        resp = client.get(
            "/v1/anything", headers={"X-Api-Key": "completely-unknown-key"}
        )
        assert resp.status_code == 401

    def test_unknown_api_key_error_code(self, client):
        body = client.get(
            "/v1/anything", headers={"X-Api-Key": "completely-unknown-key"}
        ).json()
        assert body["error"]["code"] == "UNAUTHORIZED"

    def test_unknown_api_key_with_client_bound_jwt_still_401(self, client):
        """A presented credential is never silently ignored."""
        token = make_jwt(client_id="test-client", scope="read")
        resp = client.get(
            "/v1/anything",
            headers={
                "Authorization": f"Bearer {token}",
                "X-Api-Key": "completely-unknown-key",
            },
        )
        assert resp.status_code == 401

    def test_revoked_api_key_returns_401(self, client, redis):
        """Delete the key from Redis mid-test to simulate revocation."""
        raw_key = f"revoke-test-{time.time_ns()}"
        rkey = redis_key(raw_key)
        redis.hset(rkey, mapping={"client_id": "revoke-client", "scopes": "read"})

        assert (
            client.get("/v1/anything", headers={"X-Api-Key": raw_key}).status_code
            == 404
        )

        redis.delete(rkey)

        assert (
            client.get("/v1/anything", headers={"X-Api-Key": raw_key}).status_code
            == 401
        )


class TestApiKeyValid:
    def test_api_key_alone_passes_auth(self, client, api_key):
        resp = client.get("/v1/anything", headers={"X-Api-Key": api_key})
        # Auth passes; the route itself doesn't exist.
        assert resp.status_code == 404
        assert resp.json()["error"]["code"] == "NOT_FOUND"

    def test_response_has_envelope(self, client, api_key):
        body = client.get("/v1/anything", headers={"X-Api-Key": api_key}).json()
        assert {"data", "error", "meta"} <= body.keys()

    def test_request_id_consistent(self, client, api_key):
        resp = client.get("/v1/anything", headers={"X-Api-Key": api_key})
        assert resp.headers["x-request-id"] == resp.json()["meta"]["request_id"]

    def test_key_with_matching_jwt_passes(self, client, api_key):
        token = make_jwt(client_id="test-client", scope="read")
        resp = client.get(
            "/v1/anything",
            headers={"Authorization": f"Bearer {token}", "X-Api-Key": api_key},
        )
        assert resp.status_code == 404

    def test_key_with_jwt_for_another_client_returns_401(self, client, api_key):
        token = make_jwt(client_id="someone-else", scope="read write")
        resp = client.get(
            "/v1/anything",
            headers={"Authorization": f"Bearer {token}", "X-Api-Key": api_key},
        )
        assert resp.status_code == 401

    def test_api_key_not_required_on_health(self, client):
        assert client.get("/health").status_code == 200


class TestApiKeyScopes:
    def test_read_only_key_cannot_write(self, client, api_key):
        """The api_key fixture grants only "read"."""
        resp = client.post("/v1/anything", headers={"X-Api-Key": api_key}, json={})
        assert resp.status_code == 403
        body = resp.json()
        assert body["error"]["code"] == "FORBIDDEN"
        assert "write" in body["error"]["message"]

    def test_read_only_key_cannot_delete(self, client, api_key):
        resp = client.delete("/v1/anything", headers={"X-Api-Key": api_key})
        assert resp.status_code == 403
