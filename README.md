[![License: GPL-2.0-or-later](https://img.shields.io/badge/License-GPL%20v2+-blue.svg)](LICENSE.md)
[![Build Status](https://dev.azure.com/teamkodi/kodi-pvr/_apis/build/status/kodi-pvr.pvr.demo?branchName=Nexus)](https://dev.azure.com/teamkodi/kodi-pvr/_build/latest?definitionId=56&branchName=Nexus)
[![Build Status](https://jenkins.kodi.tv/view/Addons/job/kodi-pvr/job/pvr.demo/job/Nexus/badge/icon)](https://jenkins.kodi.tv/blue/organizations/jenkins/kodi-pvr%2Fpvr.demo/branches/)
[![Coverity Scan Build Status](https://scan.coverity.com/projects/5120/badge.svg)](https://scan.coverity.com/projects/5120)

# HRTi PVR client for Kodi
This is the Hrti PVR client addon for Kodi. It provides Kodi integration for the Croation TV streaming provider HRT. A user account is required to use this addon. Please create the user account outside of this addon. A croatian is required to access geoblocked content. 

## Build instructions

### Linux

1. `git clone --branch master https://github.com/xbmc/xbmc.git`
2. `git clone https://github.com/nirvana-7777/pvr.hrti.git`
3. `cd pvr.hrti && mkdir build && cd build`
4. `cmake -DADDONS_TO_BUILD=pvr.hrti -DADDON_SRC_PREFIX=../.. -DCMAKE_BUILD_TYPE=Debug -DCMAKE_INSTALL_PREFIX=../../xbmc/addons -DPACKAGE_ZIP=1 ../../xbmc/cmake/addons`
5. `make`

##### Useful links

* [Kodi's PVR user support](https://forum.kodi.tv/forumdisplay.php?fid=167)
* [Kodi's PVR development support](https://forum.kodi.tv/forumdisplay.php?fid=136)
