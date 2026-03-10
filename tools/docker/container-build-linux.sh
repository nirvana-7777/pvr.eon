#!/usr/bin/env bash
set -euo pipefail

: "${PVR_EON_ID:=pvr.eon}"
: "${XBMC_REF:=origin/Omega}"
: "${BUILD_TYPE:=Debug}"

src_root=/src
work_root=/tmp/pvr-eon-build
xbmc_src="${src_root}/xbmc"
addon_src="${src_root}/pvr.eon"
xbmc_work="${work_root}/xbmc"
addon_work="${work_root}/pvr.eon"
definition_dir="${work_root}/definition"
build_dir="${work_root}/build"
output_root=/out
install_dir="${output_root}/install"
package_dir="${output_root}/zips"

if [[ ! -d "${xbmc_src}/.git" ]]; then
  echo "Expected a git checkout at ${xbmc_src}" >&2
  exit 1
fi

if [[ ! -f "${addon_src}/CMakeLists.txt" ]]; then
  echo "Expected addon sources at ${addon_src}" >&2
  exit 1
fi

rm -rf "${work_root}"
rm -rf "${install_dir}" "${package_dir}"
mkdir -p \
  "${definition_dir}/${PVR_EON_ID}" \
  "${build_dir}" \
  "${install_dir}" \
  "${package_dir}"

cp -a "${xbmc_src}" "${xbmc_work}"
cp -a "${addon_src}" "${addon_work}"

# Keep local build output from leaking into the packaged addon.
rm -rf "${addon_work}/build"

if ! git -C "${xbmc_work}" rev-parse --verify "${XBMC_REF}" >/dev/null 2>&1; then
  echo "Missing xbmc ref '${XBMC_REF}' in the mounted repo." >&2
  echo "Fetch the Omega branch into the local xbmc clone first." >&2
  exit 1
fi

git -C "${xbmc_work}" checkout --detach "${XBMC_REF}" >/dev/null

printf '%s . .\n' "${PVR_EON_ID}" > "${definition_dir}/${PVR_EON_ID}/${PVR_EON_ID}.txt"
printf 'all\n' > "${definition_dir}/${PVR_EON_ID}/platforms.txt"

cmake \
  -S "${xbmc_work}/cmake/addons" \
  -B "${build_dir}" \
  -DADDONS_TO_BUILD="${PVR_EON_ID}" \
  -DADDONS_DEFINITION_DIR="${definition_dir}" \
  -DADDON_SRC_PREFIX="${work_root}" \
  -DCMAKE_BUILD_TYPE="${BUILD_TYPE}" \
  -DCMAKE_INSTALL_PREFIX="${install_dir}" \
  -DPACKAGE_DIR="${package_dir}" \
  -DPACKAGE_ZIP=1

cmake --build "${build_dir}" --target "${PVR_EON_ID}" --parallel "$(nproc)"
cmake --build "${build_dir}" --target "package-${PVR_EON_ID}" --parallel "$(nproc)"

git -C "${xbmc_work}" rev-parse HEAD > "${output_root}/xbmc-commit.txt"
git -C "${addon_work}" rev-parse HEAD > "${output_root}/pvr-eon-commit.txt"
