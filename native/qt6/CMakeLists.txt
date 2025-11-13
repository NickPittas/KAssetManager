cmake_minimum_required(VERSION 3.21)
cmake_minimum_required(VERSION 3.21)

# Set vcpkg toolchain file from environment if not already set
if(NOT DEFINED CMAKE_TOOLCHAIN_FILE)
    if(DEFINED ENV{VCPKG_ROOT} AND EXISTS "$ENV{VCPKG_ROOT}/scripts/buildsystems/vcpkg.cmake")
        set(CMAKE_TOOLCHAIN_FILE "$ENV{VCPKG_ROOT}/scripts/buildsystems/vcpkg.cmake" CACHE STRING "Vcpkg toolchain file")
        # Default triplet; can be overridden by user
        if(NOT DEFINED VCPKG_TARGET_TRIPLET)
            set(VCPKG_TARGET_TRIPLET "x64-windows" CACHE STRING "Vcpkg target triplet")
        endif()
        message(STATUS "Using vcpkg toolchain: ${CMAKE_TOOLCHAIN_FILE}")
        message(STATUS "Using vcpkg triplet: ${VCPKG_TARGET_TRIPLET}")
    endif()
endif()

project(KAssetManagerQt VERSION 1.1.2 LANGUAGES CXX)
# Optional static analysis / sanitizers
option(ENABLE_ASAN "Enable AddressSanitizer (Clang/GCC only)" OFF)
if(ENABLE_ASAN)
  if (CMAKE_CXX_COMPILER_ID MATCHES "Clang|GNU")
    add_compile_options(-fsanitize=address -fno-omit-frame-pointer)
    add_link_options(-fsanitize=address)
  else()
    message(WARNING "ENABLE_ASAN is ON but current compiler may not support it")
  endif()
endif()

# UndefinedBehaviorSanitizer (Clang/GCC only)
option(ENABLE_UBSAN "Enable UndefinedBehaviorSanitizer (Clang/GCC only)" OFF)
if(ENABLE_UBSAN)
  if (CMAKE_CXX_COMPILER_ID MATCHES "Clang|GNU")
    add_compile_options(-fsanitize=undefined)
    add_link_options(-fsanitize=undefined)
  else()
    message(WARNING "ENABLE_UBSAN is ON but current compiler may not support it")
  endif()
endif()


option(ENABLE_CLANG_TIDY "Enable clang-tidy static analysis" OFF)
if(ENABLE_CLANG_TIDY)
  find_program(CLANG_TIDY_EXE NAMES clang-tidy)
  if(CLANG_TIDY_EXE)
    set(CMAKE_CXX_CLANG_TIDY "${CLANG_TIDY_EXE};-warnings-as-errors=*,-llvm-header-guard,-modernize-use-trailing-return-type")
    message(STATUS "clang-tidy enabled: ${CLANG_TIDY_EXE}")
  else()
    message(WARNING "clang-tidy not found")
  endif()
endif()


set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)


# Option to build the main application (allows CI to build tests-only)
option(BUILD_APP "Build main application" ON)

## SuperBuild bootstrap option was removed — using external package only


# Allow building only tests in CI without full app dependencies
# Code coverage (GCC/Clang only)
option(ENABLE_COVERAGE "Enable code coverage flags (GCC/Clang only)" OFF)
if(ENABLE_COVERAGE)
  if (CMAKE_CXX_COMPILER_ID MATCHES "Clang|GNU")
    add_compile_options(--coverage -O0)
    add_link_options(--coverage)
  else()
    message(WARNING "ENABLE_COVERAGE is ON but current compiler may not support it")
  endif()
endif()

if(BUILD_APP)

