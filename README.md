[![License: GPL-2.0-or-later](https://img.shields.io/badge/License-GPL%20v2+-blue.svg)](LICENSE.md)
[![Build and run tests](https://github.com/nirvana-7777/pvr.eon/actions/workflows/build.yml/badge.svg?branch=Nexus)](https://github.com/nirvana-7777/pvr.eon/actions/workflows/build.yml)

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
2. `mkdir -p xbmc/cmake/addons/addons/pvr.eon/`
3. `echo "pvr.eon https://github.com/nirvana-7777/pvr.eon Nexus" > xbmc/cmake/addons/addons/pvr.eon/pvr.eon.txt`
4. `echo "all" > xbmc/cmake/addons/addons/pvr.eon/platforms.txt`
5. `git clone https://github.com/nirvana-7777/pvr.eon.git`
6. `cd pvr.eon && mkdir build && cd build`
7. `cmake -DADDONS_TO_BUILD=pvr.eon -DADDON_SRC_PREFIX=../.. -DCMAKE_BUILD_TYPE=Debug -DCMAKE_INSTALL_PREFIX=../../xbmc/addons -DPACKAGE_ZIP=1 ../../xbmc/cmake/addons`
8. `make`

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
