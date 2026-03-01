# =========================================================================
# STAGE 1: Ultra-lightweight Build Environment (Ubuntu 24.04)
# =========================================================================
FROM ubuntu:24.04 AS builder

ENV DEBIAN_FRONTEND=noninteractive

# Install ONLY strictly necessary packages without recommended bloat
RUN apt-get update && apt-get install -y --no-install-recommends \
    clang \
    cmake \
    make \
    libc6-dev \
    && rm -rf /var/lib/apt/lists/*

WORKDIR /lzmalloc_src
COPY . .
RUN make release

# =========================================================================
# STAGE 2: Execution Environment (Clean and pristine)
# =========================================================================
FROM ubuntu:24.04

# Copy the compiled library (Ubuntu base image already includes 'ls' and 'sh')
COPY --from=builder /lzmalloc_src/build/release/liblzmalloc.so /usr/local/lib/liblzmalloc.so

ENV LZMALLOC_SO="/usr/local/lib/liblzmalloc.so"

# Quick Smoke Test using built-in system utilities
CMD ["sh", "-c", "echo '\n=== Starting POSIX Smoke Test (Ubuntu Minimal) ===' && LD_PRELOAD=$LZMALLOC_SO ls -laR /usr > /dev/null && echo '=== Smoke Test Completed Successfully ===\n'"]