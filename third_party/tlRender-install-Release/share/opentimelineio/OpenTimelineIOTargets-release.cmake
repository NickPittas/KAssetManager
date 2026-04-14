#----------------------------------------------------------------
# Generated CMake target import file for configuration "Release".
#----------------------------------------------------------------

# Commands may need to know the format version.
set(CMAKE_IMPORT_FILE_VERSION 1)

# Import target "OTIO::opentimelineio" for configuration "Release"
set_property(TARGET OTIO::opentimelineio APPEND PROPERTY IMPORTED_CONFIGURATIONS RELEASE)
set_target_properties(OTIO::opentimelineio PROPERTIES
  IMPORTED_LINK_INTERFACE_LANGUAGES_RELEASE "CXX"
  IMPORTED_LOCATION_RELEASE "/home/npittas/KAssetManager/third_party/tlRender-install-Release/lib/libopentimelineio.a"
  )

list(APPEND _cmake_import_check_targets OTIO::opentimelineio )
list(APPEND _cmake_import_check_files_for_OTIO::opentimelineio "/home/npittas/KAssetManager/third_party/tlRender-install-Release/lib/libopentimelineio.a" )

# Commands beyond this point should not need to know the version.
set(CMAKE_IMPORT_FILE_VERSION)
