cmake_minimum_required(VERSION 3.21)

# Set vcpkg toolchain file if not already set and vcpkg exists
if(NOT DEFINED CMAKE_TOOLCHAIN_FILE AND EXISTS "C:/vcpkg/scripts/buildsystems/vcpkg.cmake")
    set(CMAKE_TOOLCHAIN_FILE "C:/vcpkg/scripts/buildsystems/vcpkg.cmake" CACHE STRING "Vcpkg toolchain file")
    # Use x64-windows triplet (MSVC) for OpenImageIO since MinGW has compatibility issues
    set(VCPKG_TARGET_TRIPLET "x64-windows" CACHE STRING "Vcpkg target triplet")
    message(STATUS "Using vcpkg toolchain: ${CMAKE_TOOLCHAIN_FILE}")
    message(STATUS "Using vcpkg triplet: ${VCPKG_TARGET_TRIPLET}")
endif()

project(KAssetManagerQt VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

find_package(QT NAMES Qt6 REQUIRED COMPONENTS Widgets Multimedia MultimediaWidgets Sql Concurrent)
find_package(Qt6 REQUIRED COMPONENTS Widgets Multimedia MultimediaWidgets Sql Concurrent)

# Find OpenImageIO (optional - will use placeholders if not found)
find_package(OpenImageIO CONFIG QUIET)
if(OpenImageIO_FOUND)
    message(STATUS "OpenImageIO found - advanced format support enabled")
    add_compile_definitions(HAVE_OPENIMAGEIO)
else()
    message(WARNING "OpenImageIO not found - will use placeholder thumbnails for EXR/PSD/HDR formats")
endif()

# Find FFmpeg for log suppression
find_package(PkgConfig QUIET)
if(PkgConfig_FOUND)
    pkg_check_modules(FFMPEG QUIET libavutil)
endif()
if(NOT FFMPEG_FOUND)
    # Try vcpkg FFmpeg
    find_path(FFMPEG_INCLUDE_DIR libavutil/log.h PATHS "C:/vcpkg/installed/x64-windows/include")
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

qt_add_executable(kassetmanagerqt
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
    src/thumbnail_generator.h
    src/thumbnail_generator.cpp
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
    ${APP_RESOURCES}
)

target_include_directories(kassetmanagerqt PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/src
)

# Add FFmpeg include directory if found
if(FFMPEG_FOUND)
    if(FFMPEG_INCLUDE_DIR)
        target_include_directories(kassetmanagerqt PRIVATE ${FFMPEG_INCLUDE_DIR})
    elseif(FFMPEG_INCLUDE_DIRS)
        target_include_directories(kassetmanagerqt PRIVATE ${FFMPEG_INCLUDE_DIRS})
    endif()
endif()

target_link_libraries(kassetmanagerqt PRIVATE
    Qt6::Widgets
    Qt6::Multimedia
    Qt6::MultimediaWidgets
    Qt6::Sql
    Qt6::Concurrent
)

# Link OpenImageIO if found
if(OpenImageIO_FOUND)
    target_link_libraries(kassetmanagerqt PRIVATE OpenImageIO::OpenImageIO)
endif()

# Windows-specific: hide console in release
if (WIN32)
  set_target_properties(kassetmanagerqt PROPERTIES WIN32_EXECUTABLE TRUE)
  target_link_libraries(kassetmanagerqt PRIVATE Ole32 Shell32)
endif()
# Install rules
install(TARGETS kassetmanagerqt RUNTIME DESTINATION bin)

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
endif()

# CPack configuration (NSIS + ZIP)
set(CPACK_PACKAGE_NAME "KAsset Manager Qt")
set(CPACK_PACKAGE_VENDOR "KAM")
set(CPACK_PACKAGE_VERSION ${PROJECT_VERSION})
set(CPACK_PACKAGE_INSTALL_DIRECTORY "KAssetManagerQt")
set(CPACK_GENERATOR "ZIP;NSIS")
include(CPack)


