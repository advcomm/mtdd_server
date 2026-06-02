FROM ubuntu:24.04 AS builder

ENV DEBIAN_FRONTEND=noninteractive

RUN apt-get update && apt-get install -y --no-install-recommends \
    ca-certificates \
    build-essential \
    cmake \
    pkg-config \
    protobuf-compiler \
    protobuf-compiler-grpc \
    libgrpc++-dev \
    libprotobuf-dev \
    libpq-dev \
    libflatbuffers-dev \
    libgtest-dev \
    && rm -rf /var/lib/apt/lists/*

WORKDIR /src
COPY . .

RUN cmake -B build -DCMAKE_BUILD_TYPE=Release -DMTDD_BUILD_TESTS=ON \
    && cmake --build build -j"$(nproc)" \
    && ctest --test-dir build --output-on-failure

FROM ubuntu:24.04 AS runtime

ENV DEBIAN_FRONTEND=noninteractive

RUN apt-get update && apt-get install -y --no-install-recommends \
    ca-certificates \
    libgrpc++1.51t64 \
    libprotobuf32t64 \
    libpq5 \
    libflatbuffers-dev \
    ca-certificates \
    && rm -rf /var/lib/apt/lists/* \
    && useradd -r -u 10001 -s /usr/sbin/nologin mtdd \
    && mkdir -p /run/mtdd && chown mtdd:mtdd /run/mtdd

COPY --from=builder /src/build/mtdd_server /usr/local/bin/mtdd_server

USER mtdd
ENV MTDD_LISTEN=unix:/run/mtdd/grpc.sock
ENV MTDD_UNIX_SOCKET_MODE=666
CMD ["/usr/local/bin/mtdd_server"]
