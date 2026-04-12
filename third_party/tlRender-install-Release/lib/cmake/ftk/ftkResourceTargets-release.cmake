#----------------------------------------------------------------
# Generated CMake target import file for configuration "Release".
#----------------------------------------------------------------

# Commands may need to know the format version.
set(CMAKE_IMPORT_FILE_VERSION 1)

# Import target "ftk::ftkResource" for configuration "Release"
set_property(TARGET ftk::ftkResource APPEND PROPERTY IMPORTED_CONFIGURATIONS RELEASE)
set_target_properties(ftk::ftkResource PROPERTIES
  IMPORTED_LINK_INTERFACE_LANGUAGES_RELEASE "CXX"
  IMPORTED_LOCATION_RELEASE "${_IMPORT_PREFIX}/lib/libftkResource.a"
  )

list(APPEND _cmake_import_check_targets ftk::ftkResource )
list(APPEND _cmake_import_check_files_for_ftk::ftkResource "${_IMPORT_PREFIX}/lib/libftkResource.a" )

# Commands beyond this point should not need to know the version.
set(CMAKE_IMPORT_FILE_VERSION)
