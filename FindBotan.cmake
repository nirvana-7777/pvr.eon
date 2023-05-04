if(PKG_CONFIG_FOUND)
  pkg_check_modules(PC_Botan botan-2)
endif()

set(CMAKE_FIND_ROOT_PATH_MODE_LIBRARY BOTH)
set(CMAKE_FIND_ROOT_PATH_MODE_INCLUDE BOTH)

find_path(Botan_INCLUDE_DIR NAMES botan/botan.h
          PATHS
          ${CMAKE_PREFIX_PATH}
          ${ADDON_DEPENDS_PATH}
          /usr
          /usr/local
          PATH_SUFFIXES include/botan-2
          )

find_library(Botan_LIBRARY NAMES libbotan-2.a
               PATHS
               ${CMAKE_PREFIX_PATH}
               ${ADDON_DEPENDS_PATH}
               PATH_SUFFIXES lib)

include(FindPackageHandleStandardArgs)
find_package_handle_standard_args(Botan
                DEFAULT_MSG Botan_LIBRARY Botan_INCLUDE_DIR)

if(BOTAN_FOUND)
  set(BOTAN_INCLUDE_DIRS ${Botan_INCLUDE_DIR})
  set(BOTAN_LIBRARIES ${Botan_LIBRARY})
else ()
  set(BOTAN_INCLUDE_DIRS ${ADDON_DEPENDS_PATH}/include/botan-2)
  set(BOTAN_LIBRARIES ${ADDON_DEPENDS_PATH}/lib/libbotan-2.a)
endif()

mark_as_advanced(BOTAN_INCLUDE_DIRS BOTAN_LIBRARIES)

add_library(Botan::Botan IMPORTED UNKNOWN)
set_target_properties(
  Botan::Botan
  PROPERTIES
  IMPORTED_LOCATION "${BOTAN_LIBRARIES}"
  INTERFACE_INCLUDE_DIRECTORIES "${BOTAN_INCLUDE_DIRS}"
)
