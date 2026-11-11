-- Migration: 0003_resources_list_index
--
-- The resource list query filters on client_id and pages newest-first by
-- (created_at, id). This index serves it directly, so Postgres no longer
-- sorts every page. resources_client_idx is redundant: both this index and
-- resources_client_name_idx (client_id, name) already lead with client_id.

CREATE INDEX IF NOT EXISTS resources_client_created_idx
    ON resources (client_id, created_at DESC, id DESC);

DROP INDEX IF EXISTS resources_client_idx;