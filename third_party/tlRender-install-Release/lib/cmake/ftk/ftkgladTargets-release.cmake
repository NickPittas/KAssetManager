#----------------------------------------------------------------
# Generated CMake target import file for configuration "Release".
#----------------------------------------------------------------

# Commands may need to know the format version.
set(CMAKE_IMPORT_FILE_VERSION 1)

# Import target "ftk::ftkglad" for configuration "Release"
set_property(TARGET ftk::ftkglad APPEND PROPERTY IMPORTED_CONFIGURATIONS RELEASE)
set_target_properties(ftk::ftkglad PROPERTIES
  IMPORTED_LINK_INTERFACE_LANGUAGES_RELEASE "C"
  IMPORTED_LOCATION_RELEASE "${_IMPORT_PREFIX}/lib/libftkglad.a"
  )

list(APPEND _cmake_import_check_targets ftk::ftkglad )
list(APPEND _cmake_import_check_files_for_ftk::ftkglad "${_IMPORT_PREFIX}/lib/libftkglad.a" )

# Commands beyond this point should not need to know the version.
set(CMAKE_IMPORT_FILE_VERSION)
