FROM ubuntu:22.04

ENV DEBIAN_FRONTEND=noninteractive

RUN apt-get update \
 && apt-get install -y --no-install-recommends \
    autoconf \
    automake \
    binutils-aarch64-linux-gnu \
    build-essential \
    ca-certificates \
    cmake \
    curl \
    flex \
    g++-aarch64-linux-gnu \
    gawk \
    gcc-aarch64-linux-gnu \
    git \
    libtool \
    libcurl4-openssl-dev \
    pkg-config \
    python3 \
    rapidjson-dev \
    zip \
 && rm -rf /var/lib/apt/lists/*

COPY tools/docker/container-build-linux-aarch64.sh /usr/local/bin/container-build-linux-aarch64.sh
RUN chmod +x /usr/local/bin/container-build-linux-aarch64.sh

ENTRYPOINT ["/usr/local/bin/container-build-linux-aarch64.sh"]
