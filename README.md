# Corvus

A general-purpose infrastructure management host built from the ground up.
C++ powers the core - API gateway, resource manager, event bus, policy engine, and all data layer clients.
Python owns the operator layer - CLI, SDK, observability tooling, automation, and integration tests.
The full observability stack (Prometheus, Grafana, Loki, Jaeger) ships as first-class infrastructure alongside the application.

---

## Architecture overview

```
┌─────────────────────────────────────────────────────┐
│             Python layer (operator plane)           │
│  CLI (Typer)  │  SDK (httpx) │ Observability scripts│
└──────────────────────┬──────────────────────────────┘
                       │  REST API (JSON/HTTP)
┌──────────────────────▼──────────────────────────────┐
│                C++ core (data plane)                │
│  API Gateway → Resource Manager                     │
│             → Event Bus                             │
│             → Policy Engine                         │
│  Data clients: libpqxx │ hiredis │ prometheus-cpp   │
└─────────────────────────────────────────────────────┘
┌─────────────────────────────────────────────────────┐
│                   Data layer                        │
│  PostgreSQL  │  Redis  │  TimescaleDB               │
└─────────────────────────────────────────────────────┘
┌─────────────────────────────────────────────────────┐
│               Observability stack                   │
│  Prometheus  │  Grafana  │  Loki  │  Jaeger         │
└─────────────────────────────────────────────────────┘
```

Full architecture documentation lives in [`docs/architecture/overview.md`](docs/architecture/overview.md).

---

## Technology stack

| Layer            | Technology       | Purpose                          |
| ---------------- | ---------------- | -------------------------------- |
| C++ HTTP server  | Drogon           | API gateway and REST endpoints   |
| C++ build system | CMake + vcpkg    | Build and dependency management  |
| C++ DB client    | libpqxx          | PostgreSQL access                |
| C++ cache client | hiredis          | Redis access                     |
| C++ metrics      | prometheus-cpp   | Prometheus `/metrics` endpoint   |
| C++ testing      | Google Test      | Unit tests                       |
| Python CLI       | Typer + httpx    | Operator command-line interface  |
| Python SDK       | httpx + pydantic | Programmatic API client          |
| Python testing   | pytest           | Integration and end-to-end tests |
| Primary database | PostgreSQL 16    | System state and configuration   |
| Cache + pub/sub  | Redis 7          | Caching and async messaging      |
| Time-series DB   | TimescaleDB      | Metrics history and audit log    |
| Metrics          | Prometheus       | Scraping and alerting            |
| Dashboards       | Grafana          | Visualization and alerting UI    |
| Logging          | Loki             | Structured log aggregation       |
| Tracing          | Jaeger           | Distributed trace collection     |
| Containerization | Docker + Compose | Local development stack          |
| Orchestration    | Kubernetes       | Hybrid cloud deployment          |
| CI/CD            | GitHub Actions   | Build, test, and image pipeline  |

---

## Project status

| Epic                     | Status      |
| ------------------------ | ----------- |
| C++ project scaffold     | ✅ Complete |
| API gateway + auth       | ✅ Complete |
| PostgreSQL + Redis layer | Planned     |
| Resource manager domain  | Planned     |
| Prometheus + Grafana     | Planned     |
| Event bus                | Planned     |
| Policy engine            | Planned     |
| TimescaleDB + audit log  | Planned     |
| Loki + Jaeger tracing    | Planned     |
| Python SDK + CLI         | Planned     |
| Security hardening       | Planned     |
| Documentation + polish   | Planned     |

## Getting started

### Prerequisites

- Docker and Docker Compose
- CMake 3.25+
- A C++20-capable compiler (GCC 12+ or Clang 15+)
- Python 3.12+

### Run locally

```bash
# Clone the repo
git clone https://github.com/brij1197/corvus.git
cd corvus

# Generate an RS256 keypair for JWT authentication
openssl genrsa -out /tmp/corvus_test.pem 2048
openssl rsa -in /tmp/corvus_test.pem -pubout -out /tmp/corvus_test_pub.pem

# Create a docker-compose.override.yml with the public key
python3 -c "
key = open('/tmp/corvus_test_pub.pem').read().strip()
content = 'services:\n  corvus-core:\n    environment:\n      CORVUS_JWT_PUBLIC_KEY: |-\n'
for line in key.split('\n'):
    content += '        ' + line + '\n'
content += '  redis:\n    ports:\n      - \"6380:6379\"\n'
content += '  postgres:\n    ports:\n      - \"5433:5432\"\n'
open('docker-compose.override.yml', 'w').write(content)
"

# Start the full stack
docker compose up -d

# Build and test the C++ core
cmake --preset default
cmake --build build -j$(nproc)
ctest --test-dir build --output-on-failure

# Install Python tooling
pip install -r requirements-dev.txt

# Run integration tests (requires stack running)
export CORVUS_TEST_PRIVATE_KEY="$(cat /tmp/corvus_test.pem)"
pytest tests/integration -v
```

Full local development guide: [`docs/operations/local-dev.md`](docs/operations/local-dev.md)

---

## Repository structure

```
corvus/
├── src/                  # C++ source
│   ├── gateway/          # API gateway and middleware
│   ├── resource/         # Resource manager
│   ├── eventbus/         # Event bus
│   ├── policy/           # Policy engine
│   └── dataclients/      # libpqxx, hiredis, prometheus-cpp wrappers
├── include/              # C++ public headers
├── tests/
│   ├── unit/             # C++ Google Test suites
│   └── integration/      # Python pytest integration tests
├── python/
│   ├── cli/              # Typer CLI (corvus-ctl)
│   └── sdk/              # Python SDK
├── deploy/
│   ├── docker/           # Dockerfiles
│   ├── compose/          # Docker Compose files
│   └── k8s/              # Kubernetes manifests
├── observability/
│   ├── prometheus/       # Scrape configs and alert rules
│   ├── grafana/          # Provisioned dashboards and datasources
│   └── loki/             # Loki config
├── docs/
│   ├── architecture/     # Architecture docs and diagrams
│   ├── api/              # API reference (OpenAPI + guides)
│   ├── operations/       # Runbooks and deployment guides
│   └── adr/              # Architecture Decision Records
├── scripts/              # Dev and CI helper scripts
├── CMakeLists.txt
├── vcpkg.json
├── docker-compose.yml
└── README.md
```

---

## Documentation

- [Architecture overview](docs/architecture/overview.md)
- [Language strategy](docs/architecture/language-strategy.md)
- [Security model](docs/architecture/security-model.md)
- [Local development guide](docs/operations/local-dev.md)
- [API reference](docs/api/resources.md)
- [Architecture Decision Records](docs/adr/)

---

## Contributing

This is a personal project built incrementally. Each feature is developed on a branch, validated by CI, and merged via pull request - even as a solo project. See [`docs/operations/local-dev.md`](docs/operations/local-dev.md) for the full development workflow.

---

## License

MIT