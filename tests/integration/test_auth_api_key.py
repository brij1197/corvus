import time

from conftest import make_jwt, redis_key, sha256_hex

KEY_CACHE_TTL_SECONDS = 60


def insert_key(pg_conn, client_id, *, active=True, expires_sql="NULL", scopes="read"):
    raw = f"itest-raw-{time.time_ns()}"
    cur = pg_conn.cursor()
    cur.execute(
        "INSERT INTO api_keys (client_id, key_hash, scopes, is_active, expires_at) "
        f"VALUES (%s, %s, %s, %s, {expires_sql})",
        (client_id, sha256_hex(raw), scopes, active),
    )
    cur.close()
    return raw


class TestNoCredentials:
    def test_no_credentials_returns_401(self, api):
        assert api.get("/v1/anything").status_code == 401

    def test_message_names_both_options(self, api):
        message = api.get("/v1/anything").json()["error"]["message"]
        assert "X-Api-Key" in message
        assert "Authorization" in message

    def test_empty_api_key_counts_as_missing(self, api):
        resp = api.get("/v1/anything", headers={"X-Api-Key": ""})
        assert resp.status_code == 401

    def test_data_is_null(self, api):
        assert api.get("/v1/anything").json()["data"] is None


class TestApiKeyInvalid:
    def test_unknown_api_key_returns_401(self, api):
        resp = api.get("/v1/anything", headers={"X-Api-Key": "completely-unknown-key"})
        assert resp.status_code == 401
        assert resp.json()["error"]["code"] == "UNAUTHORIZED"

    def test_unknown_api_key_with_client_bound_jwt_still_401(self, api, reader):
        """A presented credential is never silently ignored."""
        resp = api.get(
            "/v1/anything",
            headers={**reader.jwt_headers, "X-Api-Key": "completely-unknown-key"},
        )
        assert resp.status_code == 401

    def test_inactive_key_returns_401(self, api, reader, pg_conn):
        raw = insert_key(pg_conn, reader.client_id, active=False)
        assert api.get("/v1/anything", headers={"X-Api-Key": raw}).status_code == 401

    def test_expired_key_returns_401(self, api, reader, pg_conn):
        raw = insert_key(
            pg_conn, reader.client_id, expires_sql="now() - interval '1 minute'"
        )
        assert api.get("/v1/anything", headers={"X-Api-Key": raw}).status_code == 401

    def test_redis_entry_without_a_postgres_row_is_not_trusted(self, api, redis):
        """Redis is a cache of Postgres, not a second place keys can come from."""
        raw = f"redis-only-{time.time_ns()}"
        redis.hset(redis_key(raw), mapping={"client_id": "nobody", "scopes": "read"})
        try:
            assert (
                api.get("/v1/anything", headers={"X-Api-Key": raw}).status_code == 401
            )
        finally:
            redis.delete(redis_key(raw))


class TestApiKeyRevocation:
    def test_key_is_cached_for_at_most_the_cache_ttl(self, api, reader, redis):
        api.get("/v1/anything", headers=reader.key_headers)
        ttl = redis.ttl(redis_key(reader.api_key))
        assert 0 < ttl <= KEY_CACHE_TTL_SECONDS

    def test_cache_ttl_never_outlives_the_key(self, api, reader, pg_conn, redis):
        raw = insert_key(
            pg_conn, reader.client_id, expires_sql="now() + interval '10 seconds'"
        )
        assert api.get("/v1/anything", headers={"X-Api-Key": raw}).status_code == 404
        assert 0 < redis.ttl(redis_key(raw)) <= 10

    def test_revoked_key_stops_working_once_its_cache_entry_expires(
        self, api, reader, pg_conn, redis
    ):
        assert api.get("/v1/anything", headers=reader.key_headers).status_code == 404

        cur = pg_conn.cursor()
        cur.execute(
            "UPDATE api_keys SET is_active = false WHERE key_hash = %s",
            (sha256_hex(reader.api_key),),
        )
        cur.close()
        redis.delete(redis_key(reader.api_key))

        assert api.get("/v1/anything", headers=reader.key_headers).status_code == 401


class TestApiKeyValid:
    def test_api_key_alone_passes_auth(self, api, reader):
        resp = api.get("/v1/anything", headers=reader.key_headers)
        assert resp.status_code == 404
        assert resp.json()["error"]["code"] == "NOT_FOUND"

    def test_key_known_only_to_postgres_works_on_first_use(self, api, reader, redis):
        assert not redis.exists(redis_key(reader.api_key))
        assert api.get("/v1/anything", headers=reader.key_headers).status_code == 404
        assert redis.exists(redis_key(reader.api_key))

    def test_response_has_envelope(self, api, reader):
        body = api.get("/v1/anything", headers=reader.key_headers).json()
        assert {"data", "error", "meta"} <= body.keys()

    def test_request_id_consistent(self, api, reader):
        resp = api.get("/v1/anything", headers=reader.key_headers)
        assert resp.headers["x-request-id"] == resp.json()["meta"]["request_id"]

    def test_key_with_matching_jwt_passes(self, api, reader):
        resp = api.get("/v1/anything", headers=reader.headers)
        assert resp.status_code == 404

    def test_key_with_jwt_for_another_client_returns_401(self, api, reader):
        token = make_jwt(client_id="someone-else", scope="read write")
        resp = api.get(
            "/v1/anything",
            headers={**reader.key_headers, "Authorization": f"Bearer {token}"},
        )
        assert resp.status_code == 401

    def test_api_key_not_required_on_health(self, api):
        assert api.get("/health").status_code == 200


class TestApiKeyScopes:
    def test_read_only_key_cannot_write(self, api, reader):
        resp = api.post("/v1/anything", headers=reader.key_headers, json={})
        assert resp.status_code == 403
        body = resp.json()
        assert body["error"]["code"] == "FORBIDDEN"
        assert "write" in body["error"]["message"]

    def test_read_only_key_cannot_delete(self, api, reader):
        resp = api.delete("/v1/anything", headers=reader.key_headers)
        assert resp.status_code == 403