find_package(QT NAMES Qt6 REQUIRED COMPONENTS Widgets Multimedia MultimediaWidgets Sql Concurrent Svg SvgWidgets)
find_package(Qt6 REQUIRED COMPONENTS Widgets Multimedia MultimediaWidgets Sql Concurrent Svg SvgWidgets)
# Optional: Qt PDF (viewer) if available
find_package(Qt6 QUIET COMPONENTS Pdf PdfWidgets)
# ActiveQt (QAxObject) for Office-to-PDF conversion on Windows (optional)
if (WIN32)
  # Try all known ActiveQt components; different Qt distributions export different subsets
  # 1) Preferred: via umbrella Qt6 package
  find_package(Qt6 QUIET COMPONENTS AxBase AxContainer AxServer)
  # 2) Direct, in case Qt6Config.cmake doesn't advertise these components
  if (NOT Qt6AxBase_FOUND)
    find_package(Qt6AxBase QUIET CONFIG)
  endif()
  if (NOT Qt6AxContainer_FOUND)
    find_package(Qt6AxContainer QUIET CONFIG)
  endif()
  if (NOT Qt6AxServer_FOUND)
    find_package(Qt6AxServer QUIET CONFIG)
  endif()
  # Diagnostics to help users see what was found
  if (Qt6AxBase_FOUND OR Qt6AxContainer_FOUND OR Qt6AxServer_FOUND)
    message(STATUS "ActiveQt components detected:"
      " AxBase=${Qt6AxBase_FOUND}; AxContainer=${Qt6AxContainer_FOUND}; AxServer=${Qt6AxServer_FOUND}")
  else()
    message(STATUS "ActiveQt not found for this kit")
  endif()
endif()

# Find OpenImageIO (optional - will use placeholders if not found)
find_package(OpenImageIO CONFIG QUIET)
if(OpenImageIO_FOUND)
    message(STATUS "OpenImageIO found - advanced format support enabled")
    add_compile_definitions(HAVE_OPENIMAGEIO)
else()
    message(WARNING "OpenImageIO not found - will use placeholder thumbnails for EXR/PSD/HDR formats. HINTS: ${_OIIO_HINTS}")
endif()

# OOXML ZIP reading (minizip-ng via vcpkg)
find_package(minizip-ng CONFIG REQUIRED)

# Find GStreamer (required for video/image sequence playback)
find_package(PkgConfig QUIET)
if(PkgConfig_FOUND)
    pkg_check_modules(GSTREAMER QUIET gstreamer-1.0 gstreamer-video-1.0 gstreamer-audio-1.0 gstreamer-app-1.0)
endif()
if(NOT GSTREAMER_FOUND)
    # Check for bundled GStreamer in third_party
    set(_gstreamer_candidates
        "${CMAKE_SOURCE_DIR}/../third_party/gstreamer"
        "${CMAKE_SOURCE_DIR}/../../third_party/gstreamer"
        "${CMAKE_CURRENT_LIST_DIR}/../third_party/gstreamer"
        "${CMAKE_CURRENT_LIST_DIR}/../../third_party/gstreamer"
    )
    foreach(_candidate IN LISTS _gstreamer_candidates)
        get_filename_component(_abs_candidate "${_candidate}" ABSOLUTE)
        if(EXISTS "${_abs_candidate}/include" AND EXISTS "${_abs_candidate}/lib")
            set(GSTREAMER_ROOT "${_abs_candidate}")
            break()
        endif()
    endforeach()

    if(GSTREAMER_ROOT AND EXISTS "${GSTREAMER_ROOT}")
        set(GSTREAMER_INCLUDE_DIRS
            "${GSTREAMER_ROOT}/include/gstreamer-1.0"
            "${GSTREAMER_ROOT}/include/glib-2.0"
            "${GSTREAMER_ROOT}/lib/glib-2.0/include"
        )
        set(GSTREAMER_LIBRARY_DIRS "${GSTREAMER_ROOT}/lib")
        if(EXISTS "${GSTREAMER_ROOT}/include/gstreamer-1.0")
            set(GSTREAMER_FOUND TRUE)
            message(STATUS "Using bundled GStreamer from ${GSTREAMER_ROOT}")
        endif()
    endif()
endif()
if(NOT GSTREAMER_FOUND)
    message(FATAL_ERROR "GStreamer not found. Please install GStreamer 1.0 development packages.")
