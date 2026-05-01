# syntax=docker/dockerfile:1.6
#
# Two-stage Dockerfile: a `build` stage with the full toolchain that
# compiles + tests the daemon, and a slim `runtime` stage that carries
# only the binary and its runtime shared-library deps.
#
# Used both by CI (.github/workflows/build.yml) and as the artifact
# published to ghcr.io on `v*` tags. Single source of truth for "what
# does zwaved need to build", so the local `docker build .` produces
# the same artifact CI does.
#
# Build:  docker build -t zwaved:dev .
# Run:    docker run --rm --privileged \
#             --device=/dev/ttyACM0 \
#             -v /var/run/dbus:/var/run/dbus \
#             zwaved:dev
#
# Production deployment would normally prefer running the binary
# directly under systemd on the host — the container is mostly a
# build-output artifact and a way to inspect a known-good build.

# ---------------------------------------------------------------------
# Stage 1: build — full toolchain, third-party deps from source where
# the distro lags (sdbus-c++ 2.x, eventpp, GCC 15).
# ---------------------------------------------------------------------
FROM ubuntu:24.04 AS build

ARG DEBIAN_FRONTEND=noninteractive

# Toolchain + build deps from apt.
#   * software-properties-common gives us add-apt-repository for the
#     ubuntu-toolchain-r/test PPA (GCC 15 isn't in Noble's default
#     repos yet).
#   * libsystemd-dev is the underlying sd-bus that sdbus-c++ wraps.
#   * libgtest-dev is needed by `ZWAVED_BUILD_TESTS=ON` (default).
RUN apt-get update \
    && apt-get install -y --no-install-recommends \
        software-properties-common \
        ca-certificates \
        gnupg \
        wget \
    && add-apt-repository -y ppa:ubuntu-toolchain-r/test \
    && apt-get update \
    && apt-get install -y --no-install-recommends \
        cmake \
        g++-15 \
        git \
        libudev-dev \
        libsqlite3-dev \
        libsystemd-dev \
        libgtest-dev \
        pkg-config \
    && update-alternatives --install /usr/bin/g++ g++ /usr/bin/g++-15 100 \
    && update-alternatives --install /usr/bin/gcc gcc /usr/bin/gcc-15 100 \
    && rm -rf /var/lib/apt/lists/*

# sdbus-c++ 2.x — Ubuntu still ships 1.x; daemon needs 2.x.
RUN git clone --depth 1 https://github.com/Kistler-Group/sdbus-cpp.git /tmp/sdbus-cpp \
    && cmake -S /tmp/sdbus-cpp -B /tmp/sdbus-cpp/build \
        -DSDBUSCPP_BUILD_CODEGEN=OFF \
        -DSDBUSCPP_BUILD_TESTS=OFF \
        -DCMAKE_BUILD_TYPE=Release \
    && cmake --build /tmp/sdbus-cpp/build --parallel \
    && cmake --install /tmp/sdbus-cpp/build \
    && ldconfig \
    && rm -rf /tmp/sdbus-cpp

# eventpp — header-only, not packaged. find_package needs the install.
RUN git clone --depth 1 https://github.com/wqking/eventpp.git /tmp/eventpp \
    && cmake -S /tmp/eventpp -B /tmp/eventpp/build \
    && cmake --install /tmp/eventpp/build \
    && rm -rf /tmp/eventpp

# Build + test the daemon. Tests run in this stage so a broken commit
# fails the docker build — keeps the published artifact honest.
WORKDIR /src
COPY . .
RUN cmake --preset gnu \
    && cmake --build cmake-build-gnu --parallel \
    && ctest --test-dir cmake-build-gnu --output-on-failure

# ---------------------------------------------------------------------
# Stage 2: runtime — ubuntu base + dynamic deps + the daemon binary.
# Drops the build toolchain to keep the image small.
# ---------------------------------------------------------------------
FROM ubuntu:24.04 AS runtime

ARG DEBIAN_FRONTEND=noninteractive

# Runtime shared-library deps only — no compilers, no headers.
#   * libstdc++6 from the toolchain PPA matches what gcc-15 produced
#     in the build stage.
RUN apt-get update \
    && apt-get install -y --no-install-recommends \
        software-properties-common \
        ca-certificates \
    && add-apt-repository -y ppa:ubuntu-toolchain-r/test \
    && apt-get update \
    && apt-get install -y --no-install-recommends \
        libstdc++6 \
        libudev1 \
        libsqlite3-0 \
        libsystemd0 \
    && apt-get purge -y software-properties-common \
    && apt-get autoremove -y \
    && rm -rf /var/lib/apt/lists/*

# sdbus-c++ runtime .so from the build stage.
COPY --from=build /usr/local/lib/libsdbus-c++.so* /usr/local/lib/
RUN ldconfig

# The binary itself plus the system-bus policy.
COPY --from=build /src/cmake-build-gnu/zwaved /usr/local/bin/zwaved
COPY --from=build /src/dbus/com.tiunda.ZWaved.conf /etc/dbus-1/system.d/
COPY --from=build /src/etc/zwaved.conf /etc/zwaved/zwaved.conf

# State directory matching the daemon's default (etc/zwaved.conf
# `[storage] state_dir`); can be overridden by mounting a host volume.
RUN mkdir -p /var/lib/zwaved

# OCI labels — image metadata visible on the GHCR landing page and via
# `docker inspect`. The version label is stamped at build time by CI.
ARG ZWAVED_VERSION=dev
LABEL org.opencontainers.image.title="zwaved" \
      org.opencontainers.image.description="Z-Wave communication daemon (D-Bus)" \
      org.opencontainers.image.source="https://github.com/tiunda/zwaved" \
      org.opencontainers.image.licenses="MIT" \
      org.opencontainers.image.version="${ZWAVED_VERSION}"

ENTRYPOINT ["/usr/local/bin/zwaved"]
