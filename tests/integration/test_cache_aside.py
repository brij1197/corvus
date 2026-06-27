import time
import uuid
import pytest

PREFIX = "corvus:test:"


@pytest.fixture
def cache_key():
    """Unique cache key per test."""
    return f"{PREFIX}{uuid.uuid4()}"


@pytest.fixture
def cleanup_cache_keys(redis):
    """Delete any keys matching corvus:test:* after the test."""
    yield
    for key in redis.scan_iter(match=f"{PREFIX}*"):
        redis.delete(key)


def test_redis_set_and_get(redis, cache_key, cleanup_cache_keys):
    redis.set(cache_key, "hello")
    assert redis.get(cache_key) == "hello"


def test_redis_get_missing_returns_none(redis):
    assert redis.get(f"{PREFIX}does-not-exist") is None


def test_set_with_ttl_persists_for_at_least_ttl(redis, cache_key, cleanup_cache_keys):
    redis.set(cache_key, "ephemeral", ex=10)
    ttl = redis.ttl(cache_key)
    assert 0 < ttl <= 10


def test_set_without_ttl_has_no_expiry(redis, cache_key, cleanup_cache_keys):
    redis.set(cache_key, "permanent")
    assert redis.ttl(cache_key) == -1


def test_short_ttl_key_expires(redis, cache_key, cleanup_cache_keys):
    redis.set(cache_key, "expires-fast", ex=1)
    assert redis.get(cache_key) == "expires-fast"
    time.sleep(1.5)
    assert redis.get(cache_key) is None


def test_exists_returns_one_for_present_key(redis, cache_key, cleanup_cache_keys):
    redis.set(cache_key, "x")
    assert redis.exists(cache_key) == 1


def test_exists_returns_zero_for_missing_key(redis):
    assert redis.exists(f"{PREFIX}missing-{uuid.uuid4()}") == 0


def test_delete_removes_key(redis, cache_key, cleanup_cache_keys):
    redis.set(cache_key, "delete-me")
    assert redis.delete(cache_key) == 1
    assert redis.exists(cache_key) == 0


def test_delete_returns_zero_for_missing(redis):
    assert redis.delete(f"{PREFIX}never-existed-{uuid.uuid4()}") == 0


def test_scan_finds_keys_by_pattern(redis, cleanup_cache_keys):
    scope = f"{PREFIX}scan-{uuid.uuid4()}-"
    for i in range(5):
        redis.set(f"{scope}{i}", str(i))

    found = list(redis.scan_iter(match=f"{scope}*"))
    assert len(found) == 5


def test_scan_then_delete_invalidates_prefix(redis, cleanup_cache_keys):
    scope = f"{PREFIX}bulk-{uuid.uuid4()}-"
    for i in range(10):
        redis.set(f"{scope}{i}", str(i))

    for key in redis.scan_iter(match=f"{scope}*", count=100):
        redis.delete(key)

    found = list(redis.scan_iter(match=f"{scope}*"))
    assert found == []


def test_hset_and_hget(redis, cache_key, cleanup_cache_keys):
    redis.hset(cache_key, "client_id", "client-abc")
    redis.hset(cache_key, "scopes", "read,write")

    assert redis.hget(cache_key, "client_id") == "client-abc"
    assert redis.hget(cache_key, "scopes") == "read,write"


def test_hgetall_returns_all_fields(redis, cache_key, cleanup_cache_keys):
    redis.hset(cache_key, mapping={"a": "1", "b": "2", "c": "3"})
    result = redis.hgetall(cache_key)
    assert result == {"a": "1", "b": "2", "c": "3"}


def test_hget_missing_field_returns_none(redis, cache_key, cleanup_cache_keys):
    redis.hset(cache_key, "exists", "yes")
    assert redis.hget(cache_key, "absent") is None
