import time
import pytest

RESOURCES = "/v1/resources"


def create_resource(api, tenant, **fields):
    """POST a resource and return the parsed data object."""
    body = {"kind": "server", "name": f"res-{id(fields)}"}
    body.update(fields)
    resp = api.post(RESOURCES, headers=tenant.headers, json=body)
    assert resp.status_code == 201, resp.text
    return resp.json()["data"]


class TestCreateResource:
    def test_create_returns_201(self, api, tenant):
        resp = api.post(
            RESOURCES,
            headers=tenant.headers,
            json={"kind": "server", "name": "web-01"},
        )
        assert resp.status_code == 201

    def test_create_returns_location_header(self, api, tenant):
        resp = api.post(
            RESOURCES,
            headers=tenant.headers,
            json={"kind": "server", "name": "web-loc"},
        )
        rid = resp.json()["data"]["id"]
        assert resp.headers["location"] == f"/v1/resources/{rid}"

    def test_create_response_has_envelope(self, api, tenant):
        body = api.post(
            RESOURCES,
            headers=tenant.headers,
            json={"kind": "server", "name": "web-env"},
        ).json()

        assert body["error"] is None
        assert "request_id" in body["meta"]

    def test_created_resource_belongs_to_caller(self, api, tenant):
        data = create_resource(api, tenant, name="web-owner")
        assert data["client_id"] == tenant.client_id

    def test_create_persists_all_fields(self, api, tenant):
        data = create_resource(
            api,
            tenant,
            kind="database",
            name="db-fields",
            status="active",
            metadata={"engine": "postgres", "replicas": 3},
        )

        assert data["kind"] == "database"
        assert data["name"] == "db-fields"
        assert data["status"] == "active"
        assert data["metadata"] == {"engine": "postgres", "replicas": 3}

    def test_create_assigns_timestamps(self, api, tenant):
        data = create_resource(api, tenant, name="web-ts")
        assert data["created_at"]
        assert data["updated_at"] == data["created_at"]

    def test_duplicate_name_returns_409(self, api, tenant):
        api.post(
            RESOURCES,
            headers=tenant.headers,
            json={"kind": "server", "name": "dup"},
        )
        resp = api.post(
            RESOURCES,
            headers=tenant.headers,
            json={"kind": "server", "name": "dup"},
        )

        assert resp.status_code == 409
        assert resp.json()["error"]["code"] == "RESOURCE_ALREADY_EXISTS"

    def test_same_name_allowed_for_different_tenants(self, api, tenant, other_tenant):
        """Uniqueness is scoped per client, not global."""
        for t in (tenant, other_tenant):
            resp = api.post(
                RESOURCES,
                headers=t.headers,
                json={"kind": "server", "name": "shared-name"},
            )
            assert resp.status_code == 201

    def test_missing_kind_returns_422(self, api, tenant):
        resp = api.post(RESOURCES, headers=tenant.headers, json={"name": "no-kind"})
        assert resp.status_code == 422
        assert resp.json()["error"]["code"] == "UNPROCESSABLE_ENTITY"

    def test_malformed_json_returns_400(self, api, tenant):
        resp = api.post(
            RESOURCES,
            headers={**tenant.headers, "Content-Type": "application/json"},
            content="{not json",
        )
        assert resp.status_code == 400


class TestGetResource:
    def test_get_returns_created_resource(self, api, tenant):
        created = create_resource(api, tenant, name="web-get")
        resp = api.get(f"{RESOURCES}/{created['id']}", headers=tenant.headers)

        assert resp.status_code == 200
        assert resp.json()["data"] == created

    def test_get_populates_cache(self, api, tenant, redis):
        created = create_resource(api, tenant, name="web-cache")
        cache_key = f"corvus:resource:{tenant.client_id}:{created['id']}"

        assert not redis.exists(cache_key)
        api.get(f"{RESOURCES}/{created['id']}", headers=tenant.headers)
        assert redis.exists(cache_key)

    def test_second_get_serves_identical_body(self, api, tenant):
        """Cache hit must be indistinguishable from a cache miss."""
        created = create_resource(api, tenant, name="web-twice")
        url = f"{RESOURCES}/{created['id']}"

        first = api.get(url, headers=tenant.headers).json()["data"]
        second = api.get(url, headers=tenant.headers).json()["data"]
        assert first == second

    def test_unknown_uuid_returns_404(self, api, tenant):
        resp = api.get(
            f"{RESOURCES}/00000000-0000-0000-0000-000000000000",
            headers=tenant.headers,
        )
        assert resp.status_code == 404
        assert resp.json()["error"]["code"] == "RESOURCE_NOT_FOUND"

    def test_malformed_uuid_returns_404(self, api, tenant):
        resp = api.get(f"{RESOURCES}/not-a-uuid", headers=tenant.headers)
        assert resp.status_code == 404

    def test_other_tenant_cannot_read(self, api, tenant, other_tenant):
        created = create_resource(api, tenant, name="web-private")
        resp = api.get(f"{RESOURCES}/{created['id']}", headers=other_tenant.headers)

        assert resp.status_code == 404
        assert resp.json()["error"]["code"] == "RESOURCE_NOT_FOUND"


