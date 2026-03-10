FROM ubuntu:22.04

ENV DEBIAN_FRONTEND=noninteractive

RUN apt-get update \
 && apt-get install -y --no-install-recommends \
    build-essential \
    ca-certificates \
    cmake \
    git \
    libcurl4-openssl-dev \
    pkg-config \
    rapidjson-dev \
    zip \
 && rm -rf /var/lib/apt/lists/*

COPY tools/docker/container-build-linux.sh /usr/local/bin/container-build-linux.sh
RUN chmod +x /usr/local/bin/container-build-linux.sh

ENTRYPOINT ["/usr/local/bin/container-build-linux.sh"]
