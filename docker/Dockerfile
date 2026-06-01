FROM ubuntu:24.04

ENV DEBIAN_FRONTEND=noninteractive

# Apache Arrow is not in Ubuntu default repos; use the official APT source.
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

ENV MTDD_LISTEN=0.0.0.0:50051
ENV MTDD_HOST_INDEX=0
ENV MTDD_PG_HOST=postgres

EXPOSE 50051
CMD ["/src/build/mtdd_server"]
