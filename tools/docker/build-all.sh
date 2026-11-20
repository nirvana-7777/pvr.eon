#!/usr/bin/env bash
#
# One-shot build orchestrator: runs every Docker build from this repository,
# Android first, then Linux, then the Kodi 20 "Nexus" armv7 variant.
#
# ============================== HOW TO RUN ==================================
# Native Linux:
#
#   ./tools/docker/build-all.sh [--keep-build]
#
# Windows / WSL (Docker Desktop running, WSL integration on) -- go to the
# repo on the Windows disk and pipe the script through tr, which strips the
# Windows CRLF line endings that would otherwise break bash:
#
#   wsl
#   cd "/mnt/<disk>/<path-to>/pvr.eon"
#   tr -d '\r' < tools/docker/build-all.sh | bash
#
# (pass flags as: tr -d '\r' < tools/docker/build-all.sh | bash -s -- --keep-build)
#
# When started from a Windows mount (/mnt/...), the script copies the source
# into ~/<repo-name> and builds there (Docker builds on the Windows mount are
# slow and permission-broken), then collects the zips BACK into build-zips/
# next to the source on the Windows disk. Run natively, it builds in place
# and the zips land in <repo>/build-zips/.
#
# --keep-build leaves the previous build/ directory in place instead of
# deleting it first. It does not make a re-run faster: the containers do
# their work in their own temporary directories, which are gone when they
# exit. Use the clean form for release builds.
# =============================================================================
#
# What it does, in order:
# 1. Checks Docker is reachable.
# 2. On a Windows mount: syncs the source to ~/<repo-name> (rsync copies only
#    what changed); fixes line endings and exec bits on the tools scripts.
# 3. Clones a sibling xbmc checkout (shallow) ONLY if it is missing, and
#    fetches origin/Omega and origin/Nexus only if they are absent.
# 4. Removes the previous build/ output (unless --keep-build).
# 5. Runs: android-aarch64, android-armv7, linux-amd64, linux-aarch64,
#    linux-armv7, linux-armv7-nexus. A failure does not stop the rest.
# 6. Prints an OK/FAILED summary and collects the zips into build-zips/,
#    renamed to <id>-<version>+<platform>.zip with the same +<platform>
#    suffix written into the version in the zip's addon.xml -- the naming
#    the repository uses. The zips under build/ stay exactly as built.

set -uo pipefail

keep_build=0
if [[ "${1:-}" == "--keep-build" ]]; then
  keep_build=1
fi

# Where is the repo this script belongs to? When run as a file, next to the
# script itself; when piped through tr/bash, the current directory.
if [[ -n "${BASH_SOURCE[0]:-}" && -f "${BASH_SOURCE[0]}" ]]; then
  script_path=$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)
  src_root=$(cd "${script_path}/../.." && pwd)
else
  src_root=$(pwd)
fi
if [[ ! -f "${src_root}/tools/docker/build-all.sh" ]]; then
  echo "Cannot locate the repo: run this script from the repo root" >&2
  echo "(the folder that contains tools/docker/)." >&2
  exit 1
fi

echo "== Prerequisites =="

if ! docker info >/dev/null 2>&1; then
  echo "Docker is not reachable. Start the Docker daemon (or Docker Desktop) and rerun." >&2
  exit 1
fi
echo "Docker: OK"

