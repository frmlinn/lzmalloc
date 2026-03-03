# =========================================================================
# STAGE 1: Ultra-lightweight Build Environment (Ubuntu 24.04)
# =========================================================================
FROM ubuntu:24.04 AS builder

ENV DEBIAN_FRONTEND=noninteractive

RUN apt-get update && apt-get install -y --no-install-recommends \
    clang \
    cmake \
    make \
    libc6-dev \
    git \
    && rm -rf /var/lib/apt/lists/*

WORKDIR /lzmalloc_src
COPY . .

# Validamos el motor antes de empaquetar
RUN make test CC=clang
# Compilamos la versión final
RUN make release CC=clang

# =========================================================================
# STAGE 2: Artifact Export
# =========================================================================
FROM ubuntu:24.04

# Exportamos solo el binario final
COPY --from=builder /lzmalloc_src/build/release/liblzmalloc.so /usr/local/lib/liblzmalloc.so

ENV LZMALLOC_SO="/usr/local/lib/liblzmalloc.so"

# No hay CMD por defecto para actuar como imagen base de librería, 
# o puedes dejar bash para debug.
CMD ["/bin/bash"]