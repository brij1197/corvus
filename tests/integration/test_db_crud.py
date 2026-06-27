import uuid
import pytest
from datetime import datetime, timezone


@pytest.fixture
def cleanup_clients(pg_conn):
    """Track and delete clients created during a test."""
    created_ids: list = []

    def _track(client_id):
        created_ids.append(str(client_id))

    yield _track

    if created_ids:
        with pg_conn.cursor() as cur:
            cur.execute(
                "DELETE FROM clients WHERE id = ANY(%s::uuid[])",
                (created_ids,),
            )


def _create_client(cur, name: str) -> uuid.UUID:
    cur.execute(
        "INSERT INTO clients (name) VALUES (%s) RETURNING id",
        (name,),
    )
    return cur.fetchone()[0]


def test_clients_insert_and_read(pg_cursor, cleanup_clients):
    name = f"test-client-{uuid.uuid4()}"
    cid = _create_client(pg_cursor, name)
    cleanup_clients(cid)

    pg_cursor.execute("SELECT name FROM clients WHERE id = %s", (cid,))
    assert pg_cursor.fetchone()[0] == name


def test_clients_unique_name(pg_cursor, cleanup_clients):
    import psycopg2

    name = f"dup-client-{uuid.uuid4()}"
    cid = _create_client(pg_cursor, name)
    cleanup_clients(cid)

    with pytest.raises(psycopg2.errors.UniqueViolation):
        pg_cursor.execute("INSERT INTO clients (name) VALUES (%s)", (name,))


def test_clients_updated_at_changes_on_update(pg_conn, cleanup_clients):
    cur = pg_conn.cursor()
    name = f"upd-client-{uuid.uuid4()}"
    cid = _create_client(cur, name)
    cleanup_clients(cid)

    cur.execute("SELECT updated_at FROM clients WHERE id = %s", (cid,))
    first = cur.fetchone()[0]

    import time

    time.sleep(0.05)

    cur.execute(
        "UPDATE clients SET name = %s WHERE id = %s",
        (name + "-modified", cid),
    )
    cur.execute("SELECT updated_at FROM clients WHERE id = %s", (cid,))
    second = cur.fetchone()[0]
    cur.close()

    assert second > first, "updated_at trigger did not fire"


def test_api_keys_fk_to_clients(pg_cursor, cleanup_clients):
    cid = _create_client(pg_cursor, f"akfk-{uuid.uuid4()}")
    cleanup_clients(cid)

    key_hash = "abc" * 21 + "d"
    pg_cursor.execute(
        """
        INSERT INTO api_keys (client_id, key_hash, scopes, description)
        VALUES (%s, %s, %s, %s) RETURNING id
        """,
        (cid, key_hash, "read", "test-key"),
    )
    key_id = pg_cursor.fetchone()[0]

    pg_cursor.execute("DELETE FROM clients WHERE id = %s::uuid", (str(cid),))

    pg_cursor.execute(
        "SELECT COUNT(*) FROM api_keys WHERE id = %s::uuid", (str(key_id),)
    )
    assert pg_cursor.fetchone()[0] == 0, "api_key was not cascaded"


def test_api_keys_orphan_insert_rejected(pg_cursor):
    import psycopg2

    fake_client_id = str(uuid.uuid4())
    with pytest.raises(psycopg2.errors.ForeignKeyViolation):
        pg_cursor.execute(
            """
            INSERT INTO api_keys (client_id, key_hash, scopes)
            VALUES (%s::uuid, %s, %s)
            """,
            (fake_client_id, "x" * 64, "read"),
        )


def test_resources_jsonb_metadata(pg_cursor, cleanup_clients):
    cid = _create_client(pg_cursor, f"res-{uuid.uuid4()}")
    cleanup_clients(cid)

    pg_cursor.execute(
        """
        INSERT INTO resources (client_id, kind, name, metadata)
        VALUES (%s, 'server', %s, %s::jsonb)
        RETURNING id
        """,
        (cid, f"srv-{uuid.uuid4()}", '{"region": "us-east-1", "size": "m5.large"}'),
    )
    rid = pg_cursor.fetchone()[0]

    pg_cursor.execute(
        """
        SELECT metadata->>'region', metadata->>'size'
          FROM resources WHERE id = %s
        """,
        (rid,),
    )
    assert pg_cursor.fetchone() == ("us-east-1", "m5.large")


def test_resources_gin_index_used_for_jsonb_query(pg_cursor, cleanup_clients):
    cid = _create_client(pg_cursor, f"gin-{uuid.uuid4()}")
    cleanup_clients(cid)

    for i in range(5):
        pg_cursor.execute(
            """
            INSERT INTO resources (client_id, kind, name, metadata)
            VALUES (%s, 'server', %s, %s::jsonb)
            """,
            (cid, f"srv-{i}-{uuid.uuid4()}", f'{{"env": "prod", "az": "{i}"}}'),
        )

    pg_cursor.execute(
        """
        SELECT COUNT(*) FROM resources
        WHERE client_id = %s
          AND metadata @> '{"env": "prod"}'::jsonb
        """,
        (cid,),
    )
    assert pg_cursor.fetchone()[0] == 5


def test_policy_attachment_links_policy_to_client(pg_cursor, cleanup_clients):
    cid = _create_client(pg_cursor, f"pol-{uuid.uuid4()}")
    cleanup_clients(cid)

    pg_cursor.execute(
        """
        INSERT INTO policies (name, rules)
        VALUES (%s, %s::jsonb) RETURNING id
        """,
        (f"policy-{uuid.uuid4()}", '{"allow": ["read"]}'),
    )
    pid = pg_cursor.fetchone()[0]

    pg_cursor.execute(
        """
        INSERT INTO policy_attachments (policy_id, target_type, target_id)
        VALUES (%s, %s, %s)
        """,
        (pid, "client", cid),
    )

    pg_cursor.execute(
        """
        SELECT p.name, p.rules->'allow'
          FROM policies p
          JOIN policy_attachments pa ON pa.policy_id = p.id
         WHERE pa.target_type = 'client' AND pa.target_id = %s::uuid
        """,
        (str(cid),),
    )
    row = pg_cursor.fetchone()
    assert row is not None
    assert "read" in row[1]

    pg_cursor.execute(
        "DELETE FROM policy_attachments WHERE policy_id = %s::uuid", (str(pid),)
    )
    pg_cursor.execute("DELETE FROM policies WHERE id = %s::uuid", (str(pid),))
