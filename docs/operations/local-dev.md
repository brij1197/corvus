# Local development guide

## Prerequisites

This project builds and runs on **Linux only**. On Windows, use WSL2 (Ubuntu 22.04).
All commands below run inside WSL2 or a native Linux terminal.

---

## One-time setup

### 1. Install system dependencies

```bash
sudo apt-get update
sudo apt-get install -y \
    build-essential ninja-build git curl zip unzip tar \
    pkg-config ca-certificates libssl-dev gpg wget
```

### 2. Install CMake 3.28+ from Kitware

Ubuntu 22.04's apt ships CMake 3.22, which does not support `CMakePresets.json` version 6.
Install from Kitware's official repository instead:

```bash
wget -O - https://apt.kitware.com/keys/kitware-archive-latest.asc \
  | gpg --dearmor - \
  | sudo tee /usr/share/keyrings/kitware-archive-keyring.gpg > /dev/null

echo "deb [signed-by=/usr/share/keyrings/kitware-archive-keyring.gpg] \
  https://apt.kitware.com/ubuntu/ jammy main" \
  | sudo tee /etc/apt/sources.list.d/kitware.list

sudo apt-get update
sudo apt-get install -y cmake
cmake --version   # should show 3.28+
```

### 3. Install vcpkg

```bash
git clone https://github.com/microsoft/vcpkg.git ~/vcpkg
~/vcpkg/bootstrap-vcpkg.sh -disableMetrics

# Add to shell permanently
echo 'export VCPKG_ROOT=$HOME/vcpkg' >> ~/.bashrc
echo 'export PATH=$VCPKG_ROOT:$PATH' >> ~/.bashrc
source ~/.bashrc
```

### 4. Clone the repository

```bash
cd ~
git clone https://github.com/brij1197/corvus.git
cd corvus
```

---

## Building

```bash
# Configure (downloads and builds all vcpkg dependencies on first run - takes ~10 min)
cmake --preset default

# Build
cmake --build build --parallel
```

The compiled binary is at `build/src/corvus-core`.

---

## Running locally

### C++ service only

```bash
./build/src/corvus-core
```

The service listens on `http://localhost:8080`.

### Verify health endpoints

```bash
curl http://localhost:8080/health
# {"path":"/health","status":"ok","version":"0.1.0"}

curl http://localhost:8080/ready
# {"path":"/ready","status":"ok","version":"0.1.0"}
```

### Full stack (all services)

```bash
cp .env.example .env   # first time only - fill in passwords if desired
docker compose up -d
```

Services and their ports:

| Service | URL |
|---|---|
| corvus-core (C++ API) | http://localhost:8080 |
| Grafana | http://localhost:3000 |
| Prometheus | http://localhost:9090 |
| Jaeger UI | http://localhost:16686 |
| Loki | http://localhost:3100 |

Grafana default login: `admin` / value of `GRAFANA_PASSWORD` in `.env` (default: `admin`).

---

## Running tests

### C++ unit tests

```bash
ctest --test-dir build --output-on-failure
```

### Python integration tests

```bash
# Requires the full stack to be running via docker compose
pip install -r requirements-dev.txt
pytest tests/integration -v
```

---

## Daily development workflow

1. Pull latest `main`:
   ```bash
   git checkout main && git pull origin main
   ```
2. Create a branch from the Linear issue:
   ```bash
   git checkout -b feature/CORV-XX-issue-title
   ```
3. Write code and tests
4. Build and verify locally
5. Push and open a PR - CI runs automatically
6. Squash and merge when CI is green
7. Linear closes the issue automatically on merge

---

## VS Code + WSL2

Install the [Remote - WSL](https://marketplace.visualstudio.com/items?itemName=ms-vscode-remote.remote-wsl)
extension, then:

```
Ctrl+Shift+P → "WSL: Open Folder in WSL" → navigate to ~/corvus
```

Recommended extensions inside WSL2:
- `ms-vscode.cpptools` - C++ IntelliSense
- `twxs.cmake` - CMake syntax highlighting
- `ms-vscode.cmake-tools` - CMake integration
- `charliermarsh.ruff` - Python linting

---

## Troubleshooting

**`CMakeCache.txt` path mismatch error**
Delete the stale cache and rebuild:
```bash
rm -rf build/
cmake --preset default
```

**vcpkg fails with "paths with embedded space" warning**
You are building from the Windows filesystem. Move to WSL2 native filesystem:
```bash
cd ~
git clone https://github.com/brij1197/corvus.git
```

**`cmake --version` shows 3.22**
CMake was not installed from Kitware. Re-run step 2 of the one-time setup above.

**Port 8080 already in use**
```bash
# Find and kill the existing process
lsof -i :8080
kill <PID>
```