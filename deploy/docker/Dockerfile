# ── Stage 1: builder ───────────────────────────────────────────────────────
FROM ubuntu:22.04 AS builder

ENV DEBIAN_FRONTEND=noninteractive
ENV VCPKG_ROOT=/opt/vcpkg

RUN apt-get update && apt-get install -y --no-install-recommends \
    build-essential ninja-build git curl zip unzip tar \
    pkg-config ca-certificates libssl-dev gpg wget \
    && wget -O - https://apt.kitware.com/keys/kitware-archive-latest.asc \
    | gpg --dearmor - \
    | tee /usr/share/keyrings/kitware-archive-keyring.gpg > /dev/null \
    && echo "deb [signed-by=/usr/share/keyrings/kitware-archive-keyring.gpg] \
    https://apt.kitware.com/ubuntu/ jammy main" \
    | tee /etc/apt/sources.list.d/kitware.list \
    && apt-get update && apt-get install -y --no-install-recommends cmake \
    && rm -rf /var/lib/apt/lists/*

RUN git clone https://github.com/microsoft/vcpkg.git $VCPKG_ROOT \
    && $VCPKG_ROOT/bootstrap-vcpkg.sh -disableMetrics

WORKDIR /build

COPY vcpkg.json .
RUN $VCPKG_ROOT/vcpkg install --triplet x64-linux

COPY CMakeLists.txt CMakePresets.json ./
COPY src/ src/
COPY include/ include/
COPY tests/ tests/

RUN cmake --preset ci && cmake --build build --preset ci

# ── Stage 2: distroless runtime ────────────────────────────────────────────
FROM gcr.io/distroless/cc-debian12:nonroot AS runtime

COPY --from=builder /build/build/src/corvus-core /usr/local/bin/corvus-core

EXPOSE 8080

ENTRYPOINT ["/usr/local/bin/corvus-core"]