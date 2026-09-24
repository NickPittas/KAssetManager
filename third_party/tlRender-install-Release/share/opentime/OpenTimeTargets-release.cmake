#----------------------------------------------------------------
# Generated CMake target import file for configuration "Release".
#----------------------------------------------------------------

# Commands may need to know the format version.
set(CMAKE_IMPORT_FILE_VERSION 1)

# Import target "OTIO::opentime" for configuration "Release"
set_property(TARGET OTIO::opentime APPEND PROPERTY IMPORTED_CONFIGURATIONS RELEASE)
set_target_properties(OTIO::opentime PROPERTIES
  IMPORTED_LINK_INTERFACE_LANGUAGES_RELEASE "CXX"
  IMPORTED_LOCATION_RELEASE "${_IMPORT_PREFIX}/lib/libopentime.a"
  )

list(APPEND _cmake_import_check_targets OTIO::opentime )
list(APPEND _cmake_import_check_files_for_OTIO::opentime "${_IMPORT_PREFIX}/lib/libopentime.a" )

# Commands beyond this point should not need to know the version.
set(CMAKE_IMPORT_FILE_VERSION)
