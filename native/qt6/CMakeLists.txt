cmake_minimum_required(VERSION 3.21)
cmake_minimum_required(VERSION 3.21)

# Set vcpkg toolchain file from environment if not already set
if(NOT DEFINED CMAKE_TOOLCHAIN_FILE)
    if(DEFINED ENV{VCPKG_ROOT} AND EXISTS "$ENV{VCPKG_ROOT}/scripts/buildsystems/vcpkg.cmake")
        set(CMAKE_TOOLCHAIN_FILE "$ENV{VCPKG_ROOT}/scripts/buildsystems/vcpkg.cmake" CACHE STRING "Vcpkg toolchain file")
        # Default triplet; can be overridden by user
        if(NOT DEFINED VCPKG_TARGET_TRIPLET)
            if(WIN32)
                set(VCPKG_TARGET_TRIPLET "x64-windows" CACHE STRING "Vcpkg target triplet")
            else()
                set(VCPKG_TARGET_TRIPLET "x64-linux" CACHE STRING "Vcpkg target triplet")
            endif()
        endif()
        message(STATUS "Using vcpkg toolchain: ${CMAKE_TOOLCHAIN_FILE}")
        message(STATUS "Using vcpkg triplet: ${VCPKG_TARGET_TRIPLET}")
    endif()
endif()

project(KAssetManagerQt VERSION 1.8.5 LANGUAGES CXX)
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

find_package(QT NAMES Qt6 REQUIRED COMPONENTS Widgets Multimedia MultimediaWidgets Sql Concurrent Svg SvgWidgets OpenGL OpenGLWidgets Quick QuickWidgets DBus)
find_package(Qt6 REQUIRED COMPONENTS Widgets Multimedia MultimediaWidgets Sql Concurrent Svg SvgWidgets OpenGL OpenGLWidgets Quick QuickWidgets DBus)
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

# OOXML ZIP reading - will be found from tlRender's bundled minizip-ng
# Moved the find_package call to after tlRender setup to use its bundled version

# =============================================================================
# tlRender - Professional video/image sequence playback with OCIO color management
# =============================================================================
# tlRender provides:
# - Image sequence playback (EXR, TIFF, DPX, etc.)
# - Video playback (via FFmpeg)
# - Full ACES/OpenColorIO color management pipeline
# - Qt6 widgets for viewport and timeline
#
# The tlRender package includes all dependencies statically linked:
# - OpenColorIO, OpenImageIO, OpenEXR, FFmpeg, etc.
# - ftk (feather-tk) for UI/GL rendering
# - Qt integration via tlQt and tlQtWidget libraries

# Find tlRender installation
set(_tlrender_candidates
    "${CMAKE_SOURCE_DIR}/../third_party/tlRender-build/install-Release"
    "${CMAKE_SOURCE_DIR}/../../third_party/tlRender-build/install-Release"
    "${CMAKE_SOURCE_DIR}/../third_party/tlRender-install-Release"
    "${CMAKE_SOURCE_DIR}/../../third_party/tlRender-install-Release"
    "${CMAKE_CURRENT_LIST_DIR}/../third_party/tlRender-build/install-Release"
    "${CMAKE_CURRENT_LIST_DIR}/../../third_party/tlRender-build/install-Release"
    "${CMAKE_CURRENT_LIST_DIR}/../third_party/tlRender-install-Release"
    "${CMAKE_CURRENT_LIST_DIR}/../../third_party/tlRender-install-Release"
)
foreach(_candidate IN LISTS _tlrender_candidates)
    get_filename_component(_abs_candidate "${_candidate}" ABSOLUTE)
    if(EXISTS "${_abs_candidate}/lib/cmake/tlRender")
        set(TLRENDER_ROOT "${_abs_candidate}")
        break()
    endif()
endforeach()

