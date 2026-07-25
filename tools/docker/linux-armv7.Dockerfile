FROM ubuntu:22.04

ENV DEBIAN_FRONTEND=noninteractive

RUN apt-get update \
 && apt-get install -y --no-install-recommends \
    autoconf \
    automake \
    binutils-arm-linux-gnueabihf \
    build-essential \
    ca-certificates \
    cmake \
    curl \
    flex \
    g++-arm-linux-gnueabihf \
    gawk \
    gcc-arm-linux-gnueabihf \
    git \
    libtool \
    libcurl4-openssl-dev \
    pkg-config \
    python3 \
    rapidjson-dev \
    zip \
 && rm -rf /var/lib/apt/lists/*

COPY tools/docker/container-build-linux-armv7.sh /usr/local/bin/container-build-linux-armv7.sh
RUN chmod +x /usr/local/bin/container-build-linux-armv7.sh

ENTRYPOINT ["/usr/local/bin/container-build-linux-armv7.sh"]
