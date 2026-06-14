#----------------------------------------------------------------
# Generated CMake target import file for configuration "Release".
#----------------------------------------------------------------

# Commands may need to know the format version.
set(CMAKE_IMPORT_FILE_VERSION 1)

# Import target "tlRender::tlResource" for configuration "Release"
set_property(TARGET tlRender::tlResource APPEND PROPERTY IMPORTED_CONFIGURATIONS RELEASE)
set_target_properties(tlRender::tlResource PROPERTIES
  IMPORTED_LINK_INTERFACE_LANGUAGES_RELEASE "CXX"
  IMPORTED_LOCATION_RELEASE "${_IMPORT_PREFIX}/lib/libtlResource.a"
  )

list(APPEND _cmake_import_check_targets tlRender::tlResource )
list(APPEND _cmake_import_check_files_for_tlRender::tlResource "${_IMPORT_PREFIX}/lib/libtlResource.a" )

# Commands beyond this point should not need to know the version.
set(CMAKE_IMPORT_FILE_VERSION)
