[![License: GPL-2.0-or-later](https://img.shields.io/badge/License-GPL%20v2+-blue.svg)](LICENSE.md)
[![Build and run tests](https://github.com/nirvana-7777/pvr.eon/actions/workflows/build.yml/badge.svg?branch=Omega)](https://github.com/nirvana-7777/pvr.eon/actions/workflows/build.yml)

# EON.tv PVR client for Kodi
This is the EON.tv PVR client addon for Kodi. It provides Kodi integration for the streaming provider [EON.tv](https://eon.tv). A user account / paid subscription is required to use this addon. Please create the user account outside of this addon. Please enter the username/password to the configuration of this addon. Some content is geo-blocked.

## Supported service providers

- SBB
- Telemach
- NetTV Plus
- Vivacom
- Eon Hrvatska
- Nova

## Features
- Live TV and Radio
- EPG
- Replay (including restart of current event)

## Build instructions

### Linux

1. `git clone --branch master https://github.com/xbmc/xbmc.git`
2. `git clone --branch Omega https://github.com/nirvana-7777/pvr.eon.git`
3. `cd pvr.eon && mkdir build && cd build`
4. `cmake -DADDONS_TO_BUILD=pvr.eon -DADDON_SRC_PREFIX=../.. -DCMAKE_BUILD_TYPE=Debug -DCMAKE_INSTALL_PREFIX=../../xbmc/addons -DPACKAGE_ZIP=1 ../../xbmc/cmake/addons`
5. `make`

### Local checkout note

- `ADDON_SRC_PREFIX` only redirects the source path. Kodi still requires an addon definition for `pvr.eon` via `ADDONS_DEFINITION_DIR` or `xbmc/cmake/addons/addons/pvr.eon/pvr.eon.txt`.
- The current `xbmc` `master` branch tracks Kodi `Piers` (v22). The `Omega` branch of this addon should be built against an `xbmc` Omega checkout.
- Reproducible Docker builds are available under `tools/docker/` for:
  - Linux `x86_64`: `build-linux-amd64.sh`
  - Linux `armv7`: `build-linux-armv7.sh`
  - Linux `aarch64`: `build-linux-aarch64.sh`
  - Android `armv7`: `build-android-armv7.sh`
  - Android `aarch64`: `build-android-aarch64.sh`

### Docker builds

1. Ensure the sibling `xbmc` checkout has `origin/Omega`.
2. Run the matching build script for your target from the addon root, for example `./tools/docker/build-linux-amd64.sh` or `./tools/docker/build-android-aarch64.sh`.
3. Use the generated zip from the corresponding `build/docker-*/zips/` directory.

For ARM Linux targets, the Docker scripts default to `LINUX_RENDER_SYSTEM=gles`. If your target uses desktop OpenGL, override it, for example:

`LINUX_RENDER_SYSTEM=gl ./tools/docker/build-linux-aarch64.sh`

The Android Docker image installs:
- Android SDK command-line tools
- Android platform `android-36`
- Android build-tools `36.0.0`
- Android NDK `28.2.13676358`

The build uses Kodi's `tools/depends` step only to generate the Android binary-addon toolchain, then cross-builds `pvr.eon` through `xbmc/cmake/addons`.

For older Android Kodi builds, you can match the Kodi tag and Android NDK used by that Kodi release. Example for Kodi 21.2:

`XBMC_REF=21.2-Omega ANDROID_NDK_VERSION=21.4.7075529 ANDROID_NDK_API=21 ./tools/docker/build-android-aarch64.sh`

## Notes

- Tested building it for Linux `x86_64`, Linux `armv7`, Linux `aarch64`, Android `armv7`, and Android `aarch64`
- Only tested Telemach.ba, but other should work as well or should be easy to fix
- Depends on inputstream addon
- **For seeking/rewind (live TV and replay/catchup) to work**, set the addon's
  "Select Inputstream" setting to `inputstream.ffmpegdirect` -- it defaults to
  `inputstream.adaptive`, which doesn't support seeking with this addon
- Standard inputstream-based Replay TV still behaves like a short rolling live HLS window on EON/Vivacom, so full seek/rewind is limited there
- `Experimental native archive streaming` adds working archive seek support for finished replay programmes and for `EPG -> Play programme` on already-started events
- Direct live channel `Switch` still uses the standard live playback path and does not yet expose the native archive/timeshift behavior

##### Useful links

* [Kodinerds Support Thread](https://www.kodinerds.net/thread/77069-release-pvr-eon-tv/)
* [Kodi's PVR user support](https://forum.kodi.tv/forumdisplay.php?fid=167)
* [Kodi's PVR development support](https://forum.kodi.tv/forumdisplay.php?fid=136)

## Disclaimer

- This addon is inofficial and not linked in any form to eon.tv
- All trademarks belong to them