class TestListResources:
    def test_list_returns_created_resources(self, api, tenant):
        create_resource(api, tenant, name="list-a")
        create_resource(api, tenant, name="list-b")

        items = api.get(RESOURCES, headers=tenant.headers).json()["data"]
        names = {r["name"] for r in items}
        assert {"list-a", "list-b"} <= names

    def test_list_only_returns_own_resources(self, api, tenant, other_tenant):
        create_resource(api, tenant, name="mine")
        create_resource(api, other_tenant, name="theirs")

        items = api.get(RESOURCES, headers=tenant.headers).json()["data"]
        assert all(r["client_id"] == tenant.client_id for r in items)
        assert "theirs" not in {r["name"] for r in items}

    def test_list_is_empty_for_new_tenant(self, api, other_tenant):
        items = api.get(RESOURCES, headers=other_tenant.headers).json()["data"]
        assert items == []

    def test_filter_by_kind(self, api, tenant):
        create_resource(api, tenant, kind="server", name="f-server")
        create_resource(api, tenant, kind="database", name="f-db")

        items = api.get(
            RESOURCES, headers=tenant.headers, params={"kind": "database"}
        ).json()["data"]

        assert {r["name"] for r in items} == {"f-db"}

    def test_filter_by_status(self, api, tenant):
        create_resource(api, tenant, name="s-active", status="active")
        create_resource(api, tenant, name="s-retired", status="retired")

        items = api.get(
            RESOURCES, headers=tenant.headers, params={"status": "retired"}
        ).json()["data"]

        assert {r["name"] for r in items} == {"s-retired"}

    def test_limit_caps_page_size(self, api, tenant):
        for i in range(4):
            create_resource(api, tenant, name=f"page-{i}")

        body = api.get(RESOURCES, headers=tenant.headers, params={"limit": 2}).json()

        assert len(body["data"]) == 2
        assert body["meta"]["has_more"] is True
        assert body["meta"]["next_cursor"]

    def test_cursor_returns_next_page_without_overlap(self, api, tenant):
        for i in range(5):
            create_resource(api, tenant, name=f"cursor-{i}")

        first = api.get(RESOURCES, headers=tenant.headers, params={"limit": 2}).json()
        second = api.get(
            RESOURCES,
            headers=tenant.headers,
            params={"limit": 2, "cursor": first["meta"]["next_cursor"]},
        ).json()

        first_ids = {r["id"] for r in first["data"]}
        second_ids = {r["id"] for r in second["data"]}
        assert first_ids.isdisjoint(second_ids)

    def test_paging_reaches_every_resource(self, api, tenant):
        """Walk the cursor to exhaustion; every resource appears exactly once."""
        expected = {
            create_resource(api, tenant, name=f"walk-{i}")["id"] for i in range(5)
        }

        seen = []
        cursor = None
        for _ in range(10):  # bounded, so a cursor bug can't hang the suite
            params = {"limit": 2}
            if cursor:
                params["cursor"] = cursor
            body = api.get(RESOURCES, headers=tenant.headers, params=params).json()

            seen.extend(r["id"] for r in body["data"])
            if not body["meta"].get("has_more"):
                break
            cursor = body["meta"]["next_cursor"]

        assert len(seen) == len(set(seen)), "a resource appeared on two pages"
        assert set(seen) == expected

    def test_last_page_has_no_more(self, api, tenant):
        create_resource(api, tenant, name="only-one")
        body = api.get(RESOURCES, headers=tenant.headers, params={"limit": 50}).json()
        assert body["meta"]["has_more"] is False

    def test_non_integer_limit_returns_400(self, api, tenant):
        resp = api.get(RESOURCES, headers=tenant.headers, params={"limit": "abc"})
        assert resp.status_code == 400

    def test_malformed_cursor_returns_400(self, api, tenant):
        resp = api.get(
            RESOURCES, headers=tenant.headers, params={"cursor": "not-a-cursor"}
        )
        assert resp.status_code == 400