if(TLRENDER_ROOT)
    # Add tlRender and ftk to prefix path
    # ftk is in share/ftk, tlRender is in lib/cmake/tlRender
    list(PREPEND CMAKE_PREFIX_PATH "${TLRENDER_ROOT}" "${TLRENDER_ROOT}/share/ftk")
    message(STATUS "Using tlRender from ${TLRENDER_ROOT}")
    
    # Force CMake to use tlRender's bundled ZLIB to avoid duplicate symbol issues.
    # The install layout differs by platform (.lib on Windows, libz.a on Linux).
    set(_tlrender_zlib_library "")
    if(WIN32 AND EXISTS "${TLRENDER_ROOT}/lib/zlib.lib")
        set(_tlrender_zlib_library "${TLRENDER_ROOT}/lib/zlib.lib")
    elseif(EXISTS "${TLRENDER_ROOT}/lib/libz.a")
        set(_tlrender_zlib_library "${TLRENDER_ROOT}/lib/libz.a")
    elseif(EXISTS "${TLRENDER_ROOT}/lib64/libz.a")
        set(_tlrender_zlib_library "${TLRENDER_ROOT}/lib64/libz.a")
    endif()
    set(ZLIB_ROOT "${TLRENDER_ROOT}" CACHE PATH "ZLIB root" FORCE)
    if(_tlrender_zlib_library)
        set(ZLIB_LIBRARY "${_tlrender_zlib_library}" CACHE FILEPATH "ZLIB library" FORCE)
        set(ZLIB_LIBRARY_RELEASE "${_tlrender_zlib_library}" CACHE FILEPATH "ZLIB Release library" FORCE)
    endif()
    set(ZLIB_INCLUDE_DIR "${TLRENDER_ROOT}/include" CACHE PATH "ZLIB include dir" FORCE)
    # Clear any cached found status
    unset(ZLIB_FOUND CACHE)
    
    # Find tlRender package - this will find all dependencies automatically
    # including ftk, OpenColorIO, OpenImageIO, OpenEXR, FFmpeg, etc.
    find_package(tlRender CONFIG REQUIRED)
    
    # Check if Qt integration libraries are available
    if(TARGET tlRender::tlQt AND TARGET tlRender::tlQtWidget)
        set(TLRENDER_HAS_QT TRUE)
        message(STATUS "tlRender Qt6 integration available (tlQt, tlQtWidget)")
    else()
        message(WARNING "tlRender Qt6 libraries not found. Rebuild with -DTLRENDER_QT6=ON")
    endif()
    
    # tlRender found - all dependencies resolved via imported targets
    message(STATUS "tlRender configured with OCIO and OIIO support")

    # NOTE: We intentionally do not define HAVE_OPENIMAGEIO here.
    # KAssetManager's own OIIO-based loaders are optional; tlRender is the
    # required playback path and brings its own bundled OpenImageIO/OpenColorIO.
    
    # Find minizip-ng from tlRender's bundled version (for OOXML ZIP reading)
    find_package(minizip-ng CONFIG REQUIRED)
    
    set(TLRENDER_FOUND TRUE)
else()
    message(FATAL_ERROR "tlRender not found! tlRender is required for video/image sequence playback.\n"
        "Build tlRender by running:\n"
        "  Linux: configure/build tlRender into third_party/tlRender-install-Release\n"
        "  Windows: cd third_party/tlRender-build && .\\build-tlrender.ps1\n"
        "This will build tlRender with all dependencies including OpenColorIO and OpenImageIO.")
endif()

