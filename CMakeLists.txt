cmake_minimum_required(VERSION 3.5)
project(pvr.eon)

set(CMAKE_MODULE_PATH ${CMAKE_MODULE_PATH} ${PROJECT_SOURCE_DIR})

find_package(Kodi REQUIRED)
find_package(RapidJSON 1.0.2 REQUIRED)

include_directories(${KODI_INCLUDE_DIR}/.. # Hack way with "/..", need bigger Kodi cmake rework to match right include ways
                    ${RAPIDJSON_INCLUDE_DIRS}
                    )

set(EON_SOURCES
  src/http/Curl.cpp
  src/http/HttpClient.cpp
  src/SHA256.cpp
  src/Base64.cpp
  src/aes.c
  src/pkcs7_padding.c
  src/Utils.cpp
  src/Settings.cpp
  src/PVREon.cpp
)

set(EON_HEADERS
  src/http/Curl.h
  src/http/HttpClient.h
  src/SHA256.h
  src/Base64.h
  src/aes.hpp
  src/aes.h
  src/pkcs7_padding.hpp
  src/pkcs7_padding.h
  src/Utils.h
  src/Settings.h
  src/PVREon.h
  src/Globals.h
)

addon_version(pvr.eon EON)

build_addon(pvr.eon EON DEPLIBS)

include(CPack)
