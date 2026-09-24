#----------------------------------------------------------------
# Generated CMake target import file for configuration "Release".
#----------------------------------------------------------------

# Commands may need to know the format version.
set(CMAKE_IMPORT_FILE_VERSION 1)

# Import target "ftk::ftk-resource" for configuration "Release"
set_property(TARGET ftk::ftk-resource APPEND PROPERTY IMPORTED_CONFIGURATIONS RELEASE)
set_target_properties(ftk::ftk-resource PROPERTIES
  IMPORTED_LOCATION_RELEASE "${_IMPORT_PREFIX}/bin/ftk-resource"
  )

list(APPEND _cmake_import_check_targets ftk::ftk-resource )
list(APPEND _cmake_import_check_files_for_ftk::ftk-resource "${_IMPORT_PREFIX}/bin/ftk-resource" )

# Commands beyond this point should not need to know the version.
set(CMAKE_IMPORT_FILE_VERSION)