# FFmpeg is provided by tlRender - just set the HAVE_FFMPEG define
# tlRender imports FFmpeg as transitive dependencies, so no separate linking needed
if(TLRENDER_FOUND)
    set(FFMPEG_FOUND TRUE)
    message(STATUS "FFmpeg provided by tlRender (transitive dependency)")
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
    src/i_asset_database.h
    src/project_db.h
    src/project_db.cpp
    src/project_import_worker.h
    src/project_import_worker.cpp
    src/project_version_detector.h
    src/project_version_detector.cpp
    src/projects_model.h
    src/projects_model.cpp
    src/project_assets_model.h
    src/project_assets_model.cpp
    src/project_folders_model.h
    src/project_folders_model.cpp
    src/project_sequence_grouping_proxy_model.h
    src/project_sequence_grouping_proxy_model.cpp
    src/project_item_delegate.h
    src/project_item_delegate.cpp
    src/project_manager_watcher.h
    src/project_manager_watcher.cpp
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
    src/image_preview_overlay.h
    src/image_preview_overlay.cpp
    src/import_progress_dialog.h
    src/import_progress_dialog.cpp
    src/settings_dialog.h
    src/settings_dialog.cpp
    src/user_guide_dialog.h
    src/user_guide_dialog.cpp
    src/star_rating_widget.h
    src/star_rating_widget.cpp
    src/sequence_detector.h
    src/sequence_detector.cpp
    src/sequence_grouping_proxy_model.h
    src/sequence_grouping_proxy_model.cpp
    src/asset_sequence_grouping_proxy_model.h
    src/asset_sequence_grouping_proxy_model.cpp
    src/asset_grid_view.h
    src/asset_grid_view.cpp
    src/icon_utils.h
    src/icon_utils.cpp
    src/file_utils.cpp
    src/asset_item_delegate.h
    src/asset_item_delegate.cpp
    src/fm_item_delegate.h
    src/fm_item_delegate.cpp
    src/grid_scrub.h
    src/grid_scrub.cpp
    src/scrub_frame_registry.h
    src/scrub_frame_registry.cpp
    src/fm_views_ex.h
    src/fm_views_ex.cpp
    src/file_manager_pane.h
    src/file_manager_pane.cpp
    src/image_memory_limits.h
    src/image_memory_limits.cpp
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
    src/everything_folder_model.h
    src/everything_folder_model.cpp
    src/everything_search_dialog.h
    src/utils.h
    src/utils.cpp

    src/everything_search_dialog.cpp
    src/media_convert_dialog.h
    src/media_convert_dialog.cpp
    src/media_converter_worker.h
    src/media_converter_worker.cpp
    src/thumbnail_cache_manager.h
    src/thumbnail_cache_manager.cpp
    src/thumbnail_generator_dialog.h
    src/thumbnail_generator_dialog.cpp
    src/thumbnail_generator_worker.h
    src/thumbnail_generator_worker.cpp
    # tlRender integration - professional video/sequence playback with OCIO
    src/media/media_player_types.h
    src/media/libmpv_headers.h
    src/media/libmpv_runtime.h
    src/media/libmpv_runtime.cpp
    src/media/mpv_player.h
    src/media/mpv_player.cpp
    src/media/mpv_viewport.h
    src/media/mpv_viewport.cpp
    src/media/ffmpeg_mov_player.h
    src/media/ffmpeg_mov_player.cpp
    src/media/ffmpeg_mov_viewport.h
    src/media/ffmpeg_mov_viewport.cpp
    src/media/tlrender_player.h
    src/media/tlrender_player.cpp
    src/media/tlrender_widget.h
    src/media/tlrender_widget.cpp
    src/media/tlrender_viewport.h
    src/media/tlrender_viewport.cpp
    src/theme_manager.h
    src/theme_manager.cpp
    src/annotation_items.h
    src/annotation_items.cpp
    src/annotation_layer.h
    src/annotation_layer.cpp
    ${APP_RESOURCES}
)

target_compile_definitions(kassetmanagerqt PRIVATE
    KAM_APP_VERSION="${PROJECT_VERSION}"
    QT_NO_DEBUG_OUTPUT
    QT_NO_INFO_OUTPUT
)

target_include_directories(kassetmanagerqt PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/src
)

# Add tlRender include directories
if(TLRENDER_FOUND)
    if(TLRENDER_INCLUDE_DIR)
        target_include_directories(kassetmanagerqt PRIVATE ${TLRENDER_INCLUDE_DIR})
    elseif(TLRENDER_ROOT)
        target_include_directories(kassetmanagerqt PRIVATE "${TLRENDER_ROOT}/include")
    endif()
endif()

# =============================================================================
# Link tlRender using CMake imported targets
# =============================================================================
# The tlRender CMake package exports properly configured targets with all
# INTERFACE_COMPILE_DEFINITIONS (TL_STATIC, FTK_STATIC, etc.) and
# INTERFACE_LINK_LIBRARIES set up. This means we just link to the top-level
# targets and everything else is resolved automatically.