endif()

# Find FFmpeg for conversion tool only (not for playback)
find_package(PkgConfig QUIET)
if(PkgConfig_FOUND)
    pkg_check_modules(FFMPEG QUIET libavutil)
endif()
if(NOT FFMPEG_FOUND)
    if(DEFINED ENV{FFMPEG_ROOT} AND NOT ENV{FFMPEG_ROOT} STREQUAL "")
        set(FFMPEG_ROOT $ENV{FFMPEG_ROOT})
    endif()
    if(NOT FFMPEG_ROOT)
        # Probe common in-repo locations (supports running CMake from native/qt6 or repo root)
        set(_ffmpeg_candidates
            "${CMAKE_SOURCE_DIR}/../third_party/ffmpeg"
            "${CMAKE_SOURCE_DIR}/../../third_party/ffmpeg"
            "${CMAKE_CURRENT_LIST_DIR}/../third_party/ffmpeg"
            "${CMAKE_CURRENT_LIST_DIR}/../../third_party/ffmpeg"
        )
        foreach(_candidate IN LISTS _ffmpeg_candidates)
            get_filename_component(_abs_candidate "${_candidate}" ABSOLUTE)
            if(EXISTS "${_abs_candidate}/include" AND EXISTS "${_abs_candidate}/lib")
                set(FFMPEG_ROOT "${_abs_candidate}")
                break()
            endif()
        endforeach()
    endif()
    if(FFMPEG_ROOT AND EXISTS "${FFMPEG_ROOT}")
        set(FFMPEG_INCLUDE_DIR "${FFMPEG_ROOT}/include")
        set(FFMPEG_LIBRARY_DIRS "${FFMPEG_ROOT}/lib")
        if(EXISTS "${FFMPEG_INCLUDE_DIR}")
            set(FFMPEG_FOUND TRUE)
            message(STATUS "Using custom FFmpeg from ${FFMPEG_ROOT}")
        endif()
    endif()
endif()
if(NOT FFMPEG_FOUND)
    # Try vcpkg FFmpeg
    find_path(FFMPEG_INCLUDE_DIR libavutil/log.h HINTS "$ENV{VCPKG_ROOT}/installed/${VCPKG_TARGET_TRIPLET}/include")
    if(FFMPEG_INCLUDE_DIR)
        set(FFMPEG_FOUND TRUE)
        message(STATUS "FFmpeg headers found at ${FFMPEG_INCLUDE_DIR}")
    endif()
endif()

# Standard Qt project setup (Qt6)
if (COMMAND qt_standard_project_setup)
  qt_standard_project_setup()
endif()

# Add Windows resource file for icon
if(WIN32)
    set(APP_RESOURCES src/app.rc)
endif()

qt_add_executable(kassetmanagerqt WIN32
    src/main.cpp
    src/mainwindow.h
    src/mainwindow.cpp
    src/virtual_drag.cpp
    src/virtual_drag.h
    src/db.cpp
    src/db.h
    src/drag_utils.h
    src/drag_utils.cpp
    src/virtual_folders.h
    src/virtual_folders.cpp
    src/assets_model.h
    src/assets_model.cpp
    src/assets_table_model.h
    src/importer.h
    src/importer.cpp
    src/live_preview_manager.h
    src/live_preview_manager.cpp
    src/log_manager.h
    src/log_manager.cpp
    src/progress_manager.h
    src/progress_manager.cpp
    src/tags_model.h
    src/preview_overlay.h
    src/preview_overlay.cpp
    src/import_progress_dialog.h
    src/import_progress_dialog.cpp
    src/settings_dialog.h
    src/settings_dialog.cpp
    src/star_rating_widget.h
    src/star_rating_widget.cpp
    src/sequence_detector.h
    src/sequence_detector.cpp
    src/oiio_image_loader.h
    src/oiio_image_loader.cpp
    src/project_folder_watcher.h
    src/project_folder_watcher.cpp
    src/log_viewer_widget.h
    src/log_viewer_widget.cpp
    src/video_metadata.h
    src/video_metadata.cpp
    src/file_ops.h
    src/file_ops.cpp
    src/file_ops_dialog.h
    src/file_ops_dialog.cpp
    src/office_preview.h
    src/office_preview.cpp
    src/context_preserver.h
    src/context_preserver.cpp
    src/database_health_agent.h
    src/database_health_agent.cpp
    src/database_health_dialog.h
    src/database_health_dialog.cpp
    src/bulk_rename_dialog.h
    src/bulk_rename_dialog.cpp
    src/everything_search.h
    src/everything_search.cpp
    src/everything_search_dialog.h
    src/utils.h
    src/utils.cpp

    src/everything_search_dialog.cpp
    src/media_convert_dialog.h
    src/media_convert_dialog.cpp
    src/media_converter_worker.h
    src/media_converter_worker.cpp
    src/media/gstreamer_player.h
    src/media/gstreamer_player.cpp
    ${APP_RESOURCES}
)

