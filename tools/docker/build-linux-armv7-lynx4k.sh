#!/usr/bin/env bash
set -euo pipefail

script_dir=$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)
addon_root=$(cd "${script_dir}/../.." && pwd)
workspace_root=$(cd "${addon_root}/.." && pwd)
xbmc_root="${workspace_root}/xbmc"
output_root="${addon_root}/build/Lynx4k"
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

docker run --rm \
  --platform linux/amd64 \
  --user "$(id -u):$(id -g)" \
  -e BUILD_TYPE="${BUILD_TYPE:-Release}" \
  -e XBMC_REF="${XBMC_REF:-origin/Nexus}" \
  -e PVR_EON_REF="${PVR_EON_REF:-}" \
  -e LINUX_HOST="${LINUX_HOST:-arm-linux-gnueabihf}" \
  -e LINUX_RENDER_SYSTEM="${LINUX_RENDER_SYSTEM:-gles}" \
  -e LINUX_TOOLCHAIN="${LINUX_TOOLCHAIN:-/usr}" \
  -v "${xbmc_root}:/src/xbmc:ro" \
  -v "${addon_root}:/src/pvr.eon:ro" \
  -v "${output_root}:/out" \
  "${image_tag}"
