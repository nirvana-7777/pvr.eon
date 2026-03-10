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
- A reproducible Docker build for Linux x86_64 is available at `tools/docker/build-linux-amd64.sh`.
- A reproducible Docker build for Android `aarch64` is available at `tools/docker/build-android-aarch64.sh`.

### Android aarch64 via Docker

1. Ensure the sibling `xbmc` checkout has `origin/Omega`.
2. Run `./tools/docker/build-android-aarch64.sh`.
3. Use the generated zip from `build/docker-android-aarch64/zips/pvr.eon+android-aarch64/`.

The Android Docker image installs:
- Android SDK command-line tools
- Android platform `android-36`
- Android build-tools `36.0.0`
- Android NDK `28.2.13676358`

The build uses Kodi's `tools/depends` step only to generate the Android binary-addon toolchain, then cross-builds `pvr.eon` through `xbmc/cmake/addons`.

For older Android Kodi builds, you can match the Kodi tag and Android NDK used by that Kodi release. Example for Kodi 21.2:

`XBMC_REF=21.2-Omega ANDROID_NDK_VERSION=21.4.7075529 ANDROID_NDK_API=21 ./tools/docker/build-android-aarch64.sh`

## Notes

- Tested building it for Linux and Android / x86 and aarch64
- Only tested Telemach.ba, but other should work as well or should be easy to fix
- Depends on inputstream addon
- Fast forward and rewind won't work in Replay TV because that is handled via specific servers which inputstream does not support

##### Useful links

* [Kodinerds Support Thread](https://www.kodinerds.net/thread/77069-release-pvr-eon-tv/)
* [Kodi's PVR user support](https://forum.kodi.tv/forumdisplay.php?fid=167)
* [Kodi's PVR development support](https://forum.kodi.tv/forumdisplay.php?fid=136)

## Disclaimer

- This addon is inofficial and not linked in any form to eon.tv
- All trademarks belong to them
