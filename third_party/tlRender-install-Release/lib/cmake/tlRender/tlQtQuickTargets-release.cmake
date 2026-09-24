#----------------------------------------------------------------
# Generated CMake target import file for configuration "Release".
#----------------------------------------------------------------

# Commands may need to know the format version.
set(CMAKE_IMPORT_FILE_VERSION 1)

# Import target "tlRender::tlQtQuick" for configuration "Release"
set_property(TARGET tlRender::tlQtQuick APPEND PROPERTY IMPORTED_CONFIGURATIONS RELEASE)
set_target_properties(tlRender::tlQtQuick PROPERTIES
  IMPORTED_LINK_INTERFACE_LANGUAGES_RELEASE "CXX"
  IMPORTED_LOCATION_RELEASE "${_IMPORT_PREFIX}/lib/libtlQtQuick.a"
  )

list(APPEND _cmake_import_check_targets tlRender::tlQtQuick )
list(APPEND _cmake_import_check_files_for_tlRender::tlQtQuick "${_IMPORT_PREFIX}/lib/libtlQtQuick.a" )

# Commands beyond this point should not need to know the version.
set(CMAKE_IMPORT_FILE_VERSION)
