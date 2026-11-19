#!/usr/bin/env bash
# Linux armv7 build against Kodi 20 "Nexus" -- for 32-bit boxes still on
# Kodi 20: RPi4 on 32-bit LibreELEC 11, the Lynx 4K, and similar. The regular
# build-linux-armv7.sh targets Omega; everything else is identical.
set -euo pipefail

script_dir=$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)
addon_root=$(cd "${script_dir}/../.." && pwd)
workspace_root=$(cd "${addon_root}/.." && pwd)
xbmc_root="${workspace_root}/xbmc"
output_root="${addon_root}/build/docker-linux-armv7-nexus"
image_tag="pvr-eon-builder:linux-armv7"

if [[ ! -d "${xbmc_root}/.git" ]]; then
  echo "Expected xbmc checkout at ${xbmc_root}" >&2
  exit 1
fi

if ! git -C "${xbmc_root}" rev-parse --verify origin/Nexus >/dev/null 2>&1; then
  echo "The local xbmc checkout does not have origin/Nexus." >&2
  echo "Fetch it first, then rerun this script." >&2
  exit 1
fi

mkdir -p "${output_root}"

docker build \
  --platform linux/amd64 \
  -t "${image_tag}" \
  -f "${script_dir}/linux-armv7.Dockerfile" \
  "${addon_root}"

# HOME: the --user uid has no home directory inside the image, so it
# defaults to the unwritable "/"; RapidJSON's CMake export then warns
# that it cannot create its package registry file there.
docker run --rm \
  --platform linux/amd64 \
  --user "$(id -u):$(id -g)" \
  -e HOME=/tmp \
  -e BUILD_TYPE="${BUILD_TYPE:-Release}" \
  -e XBMC_REF="${XBMC_REF:-origin/Nexus}" \
  -e PVR_EON_REF="${PVR_EON_REF:-}" \
  -e PVR_EON_VERSION_MAJOR="${PVR_EON_VERSION_MAJOR:-20}" \
  -e LINUX_HOST="${LINUX_HOST:-arm-linux-gnueabihf}" \
  -e LINUX_RENDER_SYSTEM="${LINUX_RENDER_SYSTEM:-gles}" \
  -e LINUX_TOOLCHAIN="${LINUX_TOOLCHAIN:-/usr}" \
  -v "${xbmc_root}:/src/xbmc:ro" \
  -v "${addon_root}:/src/pvr.eon:ro" \
  -v "${output_root}:/out" \
  "${image_tag}"