target_include_directories(kassetmanagerqt PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/src
)

# Add GStreamer include directories
if(GSTREAMER_FOUND)
    if(GSTREAMER_INCLUDE_DIRS)
        target_include_directories(kassetmanagerqt PRIVATE ${GSTREAMER_INCLUDE_DIRS})
    endif()
endif()

# Add FFmpeg include directory if found
if(FFMPEG_FOUND)
    if(FFMPEG_INCLUDE_DIR)
        target_include_directories(kassetmanagerqt PRIVATE ${FFMPEG_INCLUDE_DIR})
    elseif(FFMPEG_INCLUDE_DIRS)
        target_include_directories(kassetmanagerqt PRIVATE ${FFMPEG_INCLUDE_DIRS})
    endif()
endif()
# Link GStreamer libraries
if(GSTREAMER_FOUND)
    if(NOT DEFINED GSTREAMER_LIBRARY_DIRS)
        if(GSTREAMER_ROOT)
            set(GSTREAMER_LIBRARY_DIRS "${GSTREAMER_ROOT}/lib")
        endif()
    endif()

    find_library(GSTREAMER_LIB gstreamer-1.0 HINTS ${GSTREAMER_LIBRARY_DIRS})
    find_library(GSTREAMER_VIDEO_LIB gstvideo-1.0 HINTS ${GSTREAMER_LIBRARY_DIRS})
    find_library(GSTREAMER_AUDIO_LIB gstaudio-1.0 HINTS ${GSTREAMER_LIBRARY_DIRS})
    find_library(GSTREAMER_APP_LIB gstapp-1.0 HINTS ${GSTREAMER_LIBRARY_DIRS})
    find_library(GSTREAMER_BASE_LIB gstbase-1.0 HINTS ${GSTREAMER_LIBRARY_DIRS})
    find_library(GLIB_LIB glib-2.0 HINTS ${GSTREAMER_LIBRARY_DIRS})
    find_library(GOBJECT_LIB gobject-2.0 HINTS ${GSTREAMER_LIBRARY_DIRS})

    if(GSTREAMER_LIB AND GSTREAMER_VIDEO_LIB AND GSTREAMER_AUDIO_LIB AND GSTREAMER_APP_LIB AND GLIB_LIB AND GOBJECT_LIB)
        target_link_libraries(kassetmanagerqt PRIVATE
            ${GSTREAMER_LIB}
            ${GSTREAMER_VIDEO_LIB}
            ${GSTREAMER_AUDIO_LIB}
            ${GSTREAMER_APP_LIB}
            ${GSTREAMER_BASE_LIB}
            ${GLIB_LIB}
            ${GOBJECT_LIB}
        )
        target_compile_definitions(kassetmanagerqt PRIVATE HAVE_GSTREAMER)
        message(STATUS "GStreamer libraries linked successfully")
    else()
        message(FATAL_ERROR "GStreamer libraries not found in ${GSTREAMER_LIBRARY_DIRS}")
    endif()
endif()

