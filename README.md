[![License: GPL-2.0-or-later](https://img.shields.io/badge/License-GPL%20v2+-blue.svg)](LICENSE.md)
[![Build and run tests](https://github.com/nirvana-7777/pvr.magenta/actions/workflows/build.yml/badge.svg?branch=Omega)](https://github.com/nirvana-7777/pvr.magenta/actions/workflows/build.yml)

# Fork
This is a fork of  https://github.com/nirvana-7777/pvr.magenta, since it seems discontinued
I try to continue the excellent work of nirvana-7777 and implementing missing features, like deleting and creation of recordings

## Roadmap
* Compatibility for Piers
* Fix problems with getting a token at first start (best method actually is enable V1 API, authenticate with the token, restart kodi, switch to v2 API, restart kodi, authenticate)
* Only show 2 channel groups for magenta2 (

# Magenta PVR client for Kodi
This is a Magenta PVR client addon for Kodi. It provides Kodi integration for the streaming provider [Magenta TV](https://www.telekom.de/magenta-tv). A user account / paid subscription is required to use this addon. The content is geo-blocked and DRM protected. Therefore it requires inputstream adaptive in combination with widevine.
Versions from 21.9.0 also support Magenta TV 2.0. For Magenta TV 2.0 you have to provide your username and password. After entering your credentials you have to restart Kodi.

## Features 1.0
- Live TV
- EPG
- Recording Playback
- Delete Recordings
- Single and series timers (add/update/delete)
- Playtimeshift
- Automatic replacement of devices when more than 5 OTT devices reached
- Mapping to Kodi genres by Musiktoto. Thank you!
- OTP Login / Line Auth

## Features 2.0
- Live TV
- Timeshift
- EPG
- Channel Groups
- Genre mapping (edit mygenres2.json)
- Playback recordings
- Username / Password Login

## Build instructions

### Linux

1. `git clone --branch master https://github.com/xbmc/xbmc.git kodi`
   compile kodi -- if you want to build for android follow the official cross compile guide here https://github.com/xbmc/xbmc/blob/master/docs/README.Android.md
   ATTENTION: If you want to crosscompile for Omega be sure to choose an old version of the NDK - 21.4.7075529 worked fine for Omega
3. `mkdir -p kodi/cmake/addons/addons/pvr.magenta/`
4. `echo "pvr.magenta https://github.com/gemx/pvr.magenta Omega" > kodi/cmake/addons/addons/pvr.magenta/pvr.magenta.txt`
5. `echo "all" > kodi/cmake/addons/addons/pvr.magenta/platforms.txt`
6. `git clone https://github.com/gemx/pvr.magenta.git`
7. `cd pvr.magenta && mkdir build && cd build`
8. `cmake -DADDONS_TO_BUILD=pvr.magenta -DADDON_SRC_PREFIX=../.. -DCMAKE_BUILD_TYPE=Debug -DCMAKE_INSTALL_PREFIX=../../kodi/addons -DPACKAGE_ZIP=1 ../../kodi/cmake/addons`
   for cross-compile from linux to android use
8. `cmake -DADDONS_TO_BUILD="pvr.magenta" -DADDON_SRC_PREFIX=../.. -DCMAKE_BUILD_TYPE=Debug -DCMAKE_INSTALL_PREFIX=../../kodi/addons -DPACKAGE_ZIP=1 -DCMAKE_TOOLCHAIN_FILE=/home/pi/android-tools/xbmc-depends/arm-linux-androideabi-21-debug/share/Toolchain.cmake ../../kodi/cmake/addons`
10. `make`

## Notes

- Tested building it for Linux/x86 and Android/aarch64
- Depends on inputstream addon
- Depends on widevine

##### Useful links

* [Kodinerds Support Thread](https://www.kodinerds.net/thread/77429-release-pvr-magenta/)
* [Kodi's PVR user support](https://forum.kodi.tv/forumdisplay.php?fid=167)
* [Kodi's PVR development support](https://forum.kodi.tv/forumdisplay.php?fid=136)

## Disclaimer

- This addon is inofficial and not linked in any form to Magenta tv
- All trademarks belong to Magenta TV
- Use at your own risk and without support. If you have problems please use the original apps.
- The addon is done for fun and without any business interest.
