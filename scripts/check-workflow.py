import json
import re
import sys
from pathlib import Path

import yaml

WORKFLOW = ".github/workflows/ci.yml"


def steps(job):
    return yaml.safe_load(open(WORKFLOW))["jobs"][job]["steps"]


def find(job, needle):
    for s in steps(job):
        if needle in (s.get("name") or "") or needle in (s.get("run") or ""):
            return s
    return None


def vcpkg_pin_failures():
    """vcpkg must be pinned to one tag everywhere, matching vcpkg.json's baseline."""
    failures = []
    m = re.search(
        r"^ARG VCPKG_TAG=(\S+)$",
        Path("deploy/docker/Dockerfile").read_text(),
        re.MULTILINE,
    )
    if not m or m.group(1) in ("master", "main"):
        return ["deploy/docker/Dockerfile must pin VCPKG_TAG to a release tag."]
    tag = m.group(1)

    for wf in (".github/workflows/ci.yml", ".github/workflows/codeql.yml"):
        text = Path(wf).read_text()
        if "microsoft/vcpkg.git" in text and f"--branch {tag} " not in text:
            failures.append(
                f"{wf} must clone vcpkg with --branch {tag}, like the Dockerfile."
            )

    manifest = json.loads(Path("vcpkg.json").read_text())
    if not re.fullmatch(r"[0-9a-f]{40}", manifest.get("builtin-baseline", "")):
        failures.append("vcpkg.json needs a builtin-baseline commit SHA.")
    if tag not in manifest.get("$comment", ""):
        failures.append(
            f"vcpkg.json's $comment must name tag {tag}: the baseline has to be "
            "that tag's commit, so update them together."
        )
    return failures


def main():
    failures = vcpkg_pin_failures()

    start = find("integration-test", "Start stack")
    if start is None:
        failures.append("no 'Start stack' step found")
    elif start.get("env", {}).get("POSTGRES_PASSWORD") != "corvus_ci":
        failures.append(
            "'Start stack' must set POSTGRES_PASSWORD=corvus_ci - it is what "
            "initdb uses. Without it Postgres takes the compose default and "
            "every credentialled test fails auth."
        )

    if start is not None and "--no-build" not in (start.get("run") or ""):
        failures.append(
            "'Start stack' must run `docker compose up --no-build`. Letting "
            "Compose build goes through Bake, which fails in CI on fs "
            "entitlements for the layer cache."
        )

    names = [s.get("name") or "" for s in steps("integration-test")]
    build = find("integration-test", "Build corvus-core image")
    if build is None:
        failures.append("no 'Build corvus-core image' step found")
    else:
        run = build.get("run") or ""
        if "-t corvus-core:ci" not in run or "--load" not in run:
            failures.append(
                "the image build must `--load` and tag corvus-core:ci, the "
                "image the compose override points corvus-core at."
            )
        if "Start stack" in names and names.index(
            "Build corvus-core image"
        ) > names.index("Start stack"):
            failures.append("the image must be built before 'Start stack'.")

    override = find("integration-test", "docker-compose.override.yml")
    if override is None or "image: corvus-core:ci" not in (override.get("run") or ""):
        failures.append(
            "the compose override must set corvus-core's image to "
            "corvus-core:ci, or `up --no-build` has no image to start."
        )

    verify = find("integration-test", "Verify datastores")
    if verify is None:
        failures.append("no datastore reachability gate found")
    elif verify.get("env", {}).get("POSTGRES_PASSWORD") != (start or {}).get(
        "env", {}
    ).get("POSTGRES_PASSWORD"):
        failures.append(
            "the reachability gate must use the same password the stack was "
            "started with, or it tests nothing."
        )

    pytest_step = find("integration-test", "Run integration tests")
    if pytest_step is None:
        failures.append("no pytest step found")
    else:
        env = pytest_step.get("env", {})
        if env.get("CORVUS_REQUIRE_DEPS") != "1":
            failures.append(
                "pytest step must set CORVUS_REQUIRE_DEPS=1, or a missing "
                "datastore skips instead of failing and the job reports green "
                "on tests that never ran."
            )
        if env.get("CORVUS_DB_PASSWORD") != (start or {}).get("env", {}).get(
            "POSTGRES_PASSWORD"
        ):
            failures.append(
                "CORVUS_DB_PASSWORD must match the stack's POSTGRES_PASSWORD."
            )

    for f in failures:
        print(f"FAIL: {f}", file=sys.stderr)
    if failures:
        return 1
    print("workflow invariants OK")
    return 0


if __name__ == "__main__":
    sys.exit(main())