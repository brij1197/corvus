# ADR-0001: C++ core, Python operator layer

**Date:** 2026-05-02

## Context

Corvus requires both high-performance systems work (API gateway, resource
management, event bus, policy enforcement, database clients) and rapid,
ergonomic tooling (CLI, SDK, observability scripts, integration tests).
No single language serves both equally well.

## Decision

**C++20** owns everything below the REST API boundary:

- API gateway and all middleware
- Resource manager, event bus, policy engine
- All database and cache clients (libpqxx, hiredis, prometheus-cpp)
- Unit tests (Google Test)

**Python 3.12** owns everything above the REST API boundary:

- CLI (`corvus-ctl`) via Typer + httpx
- Python SDK via httpx + pydantic
- Integration and end-to-end tests via pytest
- Observability scripts and CI automation

The two layers communicate exclusively over the versioned REST API (`/v1/`).

## Alternatives considered

1. **Pure C++** - CLI and test tooling in C++ is slower to iterate on.
2. **Pure Python** - loses memory safety and systems-level control.
3. **Go for operator layer** - valid choice, Python chosen for ecosystem depth.

## Consequences

- REST API is the hard contract boundary; breaking changes require `/v2/`.
- C++ handles all performance-critical paths; Python never needs to be fast.
- Both codebases are independently deployable and testable.
