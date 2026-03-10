#!/usr/bin/env bash
set -euo pipefail

script_dir=$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)
addon_root=$(cd "${script_dir}/../.." && pwd)
workspace_root=$(cd "${addon_root}/.." && pwd)
xbmc_root="${workspace_root}/xbmc"
output_root="${addon_root}/build/docker-android-aarch64"
image_tag="pvr-eon-builder:android-aarch64"
android_platform="${ANDROID_PLATFORM:-android-36}"
android_build_tools="${ANDROID_BUILD_TOOLS:-36.0.0}"
android_ndk_version="${ANDROID_NDK_VERSION:-28.2.13676358}"

if [[ ! -d "${xbmc_root}/.git" ]]; then
  echo "Expected xbmc checkout at ${xbmc_root}" >&2
  exit 1
fi

if ! git -C "${xbmc_root}" rev-parse --verify origin/Omega >/dev/null 2>&1; then
  echo "The local xbmc checkout does not have origin/Omega." >&2
  echo "Fetch it first, then rerun this script." >&2
  exit 1
fi

mkdir -p "${output_root}"

docker build \
  --platform linux/amd64 \
  --build-arg "ANDROID_PLATFORM=${android_platform}" \
  --build-arg "ANDROID_BUILD_TOOLS=${android_build_tools}" \
  --build-arg "ANDROID_NDK_VERSION=${android_ndk_version}" \
  -t "${image_tag}" \
  -f "${script_dir}/android-aarch64.Dockerfile" \
  "${addon_root}"

docker run --rm \
  --platform linux/amd64 \
  --user "$(id -u):$(id -g)" \
  -e BUILD_TYPE="${BUILD_TYPE:-Release}" \
  -e XBMC_REF="${XBMC_REF:-origin/Omega}" \
  -e ANDROID_NDK_API="${ANDROID_NDK_API:-24}" \
  -e ANDROID_NDK_VERSION="${android_ndk_version}" \
  -v "${xbmc_root}:/src/xbmc:ro" \
  -v "${addon_root}:/src/pvr.eon:ro" \
  -v "${output_root}:/out" \
  "${image_tag}"
