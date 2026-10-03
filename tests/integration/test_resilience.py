import json
import time
from concurrent.futures import ThreadPoolExecutor
from contextlib import contextmanager

import pytest

RESOURCES = "/v1/resources"

CACHE_TTL_SECONDS = 60

RATE_LIMIT_REFILL_SECONDS = 10


def create_resource(api, tenant, name):
    resp = api.post(
        RESOURCES, headers=tenant.headers, json={"kind": "server", "name": name}
    )
    assert resp.status_code == 201, resp.text
    return resp.json()["data"]


def url_for(resource):
    return f"{RESOURCES}/{resource['id']}"


def cache_key(tenant, resource):
    return f"corvus:resource:{tenant.client_id}:{resource['id']}"


@pytest.fixture
def full_rate_bucket():
    """Let the shared rate-limit bucket refill before a burst of requests."""
    time.sleep(RATE_LIMIT_REFILL_SECONDS)


@contextmanager
def cache_commands_fail(redis):
    """Make every cache command fail while API-key lookups keep working."""
    redis.execute_command("ACL", "SETUSER", "default", "resetkeys", "~corvus:apikeys:*")
    try:
        yield
    finally:
        redis.execute_command("ACL", "SETUSER", "default", "allkeys")


class TestParallelReads:
    def test_parallel_gets_never_cross_wire_responses(
        self, api, client, tenant, other_tenant, full_rate_bucket
    ):
        """
        Every Drogon worker shares one Redis connection. If access to it isn't
        serialized, one request can read another's reply, i.e. a different
        resource, possibly another tenant's. Fire parallel GETs across two
        tenants and check every body belongs to the request that asked for it.
        """
        owned = [
            (t, create_resource(api, t, f"par-{t.client_id[:8]}-{i}"))
            for t in (tenant, other_tenant)
            for i in range(3)
        ]
        for t, r in owned:
            assert api.get(url_for(r), headers=t.headers).status_code == 200

        jobs = [owned[i % len(owned)] for i in range(72)]

        def fetch(job):
            t, r = job
            return t, r, client.get(url_for(r), headers=t.headers)

        with ThreadPoolExecutor(max_workers=8) as pool:
            results = list(pool.map(fetch, jobs))

        served = [(t, r, resp) for t, r, resp in results if resp.status_code != 429]
        assert len(served) >= 60, "too many requests were rate limited to be meaningful"

        for t, r, resp in served:
            assert resp.status_code == 200, resp.text
            data = resp.json()["data"]
            assert data["id"] == r["id"]
            assert data["name"] == r["name"]
            assert data["client_id"] == t.client_id


class TestReadsDuringWrites:
    def test_cache_never_keeps_the_pre_patch_body(
        self, api, client, tenant, redis, full_rate_bucket
    ):
        """
        A GET that read the old row before a PATCH committed must not write it
        back into the cache after the PATCH evicted it.

        Each round fires four GETs alongside one PATCH, then checks that the
        cache holds either nothing or the new body. Hitting the window is
        probabilistic, so a pass on one run is evidence, not proof; a failure
        is always a real bug.
        """
        resource = create_resource(api, tenant, "rw-0")
        url = url_for(resource)
        key = cache_key(tenant, resource)

        def read():
            return client.get(url, headers=tenant.headers).status_code

        def write(name):
            return api.patch(url, headers=tenant.headers, json={"name": name})

        for i in range(1, 13):
            name = f"rw-{i}"
            with ThreadPoolExecutor(max_workers=5) as pool:
                reads = [pool.submit(read) for _ in range(4)]
                patched = pool.submit(write, name).result()
                statuses = [f.result() for f in reads]

            assert patched.status_code == 200, patched.text
            assert all(s in (200, 429) for s in statuses), statuses

            cached = redis.get(key)
            if cached is not None:
                assert json.loads(cached)["name"] == name, f"stale cache in round {i}"

            current = api.get(url, headers=tenant.headers).json()["data"]
            assert current["name"] == name


class TestCacheCommandsFailing:
    def test_get_is_served_from_postgres(self, api, tenant, redis):
        resource = create_resource(api, tenant, "down-get")

        with cache_commands_fail(redis):
            for _ in range(2):
                resp = api.get(url_for(resource), headers=tenant.headers)
                assert resp.status_code == 200, resp.text
                assert resp.json()["data"]["name"] == "down-get"

    def test_patch_succeeds_when_eviction_fails(self, api, tenant, redis):
        """The write is committed, so the client must hear success, not a 500."""
        resource = create_resource(api, tenant, "down-patch")
        api.get(url_for(resource), headers=tenant.headers)

        with cache_commands_fail(redis):
            resp = api.patch(
                url_for(resource), headers=tenant.headers, json={"status": "active"}
            )

        assert resp.status_code == 200, resp.text
        assert resp.json()["data"]["status"] == "active"

    def test_delete_succeeds_when_eviction_fails(self, api, tenant, redis):
        resource = create_resource(api, tenant, "down-delete")
        api.get(url_for(resource), headers=tenant.headers)

        with cache_commands_fail(redis):
            resp = api.delete(url_for(resource), headers=tenant.headers)

        assert resp.status_code == 200, resp.text

    def test_entry_left_by_failed_eviction_expires_within_ttl(self, api, tenant, redis):
        """
        Known limit: if eviction fails, the old entry survives the write.
        What bounds the damage is the TTL, so it must always be set.
        """
        resource = create_resource(api, tenant, "down-ttl")
        key = cache_key(tenant, resource)
        api.get(url_for(resource), headers=tenant.headers)
        assert redis.exists(key)

        with cache_commands_fail(redis):
            api.patch(url_for(resource), headers=tenant.headers, json={"status": "x"})

        ttl = redis.ttl(key)
        assert 0 < ttl <= CACHE_TTL_SECONDS


class TestCacheConnectionDropped:
    def test_cache_reconnects_after_its_connection_is_killed(self, api, tenant, redis):
        """
        Kill corvus-core's long-lived cache connection server-side. Requests
        must keep succeeding, and caching must resume instead of staying
        silently disabled until the process restarts.
        """
        resource = create_resource(api, tenant, "reconnect")
        key = cache_key(tenant, resource)
        redis.delete(key)

        redis.execute_command("CLIENT", "KILL", "TYPE", "normal")
        redis.connection_pool.disconnect()

        for _ in range(5):
            resp = api.get(url_for(resource), headers=tenant.headers)
            assert resp.status_code == 200, resp.text
            if redis.exists(key):
                break
            time.sleep(0.2)

        assert redis.exists(key), "cache did not repopulate after reconnect"
