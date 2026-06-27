import time
import uuid
import pytest
from datetime import datetime, timezone, timedelta


@pytest.fixture
def audit_request_id():
    """Unique request_id prefix for each test, used for cleanup."""
    rid = f"test-{uuid.uuid4()}"
    yield rid


@pytest.fixture
def audit_cleanup(pg_conn, audit_request_id):
    """Delete all audit_log rows for this test's request_id prefix."""
    yield
    with pg_conn.cursor() as cur:
        cur.execute(
            "DELETE FROM audit_log WHERE request_id LIKE %s",
            (f"{audit_request_id}%",),
        )


def insert_audit_row(
    cursor,
    request_id: str,
    *,
    occurred_at=None,
    method: str = "GET",
    path: str = "/v1/test",
    status_code: int = 200,
):
    cursor.execute(
        """
        INSERT INTO audit_log
            (occurred_at, request_id, method, path, status_code)
        VALUES (%s, %s, %s, %s, %s)
        RETURNING id, occurred_at
        """,
        (
            occurred_at or datetime.now(timezone.utc),
            request_id,
            method,
            path,
            status_code,
        ),
    )
    return cursor.fetchone()


def test_audit_log_insert_and_read_back(pg_cursor, audit_request_id, audit_cleanup):
    inserted_id, inserted_ts = insert_audit_row(pg_cursor, audit_request_id)

    pg_cursor.execute(
        "SELECT method, path, status_code FROM audit_log WHERE id = %s",
        (inserted_id,),
    )
    row = pg_cursor.fetchone()
    assert row == ("GET", "/v1/test", 200)


def test_audit_log_default_occurred_at(pg_cursor, audit_request_id, audit_cleanup):
    pg_cursor.execute(
        """
        INSERT INTO audit_log (request_id, method, path, status_code)
        VALUES (%s, 'POST', '/v1/x', 201)
        RETURNING occurred_at
        """,
        (audit_request_id,),
    )
    ts = pg_cursor.fetchone()[0]
    now = datetime.now(timezone.utc)
    assert abs((now - ts).total_seconds()) < 5


def test_audit_log_metadata_is_jsonb(pg_cursor, audit_request_id, audit_cleanup):
    pg_cursor.execute(
        """
        INSERT INTO audit_log
            (request_id, method, path, status_code, metadata)
        VALUES (%s, 'GET', '/v1/x', 200, %s::jsonb)
        RETURNING id
        """,
        (audit_request_id, '{"foo": "bar", "n": 42}'),
    )
    row_id = pg_cursor.fetchone()[0]

    pg_cursor.execute(
        "SELECT metadata->>'foo', (metadata->>'n')::int FROM audit_log WHERE id = %s",
        (row_id,),
    )
    assert pg_cursor.fetchone() == ("bar", 42)


def test_audit_log_id_alone_not_unique(pg_cursor, audit_request_id, audit_cleanup):
    shared_id = str(uuid.uuid4())
    pg_cursor.execute(
        """
        INSERT INTO audit_log (id, occurred_at, request_id, method, path, status_code)
        VALUES (%s, %s, %s, 'GET', '/x', 200)
        """,
        (shared_id, datetime.now(timezone.utc), audit_request_id),
    )
    pg_cursor.execute(
        """
        INSERT INTO audit_log (id, occurred_at, request_id, method, path, status_code)
        VALUES (%s, %s, %s, 'GET', '/x', 200)
        """,
        (
            shared_id,
            datetime.now(timezone.utc) + timedelta(hours=1),
            audit_request_id,
        ),
    )

    pg_cursor.execute(
        "SELECT COUNT(*) FROM audit_log WHERE id = %s AND request_id = %s",
        (shared_id, audit_request_id),
    )
    assert pg_cursor.fetchone()[0] == 2


def test_audit_log_time_range_query(pg_cursor, audit_request_id, audit_cleanup):
    now = datetime.now(timezone.utc)

    insert_audit_row(
        pg_cursor, audit_request_id + "-old", occurred_at=now - timedelta(days=10)
    )
    insert_audit_row(
        pg_cursor, audit_request_id + "-mid", occurred_at=now - timedelta(days=3)
    )
    insert_audit_row(pg_cursor, audit_request_id + "-new", occurred_at=now)

    pg_cursor.execute(
        """
        SELECT request_id FROM audit_log
        WHERE request_id LIKE %s
          AND occurred_at >= %s
        ORDER BY occurred_at
        """,
        (f"{audit_request_id}%", now - timedelta(days=5)),
    )
    rows = [row[0] for row in pg_cursor.fetchall()]
    assert audit_request_id + "-mid" in rows
    assert audit_request_id + "-new" in rows
    assert audit_request_id + "-old" not in rows


def test_audit_log_old_inserts_create_chunks(
    pg_cursor, audit_request_id, audit_cleanup
):
    """Insert rows across different weeks and verify TimescaleDB places them into chunks."""

    now = datetime.now(timezone.utc)
    for weeks_ago in [1, 3, 5, 7]:
        insert_audit_row(
            pg_cursor,
            f"{audit_request_id}-w{weeks_ago}",
            occurred_at=now - timedelta(weeks=weeks_ago),
        )

    pg_cursor.execute(
        """
        SELECT COUNT(DISTINCT chunk_schema || '.' || chunk_name)
          FROM timescaledb_information.chunks c
         WHERE c.hypertable_name = 'audit_log'
           AND range_start <= %s
           AND range_end   >  %s - INTERVAL '8 weeks'
        """,
        (now, now),
    )
    distinct_chunks = pg_cursor.fetchone()[0]
    assert (
        distinct_chunks >= 2
    ), f"Expected ≥2 chunks for spanning data, got {distinct_chunks}"


def test_audit_log_jsonb_filter(pg_cursor, audit_request_id, audit_cleanup):
    for i in range(3):
        pg_cursor.execute(
            """
            INSERT INTO audit_log
                (request_id, method, path, status_code, metadata)
            VALUES (%s, 'GET', '/v1/x', 200, %s::jsonb)
            """,
            (f"{audit_request_id}-{i}", f'{{"env": "test", "n": {i}}}'),
        )

    pg_cursor.execute(
        """
        SELECT COUNT(*) FROM audit_log
        WHERE request_id LIKE %s
          AND metadata @> '{"env": "test"}'::jsonb
        """,
        (f"{audit_request_id}%",),
    )
    assert pg_cursor.fetchone()[0] == 3
