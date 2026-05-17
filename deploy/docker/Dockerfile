# ── Stage 1: builder ───────────────────────────────────────────────────────
FROM ubuntu:22.04 AS builder

ARG CORVUS_VERSION=0.1.0
ARG BUILD_DATE
ARG GIT_SHA

ENV DEBIAN_FRONTEND=noninteractive
ENV VCPKG_ROOT=/opt/vcpkg
ENV VCPKG_DISABLE_METRICS=1

# ── System dependencies + CMake 3.28 from Kitware ─────────────────────────
RUN apt-get update && apt-get install -y --no-install-recommends \
    build-essential ninja-build git curl zip unzip tar \
    pkg-config ca-certificates libssl-dev gpg wget \
    && wget -O - https://apt.kitware.com/keys/kitware-archive-latest.asc \
       | gpg --dearmor - \
       | tee /usr/share/keyrings/kitware-archive-keyring.gpg > /dev/null \
    && echo "deb [signed-by=/usr/share/keyrings/kitware-archive-keyring.gpg] \
       https://apt.kitware.com/ubuntu/ jammy main" \
       | tee /etc/apt/sources.list.d/kitware.list \
    && apt-get update \
    && apt-get install -y --no-install-recommends cmake \
    && rm -rf /var/lib/apt/lists/*

# ── vcpkg — pinned to a release tag for reproducibility ───────────────────
ARG VCPKG_TAG=2026.04.27
RUN git clone --depth 1 --branch ${VCPKG_TAG} \
    https://github.com/microsoft/vcpkg.git $VCPKG_ROOT \
    && $VCPKG_ROOT/bootstrap-vcpkg.sh -disableMetrics

WORKDIR /build

# ── Dependency layer — cached as long as vcpkg.json does not change ───────
COPY vcpkg.json .
RUN $VCPKG_ROOT/vcpkg install --triplet x64-linux

# ── Source layer ───────────────────────────────────────────────────────────
COPY CMakeLists.txt CMakePresets.json ./
COPY src/     src/
COPY include/ include/
COPY tests/   tests/

# ── Build ──────────────────────────────────────────────────────────────────
RUN cmake --preset ci \
    && cmake --build build --preset ci \
    && strip build/src/corvus-core

# ── Collect shared library dependencies ───────────────────────────────────
RUN mkdir -p /build/libs \
    && ldd /build/build/src/corvus-core \
       | awk '/=>/ { print $3 }' \
       | grep -v "^$" \
       | xargs -I{} cp --no-clobber {} /build/libs/ || true

# ── Stage 2: distroless runtime ────────────────────────────────────────────
FROM gcr.io/distroless/cc-debian12:nonroot AS runtime

ARG CORVUS_VERSION=0.1.0
ARG BUILD_DATE
ARG GIT_SHA

# ── OCI image labels ───────────────────────────────────────────────────────
LABEL org.opencontainers.image.title="corvus-core" \
      org.opencontainers.image.description="Corvus management host - C++ core service" \
      org.opencontainers.image.version="${CORVUS_VERSION}" \
      org.opencontainers.image.created="${BUILD_DATE}" \
      org.opencontainers.image.revision="${GIT_SHA}" \
      org.opencontainers.image.source="https://github.com/brij1197/corvus" \
      org.opencontainers.image.licenses="MIT"

# ── Copy shared libs then binary ───────────────────────────────────────────
COPY --from=builder /build/libs/              /usr/local/lib/
COPY --from=builder /build/build/src/corvus-core /usr/local/bin/corvus-core

EXPOSE 8080

HEALTHCHECK --interval=10s --timeout=3s --start-period=15s --retries=3 \
    CMD ["/usr/local/bin/corvus-core", "--health-check"]

ENTRYPOINT ["/usr/local/bin/corvus-core"]