# On a Windows mount, build in the Linux filesystem and copy the zips back.
if [[ "${src_root}" == /mnt/* ]]; then
  build_root="${HOME}/$(basename "${src_root}")"
  echo "Windows mount detected -- syncing ${src_root} -> ${build_root}"
  if command -v rsync >/dev/null 2>&1; then
    rsync -a --delete --exclude 'build/' --exclude 'build-zips/' \
      "${src_root}/" "${build_root}/"
  else
    rm -rf "${build_root}"
    mkdir -p "${build_root}"
    cp -r "${src_root}/." "${build_root}/"
    rm -rf "${build_root}/build" "${build_root}/build-zips"
  fi
else
  build_root="${src_root}"
fi
zips_out="${src_root}/build-zips"
workspace_root=$(cd "${build_root}/.." && pwd)
xbmc_root="${workspace_root}/xbmc"
script_dir="${build_root}/tools/docker"

# A copy that went through Windows reintroduces CRLF and strips the exec bit.
mapfile -t shell_scripts < <(find "${build_root}/tools" -name '*.sh')
if command -v dos2unix >/dev/null 2>&1; then
  dos2unix -q "${shell_scripts[@]}"
else
  sed -i 's/\r$//' "${shell_scripts[@]}"
fi
chmod +x "${shell_scripts[@]}"
echo "Scripts: LF + executable"

if [[ ! -d "${xbmc_root}/.git" ]]; then
  echo "No xbmc checkout at ${xbmc_root} -- cloning Omega (shallow)..."
  git clone --branch Omega --depth 1 https://github.com/xbmc/xbmc.git "${xbmc_root}"
fi
if ! git -C "${xbmc_root}" rev-parse --verify origin/Omega >/dev/null 2>&1; then
  echo "Fetching origin/Omega into the xbmc checkout..."
  git -C "${xbmc_root}" fetch --depth 1 origin Omega:refs/remotes/origin/Omega
fi
echo "xbmc origin/Omega: OK"
if ! git -C "${xbmc_root}" rev-parse --verify origin/Nexus >/dev/null 2>&1; then
  echo "Fetching origin/Nexus into the xbmc checkout..."
  git -C "${xbmc_root}" fetch --depth 1 origin Nexus:refs/remotes/origin/Nexus
fi
echo "xbmc origin/Nexus: OK"

if [[ "${keep_build}" -eq 0 && -d "${build_root}/build" ]]; then
  echo "Removing previous build output: ${build_root}/build"
  rm -rf "${build_root}/build"
elif [[ "${keep_build}" -eq 1 ]]; then
  echo "Keeping previous build output (--keep-build)"
fi

targets=(
  build-android-aarch64.sh
  build-android-armv7.sh
  build-linux-amd64.sh
  build-linux-aarch64.sh
  build-linux-armv7.sh
  build-linux-armv7-nexus.sh
)

declare -A results
failed=0

for target in "${targets[@]}"; do
  echo
  echo "========================================================"
  echo "== ${target}"
  echo "========================================================"
  if "${script_dir}/${target}"; then
    results[${target}]="OK"
  else
    results[${target}]="FAILED"
    failed=1
  fi
done

echo
echo "== Summary =="
for target in "${targets[@]}"; do
  printf '  %-32s %s\n' "${target}" "${results[${target}]}"
done

echo
echo "== Produced zips =="
rm -rf "${zips_out}"
mkdir -p "${zips_out}"
found_any=0
while IFS= read -r zipfile; do
  # Platform = the docker output folder the zip was built in
  # (build/docker-<platform>/...), which matches the repository naming:
  # android-aarch64, android-armv7, linux-amd64, linux-aarch64,
  # linux-armv7, linux-armv7-nexus.
  rel="${zipfile#"${build_root}"/build/}"
  plat="${rel%%/*}"
  plat="${plat#docker-}"
  # The Kodi 20 build is distinguished by its 20.x version, not by the
  # platform suffix, so its folder maps to the same platform name.
  plat="${plat%-nexus}"
  if command -v python3 >/dev/null 2>&1; then
    # Repack with the +<platform> suffix in the zip name AND in the version
    # attribute of the addon.xml inside, preserving entry timestamps and
    # permissions. The original zip under build/ is left untouched.
    out_name=$(python3 - "${zipfile}" "${zips_out}" "${plat}" <<'PYEOF'
import os, re, sys, zipfile
src, outdir, plat = sys.argv[1:4]
with zipfile.ZipFile(src) as zin:
    infos = zin.infolist()
    xml_name = next(i.filename for i in infos
                    if i.filename.endswith("/addon.xml")
                    and i.filename.count("/") == 1)
    addon_id = xml_name.split("/")[0]
    xml = zin.read(xml_name).decode("utf-8")
    m = re.search(r'(<addon\b[^>]*?\bversion=")([^"]+)(")', xml)
    base_ver = m.group(2).split("+")[0]
    xml = xml[:m.start()] + m.group(1) + base_ver + "+" + plat + m.group(3) + xml[m.end():]
    out = os.path.join(outdir, "%s-%s+%s.zip" % (addon_id, base_ver, plat))
    with zipfile.ZipFile(out, "w", zipfile.ZIP_DEFLATED) as zout:
        for info in infos:
            data = zin.read(info.filename)
            if info.filename == xml_name:
                data = xml.encode("utf-8")
            zout.writestr(info, data)
print(os.path.basename(out))
PYEOF
    )
    echo "  ${out_name}"
  else
    base=$(basename "${zipfile}" .zip)
    cp -f "${zipfile}" "${zips_out}/${base}+${plat}.zip"
    echo "  ${base}+${plat}.zip  (python3 not found: version inside addon.xml NOT suffixed)"
  fi
  found_any=1
done < <(find "${build_root}/build" -path '*/zips/*' -name '*.zip' 2>/dev/null)

if [[ ${found_any} -eq 1 ]]; then
  echo
  echo "All zips collected in: ${zips_out}"
else
  echo "  (none found)"
fi

exit ${failed}