class TestUpdateResource:
    def test_update_status(self, api, tenant):
        created = create_resource(api, tenant, name="u-status")
        resp = api.patch(
            f"{RESOURCES}/{created['id']}",
            headers=tenant.headers,
            json={"status": "decommissioned"},
        )

        assert resp.status_code == 200
        assert resp.json()["data"]["status"] == "decommissioned"

    def test_update_leaves_other_fields_untouched(self, api, tenant):
        created = create_resource(
            api,
            tenant,
            name="u-partial",
            kind="database",
            metadata={"engine": "postgres"},
        )
        updated = api.patch(
            f"{RESOURCES}/{created['id']}",
            headers=tenant.headers,
            json={"status": "active"},
        ).json()["data"]

        assert updated["name"] == "u-partial"
        assert updated["kind"] == "database"
        assert updated["metadata"] == {"engine": "postgres"}

    def test_update_bumps_updated_at(self, api, tenant):
        created = create_resource(api, tenant, name="u-ts")
        updated = api.patch(
            f"{RESOURCES}/{created['id']}",
            headers=tenant.headers,
            json={"status": "active"},
        ).json()["data"]

        assert updated["updated_at"] > created["updated_at"]
        assert updated["created_at"] == created["created_at"]

    def test_update_invalidates_cache(self, api, tenant, redis):
        created = create_resource(api, tenant, name="u-cache")
        url = f"{RESOURCES}/{created['id']}"
        cache_key = f"corvus:resource:{tenant.client_id}:{created['id']}"

        api.get(url, headers=tenant.headers)
        assert redis.exists(cache_key)

        api.patch(url, headers=tenant.headers, json={"status": "changed"})
        assert not redis.exists(cache_key)

    def test_get_after_update_is_not_stale(self, api, tenant):
        """The read that follows an update must not serve the pre-update body."""
        created = create_resource(api, tenant, name="u-stale")
        url = f"{RESOURCES}/{created['id']}"

        api.get(url, headers=tenant.headers)  # populate cache
        api.patch(url, headers=tenant.headers, json={"status": "fresh"})

        assert api.get(url, headers=tenant.headers).json()["data"]["status"] == "fresh"

    def test_rename_to_existing_name_returns_409(self, api, tenant):
        create_resource(api, tenant, name="taken")
        other = create_resource(api, tenant, name="renameable")

        resp = api.patch(
            f"{RESOURCES}/{other['id']}",
            headers=tenant.headers,
            json={"name": "taken"},
        )
        assert resp.status_code == 409

    def test_empty_patch_returns_422(self, api, tenant):
        created = create_resource(api, tenant, name="u-empty")
        resp = api.patch(
            f"{RESOURCES}/{created['id']}", headers=tenant.headers, json={}
        )
        assert resp.status_code == 422

    def test_update_unknown_id_returns_404(self, api, tenant):
        resp = api.patch(
            f"{RESOURCES}/00000000-0000-0000-0000-000000000000",
            headers=tenant.headers,
            json={"status": "active"},
        )
        assert resp.status_code == 404

    def test_other_tenant_cannot_update(self, api, tenant, other_tenant):
        created = create_resource(api, tenant, name="u-private")
        url = f"{RESOURCES}/{created['id']}"

        assert (
            api.patch(
                url, headers=other_tenant.headers, json={"status": "hijacked"}
            ).status_code
            == 404
        )

        # A 404 only says the caller was refused a result; it does not prove
        # the row was left alone. Read it back as its owner and confirm
        # nothing moved.
        after = api.get(url, headers=tenant.headers).json()["data"]
        assert after == created


