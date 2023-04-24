find_package(PkgConfig)
if(PKG_CONFIG_FOUND)
  pkg_check_modules(PC_BOTAN Botan)
endif()

find_path(BOTAN_INCLUDE_DIRS NAMES botan/botan.h PATHS '/home/basti/source/pvr.eon/build/build/depends/include/botan-2/')

#find_library(BOTAN_LIBRARIES NAMES botan botan-2
#               DOC "The Botan library")

#if(BOTAN_FOUND)
#  set(BOTAN_INCLUDE_DIRS ${BOTAN_INCLUDE_DIR})
#  set(BOTAN_LIBRARIES ${BOTAN_LIBRARY})
#endif()

set(BOTAN_INCLUDE_DIRS "/home/basti/source/pvr.eon/build/build/depends/include/botan-2/")
set(BOTAN_LIBRARIES "/home/basti/source/pvr.eon/build/build/depends/lib/libbotan-2.a")

mark_as_advanced(BOTAN_INCLUDE_DIRS BOTAN_LIBRARIES)

add_library(Botan::Botan IMPORTED UNKNOWN)
set_target_properties(
  Botan::Botan
  PROPERTIES
  IMPORTED_LOCATION "${BOTAN_LIBRARIES}"
  INTERFACE_INCLUDE_DIRECTORIES "${BOTAN_INCLUDE_DIRS}"
)
