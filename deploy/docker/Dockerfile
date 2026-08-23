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
    bison flex autoconf python3 python3-pip libpq-dev libpqxx-dev \
    ccache \
    && wget -O - https://apt.kitware.com/keys/kitware-archive-latest.asc \
    | gpg --dearmor - \
    | tee /usr/share/keyrings/kitware-archive-keyring.gpg > /dev/null \
    && echo "deb [signed-by=/usr/share/keyrings/kitware-archive-keyring.gpg] \
    https://apt.kitware.com/ubuntu/ jammy main" \
    | tee /etc/apt/sources.list.d/kitware.list \
    && apt-get update \
    && apt-get install -y --no-install-recommends cmake \
    && rm -rf /var/lib/apt/lists/* \
    && pip3 install --no-cache-dir gcovr

# ── vcpkg — pinned to a release tag for reproducibility ───────────────────
ARG VCPKG_TAG=master
RUN git clone --depth 1 --branch ${VCPKG_TAG} \
    https://github.com/microsoft/vcpkg.git $VCPKG_ROOT \
    && $VCPKG_ROOT/bootstrap-vcpkg.sh -disableMetrics

WORKDIR /build

# ── Dependency layer - cached as long as vcpkg.json does not change ───────
COPY vcpkg.json .
RUN --mount=type=cache,target=/root/.cache/vcpkg \
    $VCPKG_ROOT/vcpkg install --triplet x64-linux

# ── Source layer ───────────────────────────────────────────────────────────
COPY CMakeLists.txt CMakePresets.json ./
COPY src/     src/
COPY include/ include/
COPY tests/   tests/
COPY db/      db/

# ── Build ──────────────────────────────────────────────────────────────────
# CMAKE_PRESET selects which CMakePresets.json entry to build:
#   ci        (default) - Release build, produces the runtime binary below
#   asan      - ASan + UBSan instrumented debug build (tests only)
#   tsan      - TSan + UBSan instrumented debug build (tests only)
#   coverage  - gcov-instrumented debug build (tests only)
# Only "ci" is stripped/copied into the runtime image - the others are
# built and run from the builder stage directly via `docker run`.
ARG CMAKE_PRESET=ci
RUN --mount=type=cache,target=/root/.ccache \
    cmake --preset ${CMAKE_PRESET} \
    -DCMAKE_CXX_COMPILER_LAUNCHER=ccache \
    && cmake --build --preset ${CMAKE_PRESET} \
    && if [ "${CMAKE_PRESET}" = "ci" ]; then strip build/src/corvus-core; fi

# ── Collect shared library dependencies ───────────────────────────────────
# Only meaningful for the "ci" preset - asan/tsan/coverage builds are test
# binaries run directly inside the builder container and never reach the
# runtime stage.
RUN mkdir -p /build/libs \
    && if [ "${CMAKE_PRESET}" = "ci" ]; then \
    ldd /build/build/src/corvus-core \
    | awk '/=>/ { print $3 }' \
    | grep -v "^$" \
    | grep -vE '/(ld-linux[^/]*|libc|libm|libdl|librt|libpthread|libresolv|libstdc\+\+|libgcc_s)\.so' \
    | xargs -I{} cp --no-clobber {} /build/libs/ || true; \
    fi

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
COPY --from=builder /build/db/migrations/     /usr/local/share/corvus/migrations/

ENV LD_LIBRARY_PATH=/usr/local/lib

# /tmp is the only writable directory in distroless
WORKDIR /tmp

EXPOSE 8080

HEALTHCHECK --interval=10s --timeout=3s --start-period=15s --retries=3 \
    CMD ["/usr/local/bin/corvus-core", "--health-check"]

ENTRYPOINT ["/usr/local/bin/corvus-core"]