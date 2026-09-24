#----------------------------------------------------------------
# Generated CMake target import file for configuration "Release".
#----------------------------------------------------------------

# Commands may need to know the format version.
set(CMAKE_IMPORT_FILE_VERSION 1)

# Import target "tlRender::tlCore" for configuration "Release"
set_property(TARGET tlRender::tlCore APPEND PROPERTY IMPORTED_CONFIGURATIONS RELEASE)
set_target_properties(tlRender::tlCore PROPERTIES
  IMPORTED_LINK_INTERFACE_LANGUAGES_RELEASE "CXX"
  IMPORTED_LOCATION_RELEASE "${_IMPORT_PREFIX}/lib/libtlCore.a"
  )

list(APPEND _cmake_import_check_targets tlRender::tlCore )
list(APPEND _cmake_import_check_files_for_tlRender::tlCore "${_IMPORT_PREFIX}/lib/libtlCore.a" )

# Commands beyond this point should not need to know the version.
set(CMAKE_IMPORT_FILE_VERSION)
