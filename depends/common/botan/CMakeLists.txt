cmake_minimum_required(VERSION 3.5)
project(botan)

include(ExternalProject)
externalproject_add(botan
                    SOURCE_DIR ${CMAKE_SOURCE_DIR}
                    CONFIGURE_COMMAND python3 <SOURCE_DIR>/configure.py
                      --prefix=${OUTPUT_DIR}
                      --enable-static
                      --disable-shared
#                      --os=android
#                      --cc-bin=/home/basti/source/android-tools/android-ndk-r21e/toolchains/llvm/prebuilt/linux-x86_64/bin/aarch64-linux-android30-clang++
#                      --ar-command=/home/basti/source/android-tools/android-ndk-r21e/toolchains/llvm/prebuilt/linux-x86_64/bin/aarch64-linux-android-ar
#                      --cpu=aarch64
#                       --minimized-build
#                       --enable-modules=aes,cbc,base64,auto_rng,rng,hex,sha2_32,sha2_64,sha3
                       --extra-cxxflags=-fPIC
                    INSTALL_COMMAND ""
                    BUILD_IN_SOURCE 1)

install(CODE "execute_process(COMMAND make install WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR})")
