# ADR-0005: Blocking work runs on a worker pool, not Drogon's I/O threads

**Date:** 2026-10-21

## Context

libpqxx and hiredis are synchronous. Every Postgres and Redis call ran
directly on one of Drogon's 4 I/O threads, and each I/O thread also owns the
sockets of every connection assigned to it. Four slow queries were enough to
stop the server answering anything, `/health` included, and the database pool's
10 connections could never all be in use at once.

Reproduced locally: with `resources` locked by another transaction, six
`GET /v1/resources` requests left `/health` unanswered for more than 3 seconds.

## Decision

A fixed `gateway::BlockingExecutor` (10 threads, matching the Postgres pool)
runs all blocking work. I/O threads only parse, route and write responses.

- The auth middleware moves `/v1/*` requests onto a worker, since API-key
  checks hit Redis and Postgres. Routing then continues on that worker.
- Resource handlers and `/ready` go through `gateway::offload()`, which runs
  inline when already on a worker and otherwise queues the work.
- `/health` does no I/O and stays on the I/O threads.
- The queue is bounded at 256. When it is full the request gets an immediate
  503 with `Retry-After: 1` instead of waiting behind everything else.
- Drogon response callbacks are safe to call from any thread; the response is
  handed back to the connection's own event loop.

## Alternatives considered

- **Drogon's async `DbClient` with coroutines.** Fully non-blocking, but a
  rewrite of the repository layer and its tests, and hiredis would still need
  the same treatment. A worker pool fixes the failure with far less change.
- **More I/O threads.** Moves the threshold, doesn't remove it.

## Consequences

- A stuck database now exhausts workers, not the server: `/health` keeps
  answering, and `/ready` queues behind the workers, so an orchestrator sees
  the instance as not ready instead of dead.
- Worker count and queue bound are constants in `main.cpp`; they belong in
  configuration once there is a reason to tune them.