# Try to link FFmpeg core libs when headers are present (for conversion tool only)
if(FFMPEG_FOUND)
    if(NOT DEFINED FFMPEG_LIBRARY_DIRS)
        if(DEFINED ENV{VCPKG_ROOT})
            set(FFMPEG_LIBRARY_DIRS "$ENV{VCPKG_ROOT}/installed/${VCPKG_TARGET_TRIPLET}/lib")
        endif()
    endif()
    find_library(FFMPEG_AVFORMAT avformat HINTS ${FFMPEG_LIBRARY_DIRS})
    find_library(FFMPEG_AVCODEC  avcodec  HINTS ${FFMPEG_LIBRARY_DIRS})
    find_library(FFMPEG_AVUTIL     avutil     HINTS ${FFMPEG_LIBRARY_DIRS})
    find_library(FFMPEG_SWSCALE    swscale    HINTS ${FFMPEG_LIBRARY_DIRS})
    find_library(FFMPEG_SWRESAMPLE swresample HINTS ${FFMPEG_LIBRARY_DIRS})

    if(FFMPEG_AVFORMAT AND FFMPEG_AVCODEC AND FFMPEG_AVUTIL)
        target_link_libraries(kassetmanagerqt PRIVATE ${FFMPEG_AVFORMAT} ${FFMPEG_AVCODEC} ${FFMPEG_AVUTIL})
        if(FFMPEG_SWSCALE)
            target_link_libraries(kassetmanagerqt PRIVATE ${FFMPEG_SWSCALE})
            message(STATUS "FFmpeg swscale linked: ${FFMPEG_SWSCALE}")
        else()
            message(WARNING "FFmpeg swscale not found. FFmpeg thumbnail fallback will be disabled.")
        endif()
        if(FFMPEG_SWRESAMPLE)
            target_link_libraries(kassetmanagerqt PRIVATE ${FFMPEG_SWRESAMPLE})
            message(STATUS "FFmpeg swresample linked: ${FFMPEG_SWRESAMPLE}")
        else()
            message(WARNING "FFmpeg swresample not found. FFmpeg audio playback will be disabled.")
        endif()
        target_compile_definitions(kassetmanagerqt PRIVATE HAVE_FFMPEG)
        message(STATUS "FFmpeg libraries linked: ${FFMPEG_AVFORMAT}; ${FFMPEG_AVCODEC}; ${FFMPEG_AVUTIL}")
    else()
        message(WARNING "FFmpeg headers found but libraries missing. Install via vcpkg: vcpkg install ffmpeg:x64-windows")
    endif()
endif()


target_link_libraries(kassetmanagerqt PRIVATE
    Qt6::Widgets
    Qt6::Multimedia
    Qt6::MultimediaWidgets
    Qt6::Sql
    Qt6::Concurrent
    Qt6::Svg
    Qt6::SvgWidgets
)
# Link Qt PDF if available (widgets optional)
if (TARGET Qt6::Pdf)
  target_link_libraries(kassetmanagerqt PRIVATE Qt6::Pdf)
  target_compile_definitions(kassetmanagerqt PRIVATE HAVE_QT_PDF)
endif()
if (TARGET Qt6::PdfWidgets)
  target_link_libraries(kassetmanagerqt PRIVATE Qt6::PdfWidgets)
  target_compile_definitions(kassetmanagerqt PRIVATE HAVE_QT_PDF_WIDGETS)
endif()
# Link OpenImageIO if found
if(OpenImageIO_FOUND)
    target_link_libraries(kassetmanagerqt PRIVATE OpenImageIO::OpenImageIO)
endif()

# Link tlRender if enabled and found
## tlRender linking removed
# Link minizip-ng (vcpkg target)
target_link_libraries(kassetmanagerqt PRIVATE MINIZIP::minizip-ng)
target_compile_definitions(kassetmanagerqt PRIVATE HAVE_MINIZIP_NG)


# Windows-specific: hide console in release
if (WIN32)
  set_target_properties(kassetmanagerqt PROPERTIES WIN32_EXECUTABLE TRUE)
  target_link_libraries(kassetmanagerqt PRIVATE Ole32 Shell32 DbgHelp Psapi)
