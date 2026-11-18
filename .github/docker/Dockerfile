# Self-hosted GitHub Actions runner image for the Jde OpcGateway build.
#
# Layers the clang++-23 / libc++ toolchain (and the system libraries the -jde
# CMake presets link against) on top of the myoung34 auto-registering runner.
#
# Third-party dependencies (Boost, protobuf, open62541, jsonnet, …) are NOT baked
# into the image — they are the 2 GB $REPO_DIR/install tree and are bind-mounted
# read-only at `docker run` time. See README.md in this directory.
#
# GLIBC / ABI NOTE: the mounted deps (and tools like protoc) were built on the
# host; their highest symbol requirement across the whole tree is GLIBC_2.38, so
# the base image needs glibc >= 2.38. Ubuntu 24.04 "noble" (glibc 2.39) clears it;
# focal (2.31) and jammy (2.35) do NOT — and myoung34's `latest` tag is focal,
# which additionally ships a too-old cmake. If you later hit `GLIBC_x.y not found`,
# a dep was rebuilt against a newer glibc than the base — bump BASE_IMAGE, or
# rebuild the deps in-container with the `-repos` preset instead of mounting them.
# Pinned to a runner-versioned noble tag (not the floating `ubuntu-noble`, which
# upstream re-points, nor a bare version tag like `2.336.0`, which is focal —
# glibc 2.31, no liburing-dev, too-old cmake). Keep the runner version at what
# GitHub currently serves (.github/workflows/runner-version-check.yml watches it
# weekly). A stale pin is no longer fatal — the CMD at the bottom runs the listener
# under run.sh, which survives a self-update — but every container recreation then
# self-updates (~80 s) before it takes a job, and the image no longer matches what
# is actually running.
ARG BASE_IMAGE=myoung34/github-runner:2.337.0-ubuntu-noble
FROM ${BASE_IMAGE}

ARG LLVM_VERSION=23
USER root

