import pytest

EXPECTED_TABLES = {
    "schema_migrations",
    "clients",
    "api_keys",
    "resources",
    "policies",
    "policy_attachments",
    "audit_log",
}


def test_schema_migrations_table_exists(pg_cursor):
    pg_cursor.execute("SELECT to_regclass('public.schema_migrations') IS NOT NULL")
    assert pg_cursor.fetchone()[0] is True


def test_both_migrations_applied(pg_cursor):
    pg_cursor.execute("SELECT version FROM schema_migrations ORDER BY version")
    versions = [row[0] for row in pg_cursor.fetchall()]
    assert 1 in versions, "migration 0001 not applied"
    assert 2 in versions, "migration 0002 not applied"


def test_all_expected_tables_exist(pg_cursor):
    pg_cursor.execute("""
        SELECT table_name FROM information_schema.tables WHERE table_schema = 'public' AND table_type = 'BASE TABLE'
        """)
    actual = {row[0] for row in pg_cursor.fetchall()}
    missing = EXPECTED_TABLES - actual
    assert not missing, f"Missing tables: {missing}"


def test_clients_had_uuid_pk(pg_cursor):
    pg_cursor.execute("""
        SELECT column_name, data_type FROM information_schema.columns
        WHERE table_name = 'clients' AND column_name = 'id'
        """)
    row = pg_cursor.fetchone()
    assert row is not None
    assert row[1] == "uuid"


def test_clients_name_is_unique(pg_cursor):
    pg_cursor.execute("""
        SELECT COUNT(*) FROM pg_indexes
        WHERE tablename = 'clients'
          AND indexdef ILIKE '%UNIQUE%'
          AND indexdef ILIKE '%(name)%'
        """)
    assert pg_cursor.fetchone()[0] >= 1


def test_api_keys_fk_to_clients(pg_cursor):
    pg_cursor.execute("""
        SELECT COUNT(*) FROM information_schema.table_constraints
        WHERE table_name = 'api_keys' AND constraint_type = 'FOREIGN KEY'
        """)
    assert pg_cursor.fetchone()[0] >= 1


def test_api_keys_hash_column_exists(pg_cursor):
    pg_cursor.execute("""
        SELECT data_type FROM information_schema.columns
        WHERE table_name = 'api_keys' AND column_name = 'key_hash'
        """)
    row = pg_cursor.fetchone()
    assert row is not None, "key_hash column missing"
    assert row[0] == "text"


def test_resources_metadata_is_jsonb(pg_cursor):
    pg_cursor.execute("""
        SELECT data_type FROM information_schema.columns
        WHERE table_name = 'resources' AND column_name = 'metadata'
        """)
    row = pg_cursor.fetchone()
    assert row is not None
    assert row[0] == "jsonb"


def test_resources_has_gin_index(pg_cursor):
    pg_cursor.execute("""
        SELECT indexname FROM pg_indexes
        WHERE tablename = 'resources' AND indexdef LIKE '%gin%'
        """)
    rows = pg_cursor.fetchall()
    assert len(rows) >= 1, "resources is missing a GIN index on jsonb"


def test_policies_rules_is_jsonb(pg_cursor):
    pg_cursor.execute("""
        SELECT data_type FROM information_schema.columns
        WHERE table_name = 'policies' AND column_name = 'rules'
        """)
    row = pg_cursor.fetchone()
    assert row is not None
    assert row[0] == "jsonb"


def test_timescaledb_extension_enabled(pg_cursor):
    pg_cursor.execute(
        "SELECT extversion FROM pg_extension WHERE extname = 'timescaledb'"
    )
    row = pg_cursor.fetchone()
    assert row is not None, "TimescaleDB extension not installed"


def test_audit_log_is_hypertable(pg_cursor):
    pg_cursor.execute("""
        SELECT hypertable_name FROM timescaledb_information.hypertables
        WHERE hypertable_name = 'audit_log'
        """)
    assert pg_cursor.fetchone() is not None, "audit_log is not a hypertable"


def test_audit_log_has_composite_pk(pg_cursor):
    pg_cursor.execute("""
        SELECT a.attname
          FROM pg_index i
          JOIN pg_attribute a ON a.attrelid = i.indrelid AND a.attnum = ANY(i.indkey)
         WHERE i.indrelid = 'audit_log'::regclass AND i.indisprimary
         ORDER BY a.attnum
        """)
    cols = [row[0] for row in pg_cursor.fetchall()]
    assert "id" in cols
    assert "occurred_at" in cols


def test_retention_policy_exists(pg_cursor):
    pg_cursor.execute("""
        SELECT config FROM timescaledb_information.jobs
        WHERE proc_name = 'policy_retention'
        """)
    row = pg_cursor.fetchone()
    assert row is not None, "No retention policy configured"
    assert "90 days" in str(row[0])


def test_clients_has_updated_at_trigger(pg_cursor):
    pg_cursor.execute("""
        SELECT tgname FROM pg_trigger
        WHERE tgrelid = 'clients'::regclass AND NOT tgisinternal
        """)
    triggers = [row[0] for row in pg_cursor.fetchall()]
    assert any(
        "updated" in t for t in triggers
    ), f"clients table missing updated_at trigger; have: {triggers}"
