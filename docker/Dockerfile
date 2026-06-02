FROM ubuntu:24.04 AS builder

ENV DEBIAN_FRONTEND=noninteractive

RUN apt-get update && apt-get install -y --no-install-recommends \
    ca-certificates \
    wget \
    lsb-release \
    && wget -q https://packages.apache.org/artifactory/arrow/ubuntu/apache-arrow-apt-source-latest-noble.deb \
    && apt-get install -y ./apache-arrow-apt-source-latest-noble.deb \
    && rm -f ./apache-arrow-apt-source-latest-noble.deb \
    && apt-get update \
    && apt-get install -y --no-install-recommends \
    build-essential \
    cmake \
    pkg-config \
    protobuf-compiler \
    protobuf-compiler-grpc \
    libgrpc++-dev \
    libprotobuf-dev \
    libpq-dev \
    libarrow-dev \
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
    wget \
    lsb-release \
    && wget -q https://packages.apache.org/artifactory/arrow/ubuntu/apache-arrow-apt-source-latest-noble.deb \
    && apt-get install -y ./apache-arrow-apt-source-latest-noble.deb \
    && rm -f ./apache-arrow-apt-source-latest-noble.deb \
    && apt-get update \
    && apt-get install -y --no-install-recommends \
    libgrpc++1.51t64 \
    libprotobuf32t64 \
    libpq5 \
    libarrow2400 \
    libflatbuffers-dev \
    ca-certificates \
    && rm -rf /var/lib/apt/lists/* \
    && useradd -r -u 10001 -s /usr/sbin/nologin mtdd

COPY --from=builder /src/build/mtdd_server /usr/local/bin/mtdd_server

USER mtdd
ENV MTDD_LISTEN=127.0.0.1:50051
EXPOSE 50051
HEALTHCHECK --interval=30s --timeout=5s --start-period=10s --retries=3 \
  CMD pgrep -x mtdd_server >/dev/null || exit 1
CMD ["/usr/local/bin/mtdd_server"]
