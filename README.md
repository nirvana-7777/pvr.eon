[![License: GPL-2.0-or-later](https://img.shields.io/badge/License-GPL%20v2+-blue.svg)](LICENSE.md)

# EON.tv PVR client for Kodi
This is the EON.tv PVR client addon for Kodi. It provides Kodi integration for the streaming provider EON.tv. A user account is required to use this addon. Please create the user account outside of this addon. Some content is geo-blocked. 

## Build instructions

### Linux

1. `git clone --branch master https://github.com/xbmc/xbmc.git`
2. `git clone https://github.com/nirvana-7777/pvr.eon.git`
3. `cd pvr.hrti && mkdir build && cd build`
4. `cmake -DADDONS_TO_BUILD=pvr.eon -DADDON_SRC_PREFIX=../.. -DCMAKE_BUILD_TYPE=Debug -DCMAKE_INSTALL_PREFIX=../../xbmc/addons -DPACKAGE_ZIP=1 ../../xbmc/cmake/addons`
5. `make`

##### Useful links

* [Kodi's PVR user support](https://forum.kodi.tv/forumdisplay.php?fid=167)
* [Kodi's PVR development support](https://forum.kodi.tv/forumdisplay.php?fid=136)
