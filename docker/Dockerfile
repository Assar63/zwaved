# syntax=docker/dockerfile:1.6
#
# Three-stage Dockerfile:
#   * `toolchain` — full build environment, no source. Reused by
#     `.devcontainer/devcontainer.json` so editor sessions land in a
#     working environment without paying for a full daemon build on
#     every image rebuild.
#   * `build`    — `FROM toolchain`, copies the source, runs the
#     configure / compile / ctest pipeline. CI targets this stage on
#     every push / PR.
#   * `runtime`  — slim ubuntu base with the daemon binary and its
#     dynamic-library deps. Published to ghcr.io on `v*` tags.
#
# Single source of truth for "what does zwaved need to build", so the
# local `docker build -f docker/Dockerfile .` produces the same
# artefact CI does. The build context is the repo root (so `COPY . .`
# in the build stage gets the full source tree); only the Dockerfile
# itself lives under docker/.
#
# Build:  docker build -f docker/Dockerfile -t zwaved:dev .
# Run:    docker run --rm --privileged \
#             --device=/dev/ttyACM0 \
#             -v /var/run/dbus:/var/run/dbus \
#             zwaved:dev
#
# Production deployment would normally prefer running the binary
# directly under systemd on the host — the container is mostly a
# build-output artifact and a way to inspect a known-good build.

# ---------------------------------------------------------------------
# Stage 1: toolchain — full build environment, third-party deps from
# source where the distro lags (sdbus-c++ 2.x, eventpp, GCC 15) plus
# the dev extras the codegen and pre-commit hook need
# (python3-yaml / jinja2 for scripts/codegen/, clang-format /
# clang-tidy for the staged-files check). No source code copied yet —
# this stage is the canonical "what does zwaved need to build"
# environment, reused by the `build` stage below and by
# `.devcontainer/devcontainer.json` (which targets this stage so an
# editor lands in a working environment without paying for the full
# daemon build on every rebuild).
# ---------------------------------------------------------------------
FROM ubuntu:24.04 AS toolchain

ARG DEBIAN_FRONTEND=noninteractive

# Toolchain + build deps from apt.
#   * software-properties-common gives us add-apt-repository for the
#     ubuntu-toolchain-r/test PPA (GCC 15 isn't in Noble's default
#     repos yet).
#   * libsystemd-dev is the underlying sd-bus that sdbus-c++ wraps.
#   * libgtest-dev is needed by `ZWAVED_BUILD_TESTS=ON` (default).
#   * libncurses-dev for utils/zwave-terminal/ (`ZWAVED_BUILD_UTILS=ON`).
#   * python3-yaml + python3-jinja2 for scripts/codegen/ — the cmake
#     configure step probes for both and FATAL_ERRORs without them.
#   * clang-format + clang-tidy for the pre-commit hook and the
#     `scripts/check-format --fix` workflow.
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
        libncurses-dev \
        pkg-config \
        python3-yaml \
        python3-jinja2 \
        clang-format \
        clang-tidy \
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

# ---------------------------------------------------------------------
# Stage 2: build — copy in the source and run the full configure /
# compile / test cycle. Tests run in this stage so a broken commit
# fails the docker build — keeps the published artifact honest.
# ---------------------------------------------------------------------
FROM toolchain AS build

WORKDIR /src
COPY . .
RUN cmake --preset gnu \
    && cmake --build cmake-build-gnu --parallel \
    && ctest --test-dir cmake-build-gnu --output-on-failure

# ---------------------------------------------------------------------
# Stage 3: runtime — ubuntu base + dynamic deps + the daemon binary.
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
