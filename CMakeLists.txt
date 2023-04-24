cmake_minimum_required(VERSION 3.5)
project(pvr.eon)

set(CMAKE_MODULE_PATH ${CMAKE_MODULE_PATH} ${PROJECT_SOURCE_DIR})

find_package(Kodi REQUIRED)
find_package(RapidJSON 1.0.2 REQUIRED)
find_package(Botan REQUIRED)

include_directories(${KODI_INCLUDE_DIR}/.. # Hack way with "/..", need bigger Kodi cmake rework to match right include ways
                    ${RAPIDJSON_INCLUDE_DIRS}
                    ${BOTAN_INCLUDE_DIRS}
                    )

set(DEPLIBS ${BOTAN_LIBRARIES}
            )
message(STATUS "BOTAN_LIBRARIES: ${BOTAN_LIBRARIES}")

set(EON_SOURCES
  src/http/Curl.cpp
  src/http/HttpClient.cpp
  src/Utils.cpp
  src/Settings.cpp
  src/PVREon.cpp
)

set(EON_HEADERS
  src/http/Curl.h
  src/http/HttpClient.h
  src/Utils.h
  src/Settings.h
  src/PVREon.h
  src/Globals.h
)

addon_version(pvr.eon EON)

build_addon(pvr.eon EON DEPLIBS)

include(CPack)
