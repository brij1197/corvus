BEGIN;
-- 1. Enable the TimescaleDB extension
CREATE EXTENSION IF NOT EXISTS timescaledb;
-- 2. Convert audit_log into a hypertable
--    chunk_time_interval = 7 days -> roughly one chunk per week
--    if_not_exists = TRUE makes the migration idempotent
--    migrate_data = TRUE handles any rows that already exist
SELECT create_hypertable(
        'audit_log',
        'occurred_at',
        chunk_time_interval => INTERVAL '7 days',
        if_not_exists => TRUE,
        migrate_data => TRUE
    );
-- 3. Retention policy - drop chunks older than 90 days
--    Re-running add_retention_policy with the same arguments is a no-op
--    when if_not_exists = TRUE
SELECT add_retention_policy(
        'audit_log',
        INTERVAL '90 days',
        if_not_exists => TRUE
    );
COMMIT;