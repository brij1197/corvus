"""
Shared fixtures for Corvus integration tests.

Requires:
  CORVUS_TEST_PRIVATE_KEY  - RSA private key PEM (for signing test JWTs)
  CORVUS_TEST_PUBLIC_KEY   - RSA public key PEM  (informational, not used here)
  CORVUS_BASE_URL          - defaults to http://localhost:8080
  CORVUS_REDIS_HOST        - defaults to localhost
  CORVUS_REDIS_PORT        - defaults to 6379
"""

import os
import time
import hashlib
import pytest
import httpx
import redis as redis_client

from datetime import datetime, timedelta, timezone

try:
    import jwt as pyjwt
except ImportError:
    pyjwt = None

BASE_URL = os.environ.get("CORVUS_BASE_URL", "http://localhost:8080")
REDIS_HOST = os.environ.get("CORVUS_REDIS_HOST", "localhost")
REDIS_PORT = int(os.environ.get("CORVUS_REDIS_PORT", "6380"))
PRIVATE_KEY = os.environ.get("CORVUS_TEST_PRIVATE_KEY", "")


def make_jwt(
    subject: str = "test-user",
    issuer: str = "corvus-test",
    expires_in: int = 300,
    private_key: str | None = None,
    algorithm: str = "RS256",
) -> str:
    """Sign a JWT with the test private key."""
    if pyjwt is None:
        raise RuntimeError("PyJWT not installed")

    key = private_key or PRIVATE_KEY
    if not key:
        raise RuntimeError("CORVUS_TEST_PRIVATE_KEY is not set")

    now = datetime.now(tz=timezone.utc)
    payload = {
        "sub": subject,
        "iss": issuer,
        "iat": now,
        "exp": now + timedelta(seconds=expires_in),
    }
    return pyjwt.encode(payload, key, algorithm=algorithm)


def make_expired_jwt() -> str:
    """Return a JWT that expired 1 minute ago."""
    return make_jwt(expires_in=-60)


def sha256_hex(value: str) -> str:
    return hashlib.sha256(value.encode()).hexdigest()


def redis_key(api_key: str) -> str:
    return f"corvus:apikeys:{sha256_hex(api_key)}"


@pytest.fixture(scope="session")
def base_url() -> str:
    return BASE_URL


@pytest.fixture(scope="session")
def client() -> httpx.Client:
    with httpx.Client(base_url=BASE_URL, timeout=10.0) as client:
        yield client


@pytest.fixture(scope="session")
def redis() -> redis_client.Redis:
    r = redis_client.Redis(host=REDIS_HOST, port=REDIS_PORT, decode_responses=True)
    try:
        r.ping()
    except Exception as e:
        pytest.skip(f"Redis not reachable at {REDIS_HOST}:{REDIS_PORT} - {e}")
    return r


@pytest.fixture(scope="session")
def valid_jwt() -> str:
    return make_jwt()


@pytest.fixture(scope="session")
def expired_jwt() -> str:
    return make_expired_jwt()


@pytest.fixture
def api_key(redis) -> str:
    """Register a fresh API key in Redis and clean it up after the test."""
    raw_key = f"test-key-{time.time_ns()}"
    rkey = redis_key(raw_key)
    redis.hset(rkey, mapping={"client_id": "test-client", "scopes": "read"})
    yield raw_key
    redis.delete(rkey)


@pytest.fixture(scope="session", autouse=True)
def wait_for_server():
    """Block until corvus-core is healthy before running any test."""
    deadline = time.time() + 60
    while time.time() < deadline:
        try:
            resp = httpx.get(f"{BASE_URL}/health", timeout=2.0)
            if resp.status_code == 200:
                return
        except httpx.RequestError:
            pass
        time.sleep(1)
    pytest.fail(f"corvus-core did not become healthy within 60s at {BASE_URL}")
