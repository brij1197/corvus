import sys
import yaml

WORKFLOW = ".github/workflows/ci.yml"


def steps(job):
    return yaml.safe_load(open(WORKFLOW))["jobs"][job]["steps"]


def find(job, needle):
    for s in steps(job):
        if needle in (s.get("name") or "") or needle in (s.get("run") or ""):
            return s
    return None


def main():
    failures = []

    start = find("integration-test", "Start stack")
    if start is None:
        failures.append("no 'Start stack' step found")
    elif start.get("env", {}).get("POSTGRES_PASSWORD") != "corvus_ci":
        failures.append(
            "'Start stack' must set POSTGRES_PASSWORD=corvus_ci - it is what "
            "initdb uses. Without it Postgres takes the compose default and "
            "every credentialled test fails auth."
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