class TestDeleteResource:
    def test_delete_returns_200(self, api, tenant):
        created = create_resource(api, tenant, name="d-one")
        resp = api.delete(f"{RESOURCES}/{created['id']}", headers=tenant.headers)

        assert resp.status_code == 200
        assert resp.json()["data"] is None
        assert resp.json()["error"] is None

    def test_resource_is_gone_after_delete(self, api, tenant):
        created = create_resource(api, tenant, name="d-gone")
        url = f"{RESOURCES}/{created['id']}"

        api.delete(url, headers=tenant.headers)
        assert api.get(url, headers=tenant.headers).status_code == 404

    def test_delete_removes_from_list(self, api, tenant):
        created = create_resource(api, tenant, name="d-listed")
        api.delete(f"{RESOURCES}/{created['id']}", headers=tenant.headers)

        items = api.get(RESOURCES, headers=tenant.headers).json()["data"]
        assert created["id"] not in {r["id"] for r in items}

    def test_delete_invalidates_cache(self, api, tenant, redis):
        created = create_resource(api, tenant, name="d-cache")
        url = f"{RESOURCES}/{created['id']}"
        cache_key = f"corvus:resource:{tenant.client_id}:{created['id']}"

        api.get(url, headers=tenant.headers)
        assert redis.exists(cache_key)

        api.delete(url, headers=tenant.headers)
        assert not redis.exists(cache_key)

    def test_second_delete_returns_404(self, api, tenant):
        created = create_resource(api, tenant, name="d-twice")
        url = f"{RESOURCES}/{created['id']}"

        api.delete(url, headers=tenant.headers)
        assert api.delete(url, headers=tenant.headers).status_code == 404

    def test_delete_unknown_id_returns_404(self, api, tenant):
        resp = api.delete(
            f"{RESOURCES}/00000000-0000-0000-0000-000000000000",
            headers=tenant.headers,
        )
        assert resp.status_code == 404

    def test_other_tenant_cannot_delete(self, api, tenant, other_tenant):
        created = create_resource(api, tenant, name="d-private")

        assert (
            api.delete(
                f"{RESOURCES}/{created['id']}", headers=other_tenant.headers
            ).status_code
            == 404
        )

        # Still there for its real owner.
        assert (
            api.get(f"{RESOURCES}/{created['id']}", headers=tenant.headers).status_code
            == 200
        )

    def test_name_is_reusable_after_delete(self, api, tenant):
        created = create_resource(api, tenant, name="recycled")
        api.delete(f"{RESOURCES}/{created['id']}", headers=tenant.headers)

        resp = api.post(
            RESOURCES,
            headers=tenant.headers,
            json={"kind": "server", "name": "recycled"},
        )
        assert resp.status_code == 201


class TestResourceAuth:
    @pytest.mark.parametrize("path", [RESOURCES, f"{RESOURCES}/x"])
    def test_no_credentials_returns_401(self, api, path):
        assert api.get(path).status_code == 401

    def test_jwt_without_api_key_returns_401(self, api, tenant):
        resp = api.get(RESOURCES, headers={"Authorization": f"Bearer {tenant.jwt}"})
        assert resp.status_code == 401

    def test_api_key_without_jwt_returns_401(self, api, tenant):
        resp = api.get(RESOURCES, headers={"X-Api-Key": tenant.api_key})
        assert resp.status_code == 401

    def test_unknown_api_key_returns_401(self, api, tenant):
        resp = api.get(
            RESOURCES,
            headers={
                "Authorization": f"Bearer {tenant.jwt}",
                "X-Api-Key": "no-such-key",
            },
        )
        assert resp.status_code == 401


def test_full_crud_lifecycle(api, tenant):
    """create -> get -> list -> update -> delete, end to end."""
    created = create_resource(
        api,
        tenant,
        kind="database",
        name="lifecycle",
        status="provisioning",
        metadata={"engine": "postgres"},
    )
    url = f"{RESOURCES}/{created['id']}"

    assert api.get(url, headers=tenant.headers).json()["data"] == created

    listed = api.get(RESOURCES, headers=tenant.headers).json()["data"]
    assert created["id"] in {r["id"] for r in listed}

    updated = api.patch(
        url,
        headers=tenant.headers,
        json={"status": "active", "metadata": {"engine": "postgres", "ha": True}},
    ).json()["data"]
    assert updated["status"] == "active"
    assert updated["metadata"]["ha"] is True

    assert api.get(url, headers=tenant.headers).json()["data"]["status"] == "active"

    assert api.delete(url, headers=tenant.headers).status_code == 200
    assert api.get(url, headers=tenant.headers).status_code == 404
