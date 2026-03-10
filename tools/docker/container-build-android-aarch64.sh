#!/usr/bin/env bash
set -euo pipefail

: "${PVR_EON_ID:=pvr.eon}"
: "${XBMC_REF:=origin/Omega}"
: "${BUILD_TYPE:=Release}"
: "${ANDROID_HOST:=aarch64-linux-android}"
: "${ANDROID_NDK_API:=24}"
: "${ANDROID_NDK_VERSION:=28.2.13676358}"
: "${ANDROID_SDK_ROOT:=/opt/android-sdk}"

src_root=/src
work_root=/tmp/pvr-eon-android-build
xbmc_src="${src_root}/xbmc"
addon_src="${src_root}/pvr.eon"
xbmc_work="${work_root}/xbmc"
addon_work="${work_root}/pvr.eon"
definition_dir="${work_root}/definition"
addon_depends="${work_root}/addon-depends"
depends_prefix="${work_root}/kodi-depends"
build_dir="${work_root}/build"
tarballs_dir="${work_root}/tarballs"
toolchain_template="${xbmc_work}/tools/depends/target/Toolchain_binaddons.cmake"
toolchain_file="${addon_depends}/share/Toolchain_binaddons.cmake"
output_root=/out
install_dir="${output_root}/install"
package_dir="${output_root}/zips"

build_type_lc=$(printf '%s' "${BUILD_TYPE}" | tr '[:upper:]' '[:lower:]')
if [[ "${build_type_lc}" == "debug" ]]; then
  depends_debug_flag="yes"
else
  depends_debug_flag="no"
fi

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
  "${addon_depends}/share" \
  "${build_dir}" \
  "${install_dir}" \
  "${package_dir}" \
  "${tarballs_dir}"

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
printf 'android\n' > "${definition_dir}/${PVR_EON_ID}/platforms.txt"

(
  cd "${xbmc_work}/tools/depends"
  ./bootstrap
  ./configure \
    --with-tarballs="${tarballs_dir}" \
    --host="${ANDROID_HOST}" \
    --enable-debug="${depends_debug_flag}" \
    --with-sdk-path="${ANDROID_SDK_ROOT}" \
    --with-ndk-path="${ANDROID_SDK_ROOT}/ndk/${ANDROID_NDK_VERSION}" \
    --with-ndk-api="${ANDROID_NDK_API}" \
    --prefix="${depends_prefix}"
)

if [[ ! -f "${toolchain_template}" ]]; then
  echo "Kodi did not generate ${toolchain_template}" >&2
  exit 1
fi

sed "s|@CMAKE_FIND_ROOT_PATH@|${addon_depends}|g" \
  "${toolchain_template}" > "${toolchain_file}"

cmake \
  -S "${xbmc_work}/cmake/addons" \
  -B "${build_dir}" \
  -DADDONS_TO_BUILD="${PVR_EON_ID}" \
  -DADDONS_DEFINITION_DIR="${definition_dir}" \
  -DADDON_SRC_PREFIX="${work_root}" \
  -DADDON_DEPENDS_PATH="${addon_depends}" \
  -DCORE_SOURCE_DIR="${xbmc_work}" \
  -DCMAKE_TOOLCHAIN_FILE="${toolchain_file}" \
  -DCMAKE_BUILD_TYPE="${BUILD_TYPE}" \
  -DCMAKE_INSTALL_PREFIX="${install_dir}" \
  -DPACKAGE_DIR="${package_dir}" \
  -DPACKAGE_ZIP=1

cmake --build "${build_dir}" --target "${PVR_EON_ID}" --parallel "$(nproc)"
cmake --build "${build_dir}" --target "package-${PVR_EON_ID}" --parallel "$(nproc)"

git -C "${xbmc_work}" rev-parse HEAD > "${output_root}/xbmc-commit.txt"
git -C "${addon_work}" rev-parse HEAD > "${output_root}/pvr-eon-commit.txt"
cat > "${output_root}/android-build-info.txt" <<EOF
build_type=${BUILD_TYPE}
android_host=${ANDROID_HOST}
android_ndk_api=${ANDROID_NDK_API}
android_ndk_version=${ANDROID_NDK_VERSION}
android_sdk_root=${ANDROID_SDK_ROOT}
EOF
