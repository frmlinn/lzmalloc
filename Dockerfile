# ==============================================================================
# @file Dockerfile
# @brief Multi-stage containerized build environment for lzmalloc V2.
# @details Implements a reproducible compilation environment using Clang/LLVM.
# Separates the heavy build toolchain from the lightweight export artifact.
# ==============================================================================

# ------------------------------------------------------------------------------
# @stage 1: Builder
# @brief Installs the LLVM toolchain and compiles the memory allocator.
# ------------------------------------------------------------------------------
FROM ubuntu:24.04 AS builder

LABEL maintainer="frmlinn"
LABEL description="Compilation stage for lzmalloc 0.1.0"

ENV DEBIAN_FRONTEND=noninteractive

# Install core system dependencies and strict Clang toolchain
RUN apt-get update && apt-get install -y --no-install-recommends \
    clang \
    lld \
    llvm \
    cmake \
    make \
    libc6-dev \
    git \
    binutils \
    valgrind \
    gdb \
    && rm -rf /var/lib/apt/lists/*

WORKDIR /lzmalloc_src

# Copy project definition (Leveraging Docker Layer Caching)
COPY external/ external/
COPY include/ include/
COPY src/ src/
COPY tests/ tests/
COPY CMakeLists.txt Makefile lz_config.h.in ./

# Execute the deterministic release build
RUN make release CC=clang

# Execute the debug build for the test suite target
RUN make debug CC=clang

# ------------------------------------------------------------------------------
# @stage 2: Artifact Exporter
# @brief Pristine runtime environment containing only the compiled shared object.
# ------------------------------------------------------------------------------
FROM ubuntu:24.04 AS runtime

# Copy the generated shared library from the builder stage
COPY --from=builder /lzmalloc_src/build/release/liblzmalloc.so /usr/local/lib/liblzmalloc.so

# Environment variables for seamless library injection
ENV LZMALLOC_SO="/usr/local/lib/liblzmalloc.so"
ENV LD_PRELOAD="/usr/local/lib/liblzmalloc.so"

# Default entrypoint drops the user into a bash shell with lzmalloc preloaded
CMD ["/bin/bash"]