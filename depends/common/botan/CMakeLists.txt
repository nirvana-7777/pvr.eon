cmake_minimum_required(VERSION 3.5)
project(botan)

string(TOLOWER ${CMAKE_SYSTEM_NAME} CMAKE_SYSTEM_NAME)

include(ExternalProject)
externalproject_add(botan
                    SOURCE_DIR ${CMAKE_SOURCE_DIR}
                    CONFIGURE_COMMAND python3 <SOURCE_DIR>/configure.py
                      --prefix=${OUTPUT_DIR}
                      --enable-static
                      --disable-shared
                      --os=${CORE_SYSTEM_NAME}
#                      --cc-bin=${CMAKE_CXX_COMPILER}
                      --ar-command=${CMAKE_AR}
                      --cpu=${CMAKE_SYSTEM_PROCESSOR}
#                       --minimized-build
#                       --enable-modules=aes,cbc,base64,auto_rng,rng,hex,sha2_32,sha2_64,sha3
                       --extra-cxxflags=-fPIC
                    INSTALL_COMMAND ""
                    BUILD_IN_SOURCE 1)

install(CODE "execute_process(COMMAND make install WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR})")
