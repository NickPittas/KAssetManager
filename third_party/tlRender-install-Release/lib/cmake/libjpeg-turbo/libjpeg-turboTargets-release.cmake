#----------------------------------------------------------------
# Generated CMake target import file for configuration "Release".
#----------------------------------------------------------------

# Commands may need to know the format version.
set(CMAKE_IMPORT_FILE_VERSION 1)

# Import target "libjpeg-turbo::turbojpeg-static" for configuration "Release"
set_property(TARGET libjpeg-turbo::turbojpeg-static APPEND PROPERTY IMPORTED_CONFIGURATIONS RELEASE)
set_target_properties(libjpeg-turbo::turbojpeg-static PROPERTIES
  IMPORTED_LINK_INTERFACE_LANGUAGES_RELEASE "ASM_NASM;C"
  IMPORTED_LOCATION_RELEASE "/home/npittas/KAssetManager/.worktrees/linux-port-fedora43-wt/third_party/tlRender-install-Release/lib/libturbojpeg.a"
  )

list(APPEND _cmake_import_check_targets libjpeg-turbo::turbojpeg-static )
list(APPEND _cmake_import_check_files_for_libjpeg-turbo::turbojpeg-static "/home/npittas/KAssetManager/.worktrees/linux-port-fedora43-wt/third_party/tlRender-install-Release/lib/libturbojpeg.a" )

# Import target "libjpeg-turbo::jpeg-static" for configuration "Release"
set_property(TARGET libjpeg-turbo::jpeg-static APPEND PROPERTY IMPORTED_CONFIGURATIONS RELEASE)
set_target_properties(libjpeg-turbo::jpeg-static PROPERTIES
  IMPORTED_LINK_INTERFACE_LANGUAGES_RELEASE "ASM_NASM;C"
  IMPORTED_LOCATION_RELEASE "/home/npittas/KAssetManager/.worktrees/linux-port-fedora43-wt/third_party/tlRender-install-Release/lib/libjpeg.a"
  )

list(APPEND _cmake_import_check_targets libjpeg-turbo::jpeg-static )
list(APPEND _cmake_import_check_files_for_libjpeg-turbo::jpeg-static "/home/npittas/KAssetManager/.worktrees/linux-port-fedora43-wt/third_party/tlRender-install-Release/lib/libjpeg.a" )

# Commands beyond this point should not need to know the version.
set(CMAKE_IMPORT_FILE_VERSION)
