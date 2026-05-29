-- Migration: 0001_initial_schema

BEGIN;

CREATE TABLE IF NOT EXISTS schema_migrations (
    version BIGINT PRIMARY KEY,
    name TEXT NOT NULL,
    applied_at TIMESTAMPTZ NOT NULL DEFAULT now()
);

CREATE TABLE clients (
    id UUID PRIMARY KEY DEFAULT gen_random_uuid(),
    name TEXT NOT NULL,
    description TEXT,
    is_active   BOOLEAN     NOT NULL DEFAULT true,
    created_at  TIMESTAMPTZ NOT NULL DEFAULT now(),
    updated_at  TIMESTAMPTZ NOT NULL DEFAULT now()
);

CREATE UNIQUE INDEX clients_name_idx ON clients (name);
CREATE INDEX clients_is_active_idx   ON clients (is_active);

CREATE TABLE api_keys (
    id UUID PRIMARY KEY DEFAULT gen_random_uuid(),
    client_id UUID NOT NULL REFERENCES clients(id) ON DELETE CASCADE,
    key_hash TEXT NOT NULL,
    scopes TEXT NOT NULL DEFAULT '',
    description TEXT,
    expires_at TIMESTAMPTZ,
    is_active   BOOLEAN     NOT NULL DEFAULT true,
    created_at  TIMESTAMPTZ NOT NULL DEFAULT now(),
    updated_at  TIMESTAMPTZ NOT NULL DEFAULT now()
);

CREATE UNIQUE INDEX api_keys_hash_idx      ON api_keys (key_hash);
CREATE INDEX        api_keys_client_idx    ON api_keys (client_id);
CREATE INDEX        api_keys_active_idx    ON api_keys (is_active);
CREATE INDEX        api_keys_expires_idx   ON api_keys (expires_at)
    WHERE expires_at IS NOT NULL;

CREATE TABLE resources (
    id UUID PRIMARY KEY DEFAULT gen_random_uuid(),
    client_id   UUID NOT NULL REFERENCES clients (id) ON DELETE CASCADE,
    kind        TEXT        NOT NULL,
    name        TEXT        NOT NULL,
    status      TEXT        NOT NULL DEFAULT 'unknown',
    metadata    JSONB       NOT NULL DEFAULT '{}',
    created_at  TIMESTAMPTZ NOT NULL DEFAULT now(),
    updated_at  TIMESTAMPTZ NOT NULL DEFAULT now()
);

CREATE UNIQUE INDEX resources_client_name_idx ON resources (client_id, name);
CREATE INDEX        resources_kind_idx         ON resources (kind);
CREATE INDEX        resources_status_idx       ON resources (status);
CREATE INDEX        resources_client_idx       ON resources (client_id);
CREATE INDEX        resources_metadata_idx     ON resources USING GIN (metadata);

CREATE TABLE policies (
    id          UUID        PRIMARY KEY DEFAULT gen_random_uuid(),
    name        TEXT        NOT NULL,
    description TEXT,
    rules       JSONB       NOT NULL DEFAULT '[]',
    is_active   BOOLEAN     NOT NULL DEFAULT true,
    created_at  TIMESTAMPTZ NOT NULL DEFAULT now(),
    updated_at  TIMESTAMPTZ NOT NULL DEFAULT now()
);

CREATE UNIQUE INDEX policies_name_idx ON policies (name);

CREATE TABLE policy_attachments (
    id              UUID        PRIMARY KEY DEFAULT gen_random_uuid(),
    policy_id       UUID        NOT NULL REFERENCES policies (id) ON DELETE CASCADE,
    target_type     TEXT        NOT NULL,
    target_id       UUID        NOT NULL,
    created_at      TIMESTAMPTZ NOT NULL DEFAULT now()
);

CREATE UNIQUE INDEX policy_attachments_unique_idx
    ON policy_attachments (policy_id, target_type, target_id);
CREATE INDEX policy_attachments_target_idx
    ON policy_attachments (target_type, target_id);

CREATE TABLE audit_log (
    id              UUID        NOT NULL DEFAULT gen_random_uuid(),
    occurred_at     TIMESTAMPTZ NOT NULL DEFAULT now(),
    client_id       UUID        REFERENCES clients (id) ON DELETE SET NULL,
    actor_subject   TEXT,      
    request_id      TEXT        NOT NULL,
    method          TEXT        NOT NULL,  
    path            TEXT        NOT NULL,  
    status_code     SMALLINT    NOT NULL,
    duration_ms     INTEGER,               
    resource_kind   TEXT,                  
    resource_id     UUID,                  
    error_code      TEXT,                  
    metadata        JSONB       NOT NULL DEFAULT '{}'
);

ALTER TABLE audit_log ADD PRIMARY KEY (id, occurred_at);

CREATE INDEX audit_log_occurred_idx    ON audit_log (occurred_at DESC);
CREATE INDEX audit_log_client_idx      ON audit_log (client_id, occurred_at DESC);
CREATE INDEX audit_log_request_id_idx  ON audit_log (request_id);
CREATE INDEX audit_log_resource_idx    ON audit_log (resource_kind, resource_id)
    WHERE resource_id IS NOT NULL;

CREATE OR REPLACE FUNCTION set_updated_at()
RETURNS TRIGGER LANGUAGE plpgsql AS $$
BEGIN
    NEW.updated_at = now();
    RETURN NEW;
END;
$$;

CREATE TRIGGER clients_updated_at
    BEFORE UPDATE ON clients
    FOR EACH ROW EXECUTE FUNCTION set_updated_at();

CREATE TRIGGER api_keys_updated_at
    BEFORE UPDATE ON api_keys
    FOR EACH ROW EXECUTE FUNCTION set_updated_at();

CREATE TRIGGER resources_updated_at
    BEFORE UPDATE ON resources
    FOR EACH ROW EXECUTE FUNCTION set_updated_at();

CREATE TRIGGER policies_updated_at
    BEFORE UPDATE ON policies
    FOR EACH ROW EXECUTE FUNCTION set_updated_at();

INSERT INTO schema_migrations (version, name)
VALUES (1, '0001_initial_schema');

COMMIT;