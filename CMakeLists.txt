cmake_minimum_required(VERSION 3.5)
project(pvr.hrti)

set(CMAKE_MODULE_PATH ${CMAKE_MODULE_PATH} ${PROJECT_SOURCE_DIR})

find_package(Kodi REQUIRED)
find_package(RapidJSON 1.0.2 REQUIRED)

include_directories(${KODI_INCLUDE_DIR}/.. # Hack way with "/..", need bigger Kodi cmake rework to match right include ways
                    ${RAPIDJSON_INCLUDE_DIRS}
                    )

set(PVRHRTI_SOURCES
  src/http/Curl.cpp
  src/http/HttpClient.cpp
  src/Utils.cpp
  src/md5.cpp
  src/Base64.cpp
  src/Settings.cpp
  src/PVRHrti.cpp
)

set(PVRHRTI_HEADERS
  src/http/Curl.h
  src/http/HttpClient.h
  src/Utils.h
  src/md5.h
  src/Base64.h
  src/Settings.h
  src/PVRHrti.h
)

addon_version(pvr.hrti HRTI)

build_addon(pvr.hrti PVRHRTI DEPLIBS)

include(CPack)
