#----------------------------------------------------------------
# Generated CMake target import file for configuration "Release".
#----------------------------------------------------------------

# Commands may need to know the format version.
set(CMAKE_IMPORT_FILE_VERSION 1)

# Import target "SVT-AV1::SVT-AV1-static" for configuration "Release"
set_property(TARGET SVT-AV1::SVT-AV1-static APPEND PROPERTY IMPORTED_CONFIGURATIONS RELEASE)
set_target_properties(SVT-AV1::SVT-AV1-static PROPERTIES
  IMPORTED_LINK_INTERFACE_LANGUAGES_RELEASE "ASM_NASM;C"
  IMPORTED_LOCATION_RELEASE "${_IMPORT_PREFIX}/lib/libSvtAv1Enc.a"
  )

list(APPEND _cmake_import_check_targets SVT-AV1::SVT-AV1-static )
list(APPEND _cmake_import_check_files_for_SVT-AV1::SVT-AV1-static "${_IMPORT_PREFIX}/lib/libSvtAv1Enc.a" )

# Commands beyond this point should not need to know the version.
set(CMAKE_IMPORT_FILE_VERSION)
