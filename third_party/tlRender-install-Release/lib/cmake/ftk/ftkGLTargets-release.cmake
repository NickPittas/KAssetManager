#----------------------------------------------------------------
# Generated CMake target import file for configuration "Release".
#----------------------------------------------------------------

# Commands may need to know the format version.
set(CMAKE_IMPORT_FILE_VERSION 1)

# Import target "ftk::ftkGL" for configuration "Release"
set_property(TARGET ftk::ftkGL APPEND PROPERTY IMPORTED_CONFIGURATIONS RELEASE)
set_target_properties(ftk::ftkGL PROPERTIES
  IMPORTED_LINK_INTERFACE_LANGUAGES_RELEASE "CXX"
  IMPORTED_LOCATION_RELEASE "${_IMPORT_PREFIX}/lib/libftkGL.a"
  )

list(APPEND _cmake_import_check_targets ftk::ftkGL )
list(APPEND _cmake_import_check_files_for_ftk::ftkGL "${_IMPORT_PREFIX}/lib/libftkGL.a" )

# Commands beyond this point should not need to know the version.
set(CMAKE_IMPORT_FILE_VERSION)
