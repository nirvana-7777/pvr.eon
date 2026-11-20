[![License: GPL-2.0-or-later](https://img.shields.io/badge/License-GPL%20v2+-blue.svg)](LICENSE.md)
[![Build and run tests](https://github.com/nirvana-7777/pvr.eon/actions/workflows/build.yml/badge.svg?branch=Omega)](https://github.com/nirvana-7777/pvr.eon/actions/workflows/build.yml)

# EON.tv PVR client for Kodi
This is the EON.tv PVR client addon for Kodi. It provides Kodi integration for the streaming provider [EON.tv](https://eon.tv). A user account / paid subscription is required to use this addon. Please create the user account outside of this addon. Please enter the username/password to the configuration of this addon. Some content is geo-blocked.

## Supported service providers

- SBB
- Telemach BH
- Telemach HR
- Telemach SI
- Telemach ME
- Vivacom
- Nova

Vivacom accounts are signed in through Vivacom ID (login.vivacom.bg), the same
way the eontv.bg web app does it -- just enter the Vivacom ID username/password
in the addon settings. All other providers use EON's own login.

## Features
- Live TV and Radio
- EPG
- Replay (including restart of current event)
- Channel lists made in the EON apps, shown as channel groups (per viewer profile)

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
  - Linux `armv7` against Kodi 20 "Nexus" (32-bit boxes still on Kodi 20,
    e.g. RPi4 on 32-bit LibreELEC 11): `build-linux-armv7-nexus.sh`
  - Linux `aarch64`: `build-linux-aarch64.sh`
  - Android `armv7`: `build-android-armv7.sh`
  - Android `aarch64`: `build-android-aarch64.sh`

### Docker builds

1. Ensure the sibling `xbmc` checkout has `origin/Omega`.
2. Run the matching build script for your target from the addon root, for example `./tools/docker/build-linux-amd64.sh` or `./tools/docker/build-android-aarch64.sh`.
3. Use the generated zip from the corresponding `build/docker-*/zips/` directory.

To build every target in one go, run `./tools/docker/build-all.sh`. It fetches the `xbmc` checkout if it is missing, runs all the scripts above one after another (a failed target does not stop the rest) and collects the results in `build-zips/` as `pvr.eon-<version>+<platform>.zip`. It also works from WSL on a Windows checkout; the comment at the top of the script has the exact commands. Windows and macOS zips come from the manually started GitHub Actions workflows `Build windows-x86_64`, `Build osx-x86_64` and `Build osx-arm64`.

For ARM Linux targets, the Docker scripts default to `LINUX_RENDER_SYSTEM=gles`. If your target uses desktop OpenGL, override it, for example:

`LINUX_RENDER_SYSTEM=gl ./tools/docker/build-linux-aarch64.sh`

The Android Docker image installs:
- Android SDK command-line tools
- Android platform `android-36`
- Android build-tools `36.0.0`
- Android NDK `21.4.7075529` with NDK API 21 (the build scripts' default;
  select another with `ANDROID_NDK_VERSION=` / `ANDROID_NDK_API=`)

The build uses Kodi's `tools/depends` step only to generate the Android binary-addon toolchain, then cross-builds `pvr.eon` through `xbmc/cmake/addons`.

The defaults match the builds published so far. To change one, pass it explicitly:

- a different NDK: `ANDROID_NDK_VERSION=28.2.13676358 ./tools/docker/build-android-aarch64.sh`
- a specific Kodi tag instead of the Omega branch tip: `XBMC_REF=21.2-Omega ./tools/docker/build-android-aarch64.sh`

## Notes

- Tested building it for Linux `x86_64`, Linux `armv7`, Linux `aarch64`, Android `armv7`, Android `aarch64`, macOS `arm64`, macOS `x86_64`, and Windows `x86_64`
- Depends on the inputstream addons (`inputstream.adaptive`, `inputstream.ffmpegdirect`)
- Playback has two standard modes, chosen by the "Select Inputstream" setting, and an experimental one that replaces whichever of them is selected:
  - `inputstream.adaptive` (the default): live TV can be paused and finished programmes play from the guide, but there is no seeking anywhere
  - `inputstream.ffmpegdirect`: seeking and pausing work, in programmes played from the guide as well as in the programme currently on air
  - With `inputstream.ffmpegdirect` the `Live TV controls for programmes from the guide` setting (off by default) decides how a programme from the guide is opened. Off: as a guide item, so Kodi shows its video controls (subtitles, audio, ...) and you stop playback to pick another channel; selecting the programme currently on air in the guide takes you back to live at any time. On: as a channel, so Kodi keeps the live TV controls with the channel and guide buttons; in return, while it plays, selecting that channel's programme currently on air in the guide does nothing, so stop playback and tune the channel again to get back to live. `inputstream.adaptive` always opens it as a guide item
  - `Experimental native archive streaming` (Web platform only): the add-on streams by itself in place of the selected inputstream. The "Select Inputstream" choice and the ffmpegdirect quality setting are still read when the stream URL is built, so they are best left as they were. A programme started from the guide, including `EPG -> Play programme` on one already on air, can be seeked and paused and keeps the live TV controls; to get back to plain live from it, stop playback and tune the channel again. A channel tuned directly (channel list, `Switch`) plays as plain live, with no seeking and no pausing
- Channel lists made in the EON apps show up as channel groups, next to the provider's own categories and in the order they have in EON (Kodi's `Use channel group order from backend(s)` setting, on by default, has to stay on for that):
  - The lists belong to a viewer profile. Enter that profile's name under `Profile name` to load its lists. Type the name as it is written in EON; upper and lower case are treated alike only for the basic Latin letters (a-z). With the field empty, or a name the account does not have, the account's default profile is used
  - After changing the profile, the previous profile's lists stay behind in Kodi as empty groups, because Kodi never removes a group an add-on stops supplying. Clear them once under `Settings -> PVR & Live TV -> General -> Clear data` (the channel groups are enough)
  - To see a list's channels in the order they have in EON, choose `Sort by: Backend order` in Kodi's channel or guide window; Kodi's default `Number` sort arranges every group like `All channels`
  - Channel up/down and the channel numbers follow the order of `All channels` in every group. That is how Kodi works and the add-on cannot change it
- Troubleshooting:
  - `Diagnostics: log stream manifest` (settings level `Expert`) writes the server's answer to each stream request into the Kodi log. It slows every channel start down, so switch it on only while chasing a playback problem; it applies to the two standard modes
  - `tools/debug/check-eon.sh` shows what the backend serves for an account, to compare with what the add-on shows, and `tools/debug/analyze-eon-har.sh` summarises a browser capture (HAR) of the web player without printing credentials

##### Useful links

* [Kodinerds Support Thread](https://www.kodinerds.net/thread/77069-release-pvr-eon-tv/)
* [Kodi's PVR user support](https://forum.kodi.tv/forumdisplay.php?fid=167)
* [Kodi's PVR development support](https://forum.kodi.tv/forumdisplay.php?fid=136)

## Disclaimer

- This addon is unofficial and not linked in any form to eon.tv
- All trademarks belong to them
