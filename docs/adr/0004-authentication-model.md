# ADR-0004: Either an API key or a client-bound JWT

**Date:** 2026-10-12

## Context

Every `/v1/*` request had to carry both a JWT and an API key. Only the API key
middleware set `client_id`; the JWT middleware verified the signature and
discarded the claims. The JWT was a second hurdle that authorized nothing, and
scopes stored with each API key were parsed but never enforced.

## Decision

A request authenticates with **either** credential, or both:

| Sent                    | Tenant comes from       | Scopes                              |
| ----------------------- | ----------------------- | ----------------------------------- |
| `X-Api-Key`             | the key's `client_id`   | the key's scopes                    |
| `Authorization: Bearer` | the `client_id` claim   | the `scope` claim (space-separated) |
| both                    | must be the same client | only scopes both grant              |

Rules:

- A JWT without a `client_id` claim is rejected (401). A token must say which
  tenant it acts for.
- Every credential that is sent must be valid. A bad JWT next to a good key is
  a 401, not ignored.
- Key and token naming different clients is a 401.
- `GET`, `HEAD`, `OPTIONS` need `read`; every other method needs `write`.
  Missing scope is a 403.
- If the key store can't be consulted, the response is 503 with `Retry-After`.
  The decision logic lives in `auth::authenticate()`, a pure function with
  injected credential checks, so every rule above is unit-tested without Redis
  or signing keys. The Drogon middleware only adapts it to HTTP.

## Consequences

- Services use API keys; users or other services use JWTs minted by a trusted
  issuer with `client_id` and `scope` claims.
- Clients that sent both credentials keep working, provided the token now
  carries the matching `client_id`.

## Addendum: where API keys live (2026-10-18)

Postgres `api_keys` is the source of truth. Redis caches validated keys:

- On a cache miss the key is looked up by hash, and only rows that are
  `is_active` and not past `expires_at` count. A hit is cached for at most 60 s,
  and never past the key's own expiry.
- Revoking a key (`is_active = false`) or letting it expire takes effect within
  that TTL, with no Redis cleanup needed.
- Cache entries carry a `src=pg` marker; entries without it (seeded by hand or
  left over) are ignored, so Redis can't authenticate a key Postgres doesn't know.
- If Redis fails, keys are checked against Postgres directly. Only a Postgres
  failure returns 503.
- Redis connections are pooled (up to 8 idle) instead of opened per request.