# ADR-0003: TimescaleDB for audit log storage

**Date:** 2026-06-26

## Context

The `audit_log` table is the canonical record of every authenticated request
Corvus handles: HTTP method, path, status code, latency, client identity,
request ID, and per-request metadata. Two properties make it unlike the other
relational tables in the schema:

- **Write-heavy and append-only.** Every API request produces one row. At a
  modest 50 requests per second the table accumulates ~4.3M rows per day and
  ~130M rows per month. Rows are inserted in (roughly) chronological order and
  almost never updated.
- **Time-bounded read patterns.** Queries against `audit_log` are virtually
  always scoped by time: "what happened in the last hour", "show me errors for
  client X this week", "compute p95 latency over the last 24 hours". Queries
  spanning years are rare and usually for compliance exports.

The combination - high write throughput, time-ordered access, and the need to
drop old data periodically - is the canonical fit for a time-series store. A
plain PostgreSQL table grows monotonically, vacuum cost rises with table size,
btree indexes on `occurred_at` become deep, and pruning old rows requires
either expensive `DELETE` batches or partitioning the table manually.

Key requirements:

- Automatic partitioning by `occurred_at` so that time-range queries scan only
  the relevant partitions.
- Cheap deletion of old data - drop entire partitions, not row-by-row deletes.
- Configurable retention policy that runs without operator intervention.
- Foreign keys from `audit_log.client_id` to `clients.id` must still work so
  audit rows are joinable with the relational data.
- Same connection pool, same migration runner, same operational model as the
  rest of the schema. A separate storage engine (ClickHouse, InfluxDB) would
  double the surface area.
- Open-source license that allows commercial use without ambiguity.

## Decision

**TimescaleDB 2.17.2** (PostgreSQL extension) running on top of PostgreSQL 16
is the storage backend for `audit_log`. The same database instance hosts both
the relational tables (`clients`, `api_keys`, `resources`, `policies`,
`policy_attachments`) and the `audit_log` hypertable.

Configuration:

- The `postgres` service in `docker-compose.yml` uses the
  `timescale/timescaledb:2.17.2-pg16` image, with
  `shared_preload_libraries=timescaledb` set in the postgres command.
- Migration 0001 creates `audit_log` with composite primary key
  `(id, occurred_at)` - TimescaleDB requires the partitioning column to be
  part of every unique constraint, so this is set up at table creation time
  rather than altered later.
- Migration 0002 enables the `timescaledb` extension, converts `audit_log`
  into a hypertable partitioned by `occurred_at` with a 7-day
  `chunk_time_interval`, and adds a retention policy that drops chunks older
  than 90 days. All three statements use `if_not_exists => TRUE` so the
  migration is idempotent.
- The retention policy runs as a TimescaleDB background job on a daily
  schedule. No application-level cron is required.

The audit_log row schema is otherwise a standard PostgreSQL table: standard
columns, FK to `clients`, JSONB `metadata`, btree indexes. Application code
inserts and queries `audit_log` exactly the way it would a plain table; the
hypertable behaviour is transparent to the C++ side.

## Alternatives considered

**1. Plain PostgreSQL table with manual partitioning**
PostgreSQL 11+ supports native declarative partitioning (`PARTITION BY RANGE
(occurred_at)`). This would eliminate the TimescaleDB dependency but require
the application to create new partitions on a schedule, manage partition
naming, attach/detach partitions on retention, and handle the boundary cases
when a write arrives for a partition that has not yet been created. The
operational burden is non-trivial and easy to get wrong. TimescaleDB does all
of this automatically. Rejected.

**2. PostgreSQL table without partitioning**
Storing the full audit log in a single unpartitioned table is the simplest
option and would work for a long time at low traffic. However, it leaves
no clean answer for retention - `DELETE FROM audit_log WHERE occurred_at < ...`
is expensive on a large table, generates significant WAL traffic, and leaves
dead tuples that vacuum must reclaim. Time-range queries also slow down as the
table grows. Rejected.

**3. ClickHouse**
ClickHouse is purpose-built for analytical workloads over event data and would
outperform TimescaleDB on aggregate queries by a meaningful margin. The cost
is a second storage engine in the deployment: separate connection driver,
separate operational tooling, separate backup story, no foreign keys to
relational tables, and a SQL dialect that diverges from PostgreSQL in several
places. The audit log query volume Corvus expects does not justify that
overhead. Reconsider if audit analytics become a primary use case. Rejected.

**4. InfluxDB / Prometheus remote storage**
Tools designed primarily for numeric metrics. The audit log is structured
text-and-metadata events with foreign keys, not metric samples. Rejected.

**5. S3 + Parquet (object storage)**
A cheap long-term archive but unsuitable for the access patterns the audit
log needs to support - operators frequently query the recent past
interactively. Object storage could complement TimescaleDB for archive of
chunks older than the retention window, but that is a future optimisation,
not a substitute. Out of scope for now.

**6. Two separate databases (one Postgres, one TimescaleDB)**
The original Epic 3 plan placed `audit_log` in a separate `corvus_ts`
database. Rejected during implementation because it broke the foreign key
from `audit_log.client_id` to `clients.id` (foreign keys cannot cross
databases in PostgreSQL) and doubled the connection pool, migration runner,
and operational surface for no offsetting benefit. A single TimescaleDB-
enabled PostgreSQL instance hosts both with no compromise.

## Consequences

- **Single database, single connection pool.** The C++ `PgPool`,
  `MigrationRunner`, and integration tests work against `audit_log` identically
  to any other table. No second driver, no second schema migration path.
- **Foreign keys preserved.** `audit_log.client_id → clients.id` with
  `ON DELETE SET NULL` works because both tables live in the same database.
  This was the deciding factor against the two-database design.
- **Chunk size is a tuning knob.** 7-day chunks were chosen as a reasonable
  default: large enough to avoid chunk-management overhead for low-traffic
  deployments, small enough that retention drops a meaningful slice of data at
  a time. At sustained high write rates the chunk interval should be reduced
  (1 day or hourly) to keep individual chunks below ~25% of available memory
  for efficient inserts. Revisit when production traffic data is available.
- **Retention is fire-and-forget.** The 90-day retention policy is enforced by
  a TimescaleDB background job that runs daily. The `policy_retention` job
  appears in `timescaledb_information.jobs` and can be inspected, paused, or
  reconfigured via SQL without restarting the application.
- **Composite primary key is permanent.** `(id, occurred_at)` is required for
  the hypertable conversion and cannot be relaxed later. Any unique constraint
  added to `audit_log` in future migrations must also include `occurred_at`.
- **License compatibility.** TimescaleDB's core (including hypertables,
  retention policies, and continuous aggregates) is licensed under the
  Apache 2.0 License. Some advanced features (data tiering to object storage,
  compression policy tuning) are under the Timescale License (TSL), which has
  use-case restrictions. The features Corvus uses today are all Apache 2.0;
  any future adoption of TSL features requires a license review.
- **Backups.** Standard `pg_dump` and PITR via WAL archiving work against a
  TimescaleDB database, but extension-aware tooling (`pg_dump` with
  `--format=custom` and the TimescaleDB-recommended pre/post hooks) is
  required to restore hypertables correctly. Backup procedure documentation
  is deferred to the operations runbook.
- **TimescaleDB version pinning.** The image tag `timescale/timescaledb:
2.17.2-pg16` is pinned in `docker-compose.yml`. Upgrades require running
  `ALTER EXTENSION timescaledb UPDATE;` against the running database, captured
  as a migration when the version bump is made.
