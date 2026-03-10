FROM ubuntu:22.04

ARG ANDROID_CMDLINE_TOOLS_URL=https://dl.google.com/android/repository/commandlinetools-linux-14742923_latest.zip
ARG ANDROID_PLATFORM=android-36
ARG ANDROID_BUILD_TOOLS=36.0.0
ARG ANDROID_NDK_VERSION=28.2.13676358

ENV DEBIAN_FRONTEND=noninteractive
ENV ANDROID_SDK_ROOT=/opt/android-sdk
ENV ANDROID_HOME=/opt/android-sdk
ENV PATH=/opt/android-sdk/cmdline-tools/latest/bin:/opt/android-sdk/platform-tools:/usr/local/sbin:/usr/local/bin:/usr/sbin:/usr/bin:/sbin:/bin

RUN apt-get update \
 && apt-get install -y --no-install-recommends \
    autoconf \
    bison \
    build-essential \
    ca-certificates \
    cmake \
    curl \
    flex \
    gawk \
    git \
    gperf \
    lib32stdc++6 \
    lib32z1 \
    lib32z1-dev \
    pkg-config \
    openjdk-17-jdk-headless \
    unzip \
    zip \
    zlib1g-dev \
 && rm -rf /var/lib/apt/lists/*

RUN mkdir -p /opt/android-sdk/cmdline-tools /tmp/android-sdk-download \
 && curl -fsSL "${ANDROID_CMDLINE_TOOLS_URL}" -o /tmp/android-sdk-download/commandlinetools.zip \
 && unzip -q /tmp/android-sdk-download/commandlinetools.zip -d /tmp/android-sdk-download \
 && mkdir -p /opt/android-sdk/cmdline-tools/latest \
 && cp -a /tmp/android-sdk-download/cmdline-tools/. /opt/android-sdk/cmdline-tools/latest/ \
 && yes | sdkmanager --sdk_root="${ANDROID_SDK_ROOT}" --licenses >/dev/null \
 && sdkmanager --sdk_root="${ANDROID_SDK_ROOT}" \
      "platform-tools" \
      "platforms;${ANDROID_PLATFORM}" \
      "build-tools;${ANDROID_BUILD_TOOLS}" \
      "ndk;${ANDROID_NDK_VERSION}" \
 && rm -rf /tmp/android-sdk-download

COPY tools/docker/container-build-android-aarch64.sh /usr/local/bin/container-build-android-aarch64.sh
RUN chmod +x /usr/local/bin/container-build-android-aarch64.sh

ENTRYPOINT ["/usr/local/bin/container-build-android-aarch64.sh"]