endif()
# Install rules
install(TARGETS kassetmanagerqt RUNTIME DESTINATION bin)

# Install icons folder
set(_ICONS_DIR "${CMAKE_SOURCE_DIR}/../../icons")
if(EXISTS "${_ICONS_DIR}")
  install(DIRECTORY "${_ICONS_DIR}" DESTINATION bin)
  message(STATUS "Icons folder will be installed to bin/icons directory")
else()
  message(WARNING "Icons folder not found at ${_ICONS_DIR}")
endif()

# Packaging: deploy Qt runtime. Prefer Qt6DeploySupport if available.
if (WIN32)
  include(Qt6DeploySupport OPTIONAL RESULT_VARIABLE _qt_deploy_mod)
  if (_qt_deploy_mod)
    qt_generate_deploy_app_script(
      TARGET kassetmanagerqt
      OUTPUT_SCRIPT deploy_script
    )
    install(SCRIPT ${deploy_script})
  else()
    # Fallback to manual windeployqt helper
    set(_DEPLOY_QT_CMAKE "${CMAKE_CURRENT_SOURCE_DIR}/cmake/DeployQt.cmake")
    install(CODE "\ninclude(\"${_DEPLOY_QT_CMAKE}\")\nset(_exe \"\${CMAKE_INSTALL_PREFIX}/bin/kassetmanagerqt.exe\")\nmessage(STATUS \"Invoking windeployqt for \${_exe}\")\nqt_deploy(\"\${_exe}\" \"\${CMAKE_INSTALL_PREFIX}\" \"\")\n")
  endif()

  # Install Qt6Test DLL for test executables
  get_target_property(_QT_INSTALL_PATH Qt6::Core IMPORTED_LOCATION_RELEASE)
  get_filename_component(_QT_BIN_PATH "${_QT_INSTALL_PATH}" DIRECTORY)
  if(EXISTS "${_QT_BIN_PATH}/Qt6Test.dll")
    install(FILES "${_QT_BIN_PATH}/Qt6Test.dll" DESTINATION bin)
  endif()

  # Install Qt plugins for tests (SQL driver and platform plugin)
  get_filename_component(_QT_PLUGINS_PATH "${_QT_BIN_PATH}/../plugins" ABSOLUTE)
  if(EXISTS "${_QT_PLUGINS_PATH}/sqldrivers/qsqlited.dll")
    install(FILES "${_QT_PLUGINS_PATH}/sqldrivers/qsqlited.dll" DESTINATION plugins/sqldrivers)
  elseif(EXISTS "${_QT_PLUGINS_PATH}/sqldrivers/qsqlite.dll")
    install(FILES "${_QT_PLUGINS_PATH}/sqldrivers/qsqlite.dll" DESTINATION plugins/sqldrivers)
  endif()
  # Always ship qwindows platform plugin for GUI. Also include qoffscreen for headless.
  if(EXISTS "${_QT_PLUGINS_PATH}/platforms/qwindows.dll")
    install(FILES "${_QT_PLUGINS_PATH}/platforms/qwindows.dll" DESTINATION plugins/platforms)
  endif()
  if(EXISTS "${_QT_PLUGINS_PATH}/platforms/qoffscreend.dll")
    install(FILES "${_QT_PLUGINS_PATH}/platforms/qoffscreend.dll" DESTINATION plugins/platforms)
  elseif(EXISTS "${_QT_PLUGINS_PATH}/platforms/qoffscreen.dll")
    install(FILES "${_QT_PLUGINS_PATH}/platforms/qoffscreen.dll" DESTINATION plugins/platforms)
  endif()

  # Ensure Qt loads plugins from "plugins" next to the exe (more robust than relying on PATH)
  set(_QT_CONF "${CMAKE_CURRENT_BINARY_DIR}/qt.conf")
  file(WRITE "${_QT_CONF}" "[Paths]\nPlugins=plugins\n")
  install(FILES "${_QT_CONF}" DESTINATION bin)

  # Install all vcpkg DLLs (OpenImageIO, zlib, bzip2, liblzma, libpng, libjpeg, etc.)
  set(_VCPKG_BIN "")
  if(DEFINED ENV{VCPKG_ROOT})
    set(_VCPKG_BIN "$ENV{VCPKG_ROOT}/installed/${VCPKG_TARGET_TRIPLET}/bin")
  endif()
  if(_VCPKG_BIN AND EXISTS "${_VCPKG_BIN}")
    file(GLOB _VCPKG_DLLS "${_VCPKG_BIN}/*.dll")
    foreach(_dll ${_VCPKG_DLLS})
      get_filename_component(_dll_name "${_dll}" NAME)
      # Skip FFmpeg DLLs - we handle those separately
      if(NOT _dll_name MATCHES "^(avcodec|avdevice|avfilter|avformat|avutil|postproc|swresample|swscale)")
        install(FILES "${_dll}" DESTINATION bin)
      endif()
    endforeach()
  endif()

  # Install custom FFmpeg DLLs if FFMPEG_ROOT is set
  if(DEFINED ENV{FFMPEG_ROOT})
    file(TO_CMAKE_PATH "$ENV{FFMPEG_ROOT}/bin" _FFMPEG_BIN)
    if(EXISTS "${_FFMPEG_BIN}")
      file(GLOB _FFMPEG_DLLS "${_FFMPEG_BIN}/*.dll")
      foreach(_dll ${_FFMPEG_DLLS})
        install(FILES "${_dll}" DESTINATION bin)
      endforeach()
    endif()
  endif()

  # Install Everything SDK DLL if available
  set(_EVERYTHING_DLL "${CMAKE_SOURCE_DIR}/../../third_party/everything/Everything64.dll")
  if(EXISTS "${_EVERYTHING_DLL}")
    install(FILES "${_EVERYTHING_DLL}" DESTINATION bin)
    message(STATUS "Everything64.dll will be installed to bin directory")
  else()
    message(WARNING "Everything64.dll not found at ${_EVERYTHING_DLL}")
    message(WARNING "Run scripts/download-everything-sdk.ps1 to download the Everything SDK")
  endif()

  # Install GStreamer runtime files
  if(GSTREAMER_ROOT AND EXISTS "${GSTREAMER_ROOT}")
    # Install GStreamer DLLs from bin directory
    file(GLOB _GSTREAMER_DLLS "${GSTREAMER_ROOT}/bin/*.dll")
    foreach(_dll ${_GSTREAMER_DLLS})
      install(FILES "${_dll}" DESTINATION bin)
    endforeach()
    message(STATUS "GStreamer DLLs will be installed from ${GSTREAMER_ROOT}/bin")

    # Install GStreamer plugins
    if(EXISTS "${GSTREAMER_ROOT}/lib/gstreamer-1.0")
      install(DIRECTORY "${GSTREAMER_ROOT}/lib/gstreamer-1.0" DESTINATION lib)
      message(STATUS "GStreamer plugins will be installed from ${GSTREAMER_ROOT}/lib/gstreamer-1.0")
    endif()
  endif()
endif()

# End BUILD_APP block
endif()


# Enable testing
enable_testing()

# Add tests subdirectory (optional, can be disabled with -DBUILD_TESTS=OFF)
option(BUILD_TESTS "Build unit tests" ON)
if(BUILD_TESTS)
    add_subdirectory(tests)
endif()

# CPack configuration (NSIS + ZIP)
set(CPACK_PACKAGE_NAME "KAsset Manager Qt")
set(CPACK_PACKAGE_VENDOR "KAM")
set(CPACK_PACKAGE_VERSION ${PROJECT_VERSION})
set(CPACK_PACKAGE_INSTALL_DIRECTORY "KAssetManagerQt")
set(CPACK_GENERATOR "ZIP;NSIS")
include(CPack)


# tlRender (optional external package)
## tlRender integration removed; build without it
