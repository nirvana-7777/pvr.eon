#!/usr/bin/env bash
set -euo pipefail

: "${PVR_EON_ID:=pvr.eon}"
: "${XBMC_REF:=origin/Omega}"
: "${PVR_EON_REF:=}"
: "${BUILD_TYPE:=Release}"
: "${LINUX_HOST:=aarch64-linux-gnu}"
: "${LINUX_RENDER_SYSTEM:=gles}"
: "${LINUX_TOOLCHAIN:=/usr}"

src_root=/src
work_root=/tmp/pvr-eon-linux-aarch64-build
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

# ---------------------------------------------------------------------------
# Normalize what gets shipped. Kodi's packaging writes the library as a
# versioned chain (pvr.eon.so -> pvr.eon.so.<kodi> -> pvr.eon.so.<version>)
# with addon.xml naming the fully versioned file; in a zip the links survive
# only as copies, and any chain/addon.xml mismatch is fatal at load time
# ("Could not locate pvr.eon.so.<version>"). A single unversioned library
# named by addon.xml cannot mismatch anything, and its name no longer changes
# from release to release. The kodi.binary.* imports the build injects are
# also aligned with the hand-written ones, with no blank line after
# <requires>. On targets whose library has another naming scheme (Android's
# libpvr.eon.so) the flattening finds nothing and only the formatting runs.
normalize_addon_dir() {
  local addon_dir=$1
  local xml="${addon_dir}/addon.xml"
  [[ -f "${xml}" ]] || return 0

  # The real library is the largest regular file of the chain: copies made
  # from the links match its size, links stored as links resolve through
  # readlink and a dangling one is skipped.
  local real_so="" candidate resolved
  while IFS= read -r candidate; do
    resolved=$(readlink -f "${candidate}") || continue
    [[ -f "${resolved}" ]] || continue
    if [[ -z "${real_so}" || $(stat -c%s "${resolved}") -gt $(stat -c%s "${real_so}") ]]; then
      real_so="${resolved}"
    fi
  done < <(find "${addon_dir}" -maxdepth 1 -name "${PVR_EON_ID}.so*")

  if [[ -n "${real_so}" ]]; then
    local keep="${addon_dir}/${PVR_EON_ID}.so.keep"
    cp -f "${real_so}" "${keep}"
    find "${addon_dir}" -maxdepth 1 -name "${PVR_EON_ID}.so*" ! -name "*.keep" -exec rm -f {} +
    mv "${keep}" "${addon_dir}/${PVR_EON_ID}.so"
    sed -i "s/library_linux=\"[^\"]*\"/library_linux=\"${PVR_EON_ID}.so\"/" "${xml}"
  fi

  # No blank (whitespace-only) lines anywhere EXCEPT inside <news>, whose
  # blank lines separate version entries and are meaningful.
  sed -i '/<news>/,/<\/news>/!{/^[[:space:]]*$/d}' "${xml}"
  sed -i 's/^<import addon="kodi\./\t\t<import addon="kodi./' "${xml}"
}

normalize_zip() {
  # Absolute path: the function cd-s around, a relative path would break.
  local zipfile
  zipfile=$(readlink -f "$1")
  local tmp="${work_root}/normalize"
  rm -rf "${tmp}"
  mkdir -p "${tmp}"
  (cd "${tmp}" && unzip -q "${zipfile}")
  normalize_addon_dir "${tmp}/${PVR_EON_ID}"
  rm -f "${zipfile}"
  (cd "${tmp}" && zip -qry "${zipfile}" "${PVR_EON_ID}")
  rm -rf "${tmp}"
}

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

git -C "${xbmc_work}" checkout --detach -f "${XBMC_REF}" >/dev/null

if [[ -n "${PVR_EON_REF}" ]]; then
  if ! git -C "${addon_work}" rev-parse --verify "${PVR_EON_REF}" >/dev/null 2>&1; then
    echo "Missing addon ref '${PVR_EON_REF}' in the mounted repo." >&2
    echo "Fetch the requested branch into the local pvr.eon clone first." >&2
    exit 1
  fi

  git -C "${addon_work}" checkout --detach -f "${PVR_EON_REF}" >/dev/null
fi

printf '%s . .\n' "${PVR_EON_ID}" > "${definition_dir}/${PVR_EON_ID}/${PVR_EON_ID}.txt"
printf 'linux\n' > "${definition_dir}/${PVR_EON_ID}/platforms.txt"

(
  cd "${xbmc_work}/tools/depends"
  ./bootstrap
  ./configure \
    --with-tarballs="${tarballs_dir}" \
    --host="${LINUX_HOST}" \
    --enable-debug="${depends_debug_flag}" \
    --with-toolchain="${LINUX_TOOLCHAIN}" \
    --with-rendersystem="${LINUX_RENDER_SYSTEM}" \
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

normalize_addon_dir "${install_dir}/${PVR_EON_ID}"
# The packaging target nests the zip one level deeper (zips/<addon-id>/),
# so walk the whole tree instead of globbing the top level.
found_zip=0
while IFS= read -r packaged_zip; do
  echo "Normalizing $(basename "${packaged_zip}")"
  normalize_zip "${packaged_zip}"
  found_zip=1
done < <(find "${package_dir}" -type f -name '*.zip')
if [[ "${found_zip}" -eq 0 ]]; then
  echo "WARNING: no addon zip found under ${package_dir} -- nothing was normalized" >&2
fi

git -C "${xbmc_work}" rev-parse HEAD > "${output_root}/xbmc-commit.txt"
# The addon tree may come from an archive without .git; skip the hash quietly.
git -C "${addon_work}" rev-parse HEAD > "${output_root}/pvr-eon-commit.txt" 2>/dev/null || true
cat > "${output_root}/linux-build-info.txt" <<EOF
build_type=${BUILD_TYPE}
linux_host=${LINUX_HOST}
linux_rendersystem=${LINUX_RENDER_SYSTEM}
linux_toolchain=${LINUX_TOOLCHAIN}
EOF
