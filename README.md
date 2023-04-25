[![License: GPL-2.0-or-later](https://img.shields.io/badge/License-GPL%20v2+-blue.svg)](LICENSE.md)

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
2. `git clone https://github.com/nirvana-7777/pvr.eon.git`
3. `cd pvr.eon && mkdir build && cd build`
4. `cmake -DADDONS_TO_BUILD=pvr.eon -DADDON_SRC_PREFIX=../.. -DCMAKE_BUILD_TYPE=Debug -DCMAKE_INSTALL_PREFIX=../../xbmc/addons -DPACKAGE_ZIP=1 ../../xbmc/cmake/addons`
5. `make`

## Notes

- Tested building it for Linux and Android / x86 and aarch64 (manually switch in depends/botan)
- Only tested Telemach.ba, but other should work as well or should be easy to fix

##### Useful links

* [Kodi's PVR user support](https://forum.kodi.tv/forumdisplay.php?fid=167)
* [Kodi's PVR development support](https://forum.kodi.tv/forumdisplay.php?fid=136)

## Disclaimer

- This addon is inofficial and not linked in any form to eon.tv
- All trademarks belong to them