if(TLRENDER_FOUND)
    # Link tlRender core libraries via imported targets
    # These targets bring in all transitive dependencies automatically
    target_link_libraries(kassetmanagerqt PRIVATE
        tlRender::tlTimeline    # Timeline/player functionality
        tlRender::tlGL          # OpenGL rendering
        tlRender::tlIO          # File I/O (EXR, TIFF, DPX, etc.)
        tlRender::tlCore        # Core utilities
        tlRender::tlUI          # UI widgets
        tlRender::tlDevice      # Device handling
    )
    
    # Link Qt integration libraries if available
    if(TLRENDER_HAS_QT)
        target_link_libraries(kassetmanagerqt PRIVATE
            tlRender::tlQt          # Qt object wrappers
            tlRender::tlQtWidget    # Qt widget implementations (Viewport, etc.)
        )
    endif()
    
    # Add compile definitions to enable tlRender + bundled deps code paths.
    # NOTE: We intentionally rely on tlRender's bundled dependencies to avoid ABI/version mixing.
    # These are used throughout the app (thumbnails, still-image preview, OCIO UI lists).
    target_compile_definitions(kassetmanagerqt PRIVATE
      HAVE_TLRENDER=1
      HAVE_OPENIMAGEIO=1
      HAVE_OCIO=1
    )
    
    # Windows system libraries required by tlRender dependencies
    if(WIN32)
        target_link_libraries(kassetmanagerqt PRIVATE
            opengl32
            winmm
            imm32
            version
            setupapi
            # Windows Media Foundation (for WMF decoder)
            mf
            mfplat
            mfreadwrite
            mfuuid
            propsys
            wmcodecdspuuid
            # Other Windows libs
            psapi
            dwmapi
        )
    endif()
    
    message(STATUS "tlRender linked via imported targets with OCIO color management")
endif()

# FFmpeg is provided by tlRender as a transitive dependency
# Just set the compile definition for code that checks HAVE_FFMPEG
if(TLRENDER_FOUND)
    target_compile_definitions(kassetmanagerqt PRIVATE HAVE_FFMPEG)
    message(STATUS "FFmpeg support enabled via tlRender")
endif()


target_link_libraries(kassetmanagerqt PRIVATE
    Qt6::Widgets
    Qt6::Multimedia
    Qt6::MultimediaWidgets
    Qt6::Sql
    Qt6::Concurrent
    Qt6::DBus
    Qt6::Svg
    Qt6::SvgWidgets
    Qt6::OpenGL
    Qt6::OpenGLWidgets
    Qt6::Quick
    Qt6::QuickWidgets
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

# OpenImageIO is provided by tlRender - no need to link separately

# Link minizip-ng from the tlRender install
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
set(_ICONS_DIR "${CMAKE_SOURCE_DIR}/../../Icons")
if(EXISTS "${_ICONS_DIR}")
  install(DIRECTORY "${_ICONS_DIR}" DESTINATION bin)
  message(STATUS "Icons folder will be installed to bin/Icons directory")
else()
  message(WARNING "Icons folder not found at ${_ICONS_DIR}")
endif()

# Install bundled ACES OCIO config (ships with repo) so the portable build has a known-good config.
set(_ACES_OCIO_DIR "${CMAKE_SOURCE_DIR}/../../OpenColorIO-Config-ACES-1.2")
if(EXISTS "${_ACES_OCIO_DIR}")
  install(DIRECTORY "${_ACES_OCIO_DIR}" DESTINATION bin)
  message(STATUS "ACES OCIO config will be installed to bin/OpenColorIO-Config-ACES-1.2")
else()
  message(WARNING "ACES OCIO config folder not found at ${_ACES_OCIO_DIR}")
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

  # Install tlRender runtime files (DLLs if using shared libs)
  if(TLRENDER_ROOT AND EXISTS "${TLRENDER_ROOT}/bin")
    file(GLOB _TLRENDER_DLLS "${TLRENDER_ROOT}/bin/*.dll")
    foreach(_dll ${_TLRENDER_DLLS})
      install(FILES "${_dll}" DESTINATION bin)
    endforeach()
    if(_TLRENDER_DLLS)
      message(STATUS "tlRender DLLs will be installed from ${TLRENDER_ROOT}/bin")
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

# CPack configuration
set(CPACK_PACKAGE_NAME "KAsset Manager Qt")
set(CPACK_PACKAGE_VENDOR "KAM")
set(CPACK_PACKAGE_VERSION ${PROJECT_VERSION})
set(CPACK_PACKAGE_INSTALL_DIRECTORY "KAssetManagerQt")
if(WIN32)
  set(CPACK_GENERATOR "ZIP;NSIS")
else()
  set(CPACK_GENERATOR "TGZ")
endif()
include(CPack)


# tlRender (optional external package)
## tlRender integration removed; build without it