# clang/libc++/lld from apt.llvm.org, plus the system -dev packages the presets
# reference: libxml2 (LIBXML2_INCLUDE_DIR), liburing (fwk io_uring), OpenSSL, and
# zstd + zlib static libs (open62541's export links static system OpenSSL, which
# drags in -lzstd and libz.a). libbacktrace and libjitterentropy are built from
# source below (no usable apt package on noble). binutils (objdump/strip) and
# patchelf are for apps/OpcHub/setup/linux/build-deb.sh (linux-release.yml).
# protobuf-compiler is apt's protoc, 3.21, for the demo tarball's smoke modules (apps/OpcHub/setup/demo/build-demo.sh):
# the demo's box and the watch's runner read them with apt's python3-protobuf, which takes no later protoc's code, so
# not the mounted deps' protoc.  Of libprotobuf-dev only the .proto files it imports are laid, under /usr/include:  its
# headers and libprotobuf.so would sit on the system paths beside the mounted deps' protobuf.
RUN set -eux; \
    export DEBIAN_FRONTEND=noninteractive; \
    apt-get update; \
    apt-get install -y --no-install-recommends \
        wget gnupg lsb-release ca-certificates software-properties-common; \
    wget -qO /tmp/llvm.sh https://apt.llvm.org/llvm.sh; \
    chmod +x /tmp/llvm.sh; \
    /tmp/llvm.sh ${LLVM_VERSION}; \
    apt-get install -y --no-install-recommends \
        clang-${LLVM_VERSION} \
        lld-${LLVM_VERSION} \
        libc++-${LLVM_VERSION}-dev \
        libc++abi-${LLVM_VERSION}-dev \
        ninja-build make git pkg-config \
        libxml2-dev liburing-dev libssl-dev libzstd-dev zlib1g-dev \
        binutils patchelf protobuf-compiler; \
    cd /tmp; apt-get download libprotobuf-dev; \
    dpkg-deb --fsys-tarfile libprotobuf-dev_*.deb | tar -x -C / --wildcards './usr/include/google/protobuf/*.proto'; \
    rm -rf /var/lib/apt/lists/* /tmp/llvm.sh /tmp/libprotobuf-dev_*.deb

# libbacktrace (BOOST_STACKTRACE_USE_BACKTRACE / the presets' -lbacktrace): Ubuntu
# added the libbacktrace-dev package in 25.04, so noble (24.04) lacks it — build
# from source. Built PIC because the project links it into shared libs (libJde.so),
# and installed to the multiarch libdir + /usr/include to match the host package's
# layout, so -lbacktrace and <backtrace.h> resolve with no extra search paths.
RUN set -eux; \
    git clone --depth 1 https://github.com/ianlancetaylor/libbacktrace.git /tmp/libbacktrace; \
    cd /tmp/libbacktrace; \
    ./configure --prefix=/usr --libdir=/usr/lib/x86_64-linux-gnu \
        CC=clang-${LLVM_VERSION} CFLAGS="-fPIC -O2"; \
    make -j"$(nproc)"; \
    make install; \
    rm -rf /tmp/libbacktrace

# libjitterentropy: open62541's exported link interface (baked on the host, which
# links static system OpenSSL) requires -l:libjitterentropy.a / -ljitterentropy.
# noble packages only the jitterentropy-rngd daemon, not the library, so build it
# from source. Install the static archive straight into the multiarch libdir —
# `make install` uses /usr/lib, which this clang/lld config does not search, while
# /usr/lib/x86_64-linux-gnu (where libzstd-dev lands) is searched. The .a alone
# satisfies both -ljitterentropy and -l:libjitterentropy.a (statically linked, so
# no runtime .so needed).
RUN set -eux; \
    git clone --depth 1 https://github.com/smuellerDD/jitterentropy-library.git /tmp/jent; \
    cd /tmp/jent; \
    make -j"$(nproc)" CC=clang-${LLVM_VERSION}; \
    install -m 644 libjitterentropy.a /usr/lib/x86_64-linux-gnu/; \
    ldconfig; \
    rm -rf /tmp/jent

# cmake from the Kitware release rather than apt: noble ships 3.28.3, below the
# preset floor (cmakeMinimumRequired/MIN_REQ_CMAKE_VERSION = 4.2.3), so apt's
# cmake cannot configure the repo. Pin exactly the floor so CI proves the stated
# minimum actually works.
ARG CMAKE_VERSION=4.2.3
RUN set -eux; \
    wget -qO /tmp/cmake.tar.gz \
        "https://github.com/Kitware/CMake/releases/download/v${CMAKE_VERSION}/cmake-${CMAKE_VERSION}-linux-x86_64.tar.gz"; \
    tar -xzf /tmp/cmake.tar.gz --strip-components=1 -C /usr/local; \
    rm /tmp/cmake.tar.gz; \
    cmake --version

# The -jde presets set CMAKE_CXX_COMPILER=clang++ and link with -fuse-ld=lld, so
# the unversioned names must resolve to the v23 binaries (mirrors the host, where
# /usr/bin/clang++ -> clang++-23).
RUN set -eux; \
    update-alternatives --install /usr/bin/clang   clang   /usr/bin/clang-${LLVM_VERSION}   100; \
    update-alternatives --install /usr/bin/clang++ clang++ /usr/bin/clang++-${LLVM_VERSION} 100; \
    ln -sf /usr/bin/lld-${LLVM_VERSION}    /usr/bin/lld; \
    ln -sf /usr/bin/ld.lld-${LLVM_VERSION} /usr/bin/ld.lld; \
    clang++ --version

# Entry point / runner registration are inherited from the base image; only the
# command is overridden.
#
# Run the listener under the runner's own run.sh instead of the base image's
# `./bin/Runner.Listener run --startuptype service`. The difference is the
# self-update: the listener exits (code 3, or 4 when ephemeral) while a detached
# update script swaps `bin`/`externals` for the new version. With the listener as
# the entrypoint's only child, that exit ends the container, `--restart always`
# restarts it, and the updater is killed mid-copy after it has already moved `bin`
# aside — every later start then exits 127 on a missing ./bin/Runner.Listener,
# config.sh included (2026-09-03: 631 restarts, jobs queued for hours, recovery =
# rebuild + `docker rm` because the damage is in the writable layer). run.sh's
# helper instead waits for the updater's `update.finished` flag and relaunches the
# new listener in place — verified 2026-09-04 on a 2.336.0 base: updated to
# 2.337.0 and back to "Listening for Jobs" in 80 s, RestartCount 0. The ephemeral
# flow is unchanged (listener exits 0 after a job -> run.sh exits 0 -> entrypoint
# deregisters -> `--restart always` re-registers). `--startuptype service` is kept
# from the base CMD; the runner passes restartInteractiveRunner=false regardless,
# so run.sh's loop is the only relauncher (no second listener from the updater).
CMD ["./run.sh", "--startuptype", "service"]
