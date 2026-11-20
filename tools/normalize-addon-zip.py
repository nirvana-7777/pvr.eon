#!/usr/bin/env python3
"""Normalize a packaged Kodi addon zip, in place.

Kodi's packaging ships the binary as a versioned chain (pvr.eon.so ->
pvr.eon.so.<kodi> -> pvr.eon.so.<version>, and the dylib equivalent on
macOS) with addon.xml naming the fully versioned file; in a zip the links
survive only as copies, and any chain/addon.xml mismatch is fatal at load
time. This keeps a single unversioned library named by addon.xml, so the
file name never changes between releases. It also aligns the kodi.binary.*
imports the build injects with the hand-written ones and drops the blank
line after <requires>. A zip whose library has no versioned chain (a
Windows .dll) only gets the formatting pass.

With --platform NAME a release copy is written as well, named
<id>-<version>+NAME.zip and carrying the same +NAME suffix on the version
in its addon.xml -- the form a repository serving several platforms needs,
since every platform's zip otherwise shares one name and one version. The
copy goes to --out-dir (default: next to the source zip).

Usage: normalize-addon-zip.py [--platform NAME [--out-dir DIR]] <zip> [<zip> ...]
       (stdlib only)
"""

import argparse
import os
import re
import sys
import zipfile

ADDON_ID = os.environ.get("PVR_EON_ID", "pvr.eon")


def normalize(zip_path: str) -> None:
    with zipfile.ZipFile(zip_path) as zf:
        infos = {info.filename: info for info in zf.infolist()
                 if not info.filename.endswith("/")}
        entries = {name: zf.read(name) for name in infos}

    xml_name = f"{ADDON_ID}/addon.xml"
    if xml_name not in entries:
        print(f"{zip_path}: no {xml_name}, left untouched")
        return

    # Flatten the library chain: the real library is the largest file of the
    # chain (copies made from links match it; links stored as links are tiny).
    for ext in (".so", ".dylib"):
        pattern = re.compile(
            rf"^{re.escape(ADDON_ID)}/(lib)?{re.escape(ADDON_ID)}(\.[0-9.]+)?{re.escape(ext)}(\.[0-9.]+)?$")
        chain = [name for name in entries if pattern.match(name)]
        if len(chain) < 2:
            continue
        real = max(chain, key=lambda name: len(entries[name]))
        flat = f"{ADDON_ID}/{ADDON_ID}{ext}"
        payload = entries[real]
        real_info = infos[real]
        for name in chain:
            del entries[name]
            del infos[name]
        entries[flat] = payload
        flat_info = zipfile.ZipInfo(flat, date_time=real_info.date_time)
        flat_info.external_attr = real_info.external_attr
        infos[flat] = flat_info
        xml = entries[xml_name].decode("utf-8")
        xml = re.sub(r'(library_(?:linux|osx))="[^"]*"',
                     rf'\1="{ADDON_ID}{ext}"', xml)
        entries[xml_name] = xml.encode("utf-8")
        print(f"{zip_path}: {len(chain)} library files -> {ADDON_ID}{ext}")

    # Formatting: no blank (whitespace-only) lines anywhere EXCEPT inside
    # <news>, whose blank lines separate version entries and are meaningful;
    # and the injected kodi.binary.* imports aligned with the hand-written
    # ones. The file may carry CRLF line endings when it was generated on a
    # Windows runner, so both endings are accepted and preserved.
    xml = entries[xml_name].decode("utf-8")
    news_blocks = []

    def stash_news(match):
        news_blocks.append(match.group(0))
        return f"\x00NEWS{len(news_blocks) - 1}\x00"

    xml = re.sub(r"<news>.*?</news>", stash_news, xml, flags=re.S)
    xml = re.sub(r"(?m)^[ \t]+\r?\n|^\r?\n", "", xml)
    xml = re.sub(r'(?m)^<import addon="kodi\.', '\t\t<import addon="kodi.', xml)
    for index, block in enumerate(news_blocks):
        xml = xml.replace(f"\x00NEWS{index}\x00", block)
    entries[xml_name] = xml.encode("utf-8")

    # Rewrite the zip keeping each entry's original timestamp and permission
    # bits, so normalizing changes nothing but what it is meant to change.
    tmp_path = zip_path + ".tmp"
    with zipfile.ZipFile(tmp_path, "w", zipfile.ZIP_DEFLATED) as zf:
        for name in sorted(entries):
            src = infos.get(name)
            info = zipfile.ZipInfo(name, date_time=src.date_time if src else (1980, 1, 1, 0, 0, 0))
            if src:
                info.external_attr = src.external_attr
            info.compress_type = zipfile.ZIP_DEFLATED
            zf.writestr(info, entries[name])
    os.replace(tmp_path, zip_path)
    print(f"{zip_path}: normalized")


def write_platform_copy(zip_path: str, platform: str, out_dir: str) -> str:
    """Write <id>-<version>+<platform>.zip with the suffixed version inside."""
    xml_name = f"{ADDON_ID}/addon.xml"
    with zipfile.ZipFile(zip_path) as zf:
        infos = zf.infolist()
        entries = {info.filename: zf.read(info.filename) for info in infos}

    xml = entries[xml_name].decode("utf-8")
    match = re.search(r'(<addon\b[^>]*?\bversion=")([^"]+)(")', xml)
    if not match:
        raise SystemExit(f"{zip_path}: no version attribute in {xml_name}")
    base_version = match.group(2).split("+")[0]
    xml = (xml[:match.start()] + match.group(1) + base_version + "+" + platform
           + match.group(3) + xml[match.end():])
    entries[xml_name] = xml.encode("utf-8")

    os.makedirs(out_dir, exist_ok=True)
    out_path = os.path.join(out_dir, f"{ADDON_ID}-{base_version}+{platform}.zip")
    with zipfile.ZipFile(out_path, "w", zipfile.ZIP_DEFLATED) as zf:
        for info in infos:
            zf.writestr(info, entries[info.filename])
    print(f"{zip_path}: release copy -> {out_path}")
    return out_path


def main() -> int:
    parser = argparse.ArgumentParser(
        description="Normalize packaged Kodi addon zips, in place.")
    parser.add_argument("zips", nargs="+", metavar="zip")
    parser.add_argument("--platform", metavar="NAME",
                        help="also write <id>-<version>+NAME.zip with the suffixed version inside")
    parser.add_argument("--out-dir", metavar="DIR",
                        help="where the --platform copy goes (default: next to the source zip)")
    args = parser.parse_args()
    if args.out_dir and not args.platform:
        parser.error("--out-dir only applies together with --platform")

    for path in args.zips:
        normalize(path)
        if args.platform:
            write_platform_copy(path, args.platform,
                                args.out_dir or os.path.dirname(os.path.abspath(path)))
    return 0


if __name__ == "__main__":
    sys.exit(main())
