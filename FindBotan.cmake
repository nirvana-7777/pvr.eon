if(PKG_CONFIG_FOUND)
  pkg_check_modules(PC_BOTAN botan-2>=2.19)
endif()

find_path(BOTAN_INCLUDE_DIRS NAMES botan/botan.h
                                PATHS ${CMAKE_PREFIX_PATH}/include/botan-2)

find_library(BOTAN_LIBRARIES NAMES libbotan-2.a
               PATHS ${CMAKE_PREFIX_PATH}/lib)

#if(BOTAN_FOUND)
#  set(BOTAN_INCLUDE_DIRS ${BOTAN_INCLUDE_DIR})
#  set(BOTAN_LIBRARIES ${BOTAN_LIBRARY})
#endif()

mark_as_advanced(BOTAN_INCLUDE_DIRS BOTAN_LIBRARIES)

add_library(Botan::Botan IMPORTED UNKNOWN)
set_target_properties(
  Botan::Botan
  PROPERTIES
  IMPORTED_LOCATION "${BOTAN_LIBRARIES}"
  INTERFACE_INCLUDE_DIRECTORIES "${BOTAN_INCLUDE_DIRS}"
)
