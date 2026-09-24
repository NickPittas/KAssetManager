find_package(Qt6 REQUIRED COMPONENTS Core Gui Widgets Multimedia MultimediaWidgets Sql Concurrent Test Svg SvgWidgets OpenGL OpenGLWidgets Quick QuickWidgets DBus)
find_package(PkgConfig QUIET)
if(PkgConfig_FOUND)
    pkg_check_modules(LIBMPV QUIET IMPORTED_TARGET mpv NO_CMAKE_PATH NO_CMAKE_ENVIRONMENT_PATH)
endif()

# Test executable: test_simple
add_executable(test_simple
    test_simple.cpp
)
set_property(TARGET test_simple PROPERTY AUTOMOC ON)

target_link_libraries(test_simple PRIVATE Qt6::Test)
target_include_directories(test_simple PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/../src)

add_test(NAME test_simple COMMAND test_simple)
set_tests_properties(test_simple PROPERTIES WORKING_DIRECTORY "${CMAKE_BINARY_DIR}/install_run/bin")

# Test executable: test_db
add_executable(test_db
    test_db.cpp
    ../src/db.cpp
    ../src/db.h
    ../src/log_manager.cpp
    ../src/log_manager.h
    ../src/runtime_paths.cpp
    ../src/runtime_paths.h
)
set_property(TARGET test_db PROPERTY AUTOMOC ON)

# Test executable: test_db_worker
add_executable(test_db_worker
    test_db_worker.cpp
    ../src/db_worker.cpp
    ../src/db_worker.h
)

# Test executable: test_project_watchers
add_executable(test_project_watchers
    test_project_watchers.cpp
    ../src/project_folder_watcher.cpp
    ../src/project_folder_watcher.h
    ../src/project_manager_watcher.cpp
    ../src/project_manager_watcher.h
    ../src/file_utils.cpp
    ../src/file_utils.h
    ../src/project_db.cpp
    ../src/project_db.h
    ../src/project_path_utils.h
    ../src/project_version_detector.cpp
    ../src/project_version_detector.h
    ../src/log_manager.cpp
    ../src/log_manager.h
    ../src/runtime_paths.cpp
    ../src/runtime_paths.h
)

set_property(TARGET test_project_watchers PROPERTY AUTOMOC ON)

target_link_libraries(test_project_watchers PRIVATE Qt6::Test Qt6::Core Qt6::Sql Qt6::Concurrent)
target_include_directories(test_project_watchers PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/../src)

add_test(NAME test_project_watchers COMMAND test_project_watchers)
install(TARGETS test_project_watchers DESTINATION bin)


# Test executable: test_media_async_request
add_executable(test_media_async_request
    test_media_async_request.cpp
    ../src/media_async_request.cpp
    ../src/media_async_request.h
)

set_property(TARGET test_media_async_request PROPERTY AUTOMOC ON)

target_link_libraries(test_media_async_request PRIVATE Qt6::Test Qt6::Core Qt6::Concurrent)
target_include_directories(test_media_async_request PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/../src)

add_test(NAME test_media_async_request COMMAND test_media_async_request)
install(TARGETS test_media_async_request DESTINATION bin)
set_property(TARGET test_db_worker PROPERTY AUTOMOC ON)

target_link_libraries(test_db_worker PRIVATE Qt6::Test Qt6::Sql Qt6::Core)
target_include_directories(test_db_worker PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/../src)

add_test(NAME test_db_worker COMMAND test_db_worker)
install(TARGETS test_db_worker DESTINATION bin)

target_link_libraries(test_db PRIVATE Qt6::Test Qt6::Sql Qt6::Core)
target_include_directories(test_db PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/../src)

add_test(NAME test_db COMMAND test_db)

# Test executable: test_project_db
add_executable(test_project_db
    test_project_db.cpp
    ../src/project_db.cpp
    ../src/project_db.h
    ../src/project_path_utils.h
    ../src/log_manager.cpp
    ../src/log_manager.h
    ../src/runtime_paths.cpp
    ../src/runtime_paths.h
)
set_property(TARGET test_project_db PROPERTY AUTOMOC ON)

target_link_libraries(test_project_db PRIVATE Qt6::Test Qt6::Sql Qt6::Core)
target_include_directories(test_project_db PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/../src)

add_test(NAME test_project_db COMMAND test_project_db)

# Test executable: test_project_assets_model
add_executable(test_project_assets_model
    test_project_assets_model.cpp
    ../src/project_assets_model.cpp
    ../src/project_assets_model.h
    ../src/project_db.cpp
    ../src/project_db.h
    ../src/project_version_detector.cpp
    ../src/project_version_detector.h
    ../src/i_asset_database.h
    ../src/project_path_utils.h
    ../src/log_manager.cpp
    ../src/log_manager.h
    ../src/runtime_paths.cpp
    ../src/runtime_paths.h
)

set_property(TARGET test_project_assets_model PROPERTY AUTOMOC ON)

target_link_libraries(test_project_assets_model PRIVATE Qt6::Test Qt6::Core Qt6::Sql)

target_include_directories(test_project_assets_model PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/../src)

add_test(NAME test_project_assets_model COMMAND test_project_assets_model)
set_tests_properties(test_project_assets_model PROPERTIES WORKING_DIRECTORY "${CMAKE_BINARY_DIR}/install_run/bin")

install(TARGETS test_project_assets_model DESTINATION bin)

# Test executable: test_models
add_executable(test_models
    test_models.cpp
    ../src/assets_model.cpp
    ../src/assets_model.h
    ../src/db.cpp
    ../src/db.h
    ../src/log_manager.cpp
    ../src/log_manager.h
    ../src/runtime_paths.cpp
    ../src/runtime_paths.h
)
set_property(TARGET test_models PROPERTY AUTOMOC ON)

target_link_libraries(test_models PRIVATE Qt6::Test Qt6::Sql Qt6::Core Qt6::Widgets)
target_include_directories(test_models PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/../src)

add_test(NAME test_models COMMAND test_models)
set_tests_properties(test_models PROPERTIES WORKING_DIRECTORY "${CMAKE_BINARY_DIR}/install_run/bin")

# Test executable: test_sequence_detector
add_executable(test_sequence_detector
    test_sequence_detector.cpp
    ../src/sequence_detector.cpp
    ../src/sequence_detector.h
)
set_property(TARGET test_sequence_detector PROPERTY AUTOMOC ON)

target_link_libraries(test_sequence_detector PRIVATE Qt6::Test Qt6::Core)

target_include_directories(test_sequence_detector PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/../src)

add_test(NAME test_sequence_detector COMMAND test_sequence_detector)
set_tests_properties(test_sequence_detector PROPERTIES WORKING_DIRECTORY "${CMAKE_BINARY_DIR}/install_run/bin")

# Install test executables to bin directory
install(TARGETS test_simple test_db test_project_db test_project_assets_model test_models test_sequence_detector DESTINATION bin)


# Test executable: test_importer
add_executable(test_importer
    test_importer.cpp
    ../src/importer.cpp
    ../src/importer.h
    ../src/import_controller.cpp
    ../src/import_controller.h
    ../src/db_worker.cpp
    ../src/db_worker.h
    ../src/db.cpp
    ../src/db.h
    ../src/log_manager.cpp
    ../src/log_manager.h
    ../src/runtime_paths.cpp
    ../src/runtime_paths.h
    ../src/sequence_detector.cpp
    ../src/sequence_detector.h
    ../src/file_utils.h
)
set_property(TARGET test_importer PROPERTY AUTOMOC ON)

target_link_libraries(test_importer PRIVATE Qt6::Test Qt6::Sql Qt6::Core Qt6::Widgets)

target_include_directories(test_importer PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/../src)

add_test(NAME test_importer COMMAND test_importer)
set_tests_properties(test_importer PROPERTIES WORKING_DIRECTORY "${CMAKE_BINARY_DIR}/install_run/bin")

# Install test executables to bin directory (append)
install(TARGETS test_importer DESTINATION bin)


# Test executable: test_utils
add_executable(test_utils
    test_utils.cpp
    ../src/utils.h
)
set_property(TARGET test_utils PROPERTY AUTOMOC ON)

target_link_libraries(test_utils PRIVATE Qt6::Test Qt6::Core)

target_include_directories(test_utils PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/../src)

add_test(NAME test_utils COMMAND test_utils)
set_tests_properties(test_utils PROPERTIES WORKING_DIRECTORY "${CMAKE_BINARY_DIR}/install_run/bin")

install(TARGETS test_utils DESTINATION bin)

# Test executable: test_live_preview_manager
add_executable(test_live_preview_manager
    test_live_preview_manager.cpp
    ../src/live_preview_manager.cpp
    ../src/live_preview_manager.h
    ../src/image_memory_limits.cpp
    ../src/image_memory_limits.h
    ../src/thumbnail_cache_manager.cpp
    ../src/thumbnail_cache_manager.h
    ../src/oiio_image_loader.cpp
    ../src/oiio_image_loader.h
    ../src/runtime_paths.cpp
    ../src/runtime_paths.h
    ../src/utils.cpp
    ../src/utils.h
)
set_property(TARGET test_live_preview_manager PROPERTY AUTOMOC ON)

# Needs Widgets for QPixmap, and Concurrent for QtConcurrent::run
target_link_libraries(test_live_preview_manager PRIVATE
    Qt6::Test
    Qt6::Core
    Qt6::Widgets
    Qt6::Concurrent
)

target_include_directories(test_live_preview_manager PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/../src
)

add_test(NAME test_live_preview_manager COMMAND test_live_preview_manager)
set_tests_properties(test_live_preview_manager PROPERTIES WORKING_DIRECTORY "${CMAKE_BINARY_DIR}/install_run/bin")


# Ensure external heavy features are disabled for this unit test to avoid extra SDK deps
# Use value-0 so we can leave directory-level defines intact and rely on #if defined(X) && X guards
# This keeps other tests and the main app unaffected
target_compile_definitions(test_live_preview_manager PRIVATE
    HAVE_OPENIMAGEIO=0
    HAVE_FFMPEG=0
    HAVE_TLRENDER=0
)

install(TARGETS test_live_preview_manager DESTINATION bin)

# Test executable: test_media_converter_worker
add_executable(test_media_converter_worker
    test_media_converter_worker.cpp
    ../src/media_converter_worker.cpp
    ../src/media_converter_worker.h
)
set_property(TARGET test_media_converter_worker PROPERTY AUTOMOC ON)

target_link_libraries(test_media_converter_worker PRIVATE Qt6::Test Qt6::Core Qt6::Widgets)

target_include_directories(test_media_converter_worker PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/../src)

add_test(NAME test_media_converter_worker COMMAND test_media_converter_worker)
set_tests_properties(test_media_converter_worker PROPERTIES WORKING_DIRECTORY "${CMAKE_BINARY_DIR}/install_run/bin")

install(TARGETS test_media_converter_worker DESTINATION bin)

# Test executable: test_virtual_drag
add_executable(test_virtual_drag
    test_virtual_drag.cpp
    ../src/virtual_drag.cpp
    ../src/virtual_drag.h
)
set_property(TARGET test_virtual_drag PROPERTY AUTOMOC ON)

target_link_libraries(test_virtual_drag PRIVATE Qt6::Test Qt6::Core Qt6::Gui)

target_include_directories(test_virtual_drag PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/../src)

add_test(NAME test_virtual_drag COMMAND test_virtual_drag)
set_tests_properties(test_virtual_drag PROPERTIES WORKING_DIRECTORY "${CMAKE_BINARY_DIR}/install_run/bin")

install(TARGETS test_virtual_drag DESTINATION bin)

# Test executable: test_drag_utils
add_executable(test_drag_utils
    test_drag_utils.cpp
    ../src/drag_utils.cpp
    ../src/drag_utils.h
)
set_property(TARGET test_drag_utils PROPERTY AUTOMOC ON)

target_link_libraries(test_drag_utils PRIVATE Qt6::Test Qt6::Core Qt6::Gui Qt6::Widgets Qt6::DBus)

target_include_directories(test_drag_utils PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/../src)

add_test(NAME test_drag_utils COMMAND test_drag_utils)
set_tests_properties(test_drag_utils PROPERTIES WORKING_DIRECTORY "${CMAKE_BINARY_DIR}/install_run/bin")

install(TARGETS test_drag_utils DESTINATION bin)

# Test executable: test_everything_search
add_executable(test_everything_search
    test_everything_search.cpp
    ../src/everything_search.cpp
    ../src/everything_search.h
)
set_property(TARGET test_everything_search PROPERTY AUTOMOC ON)

target_link_libraries(test_everything_search PRIVATE Qt6::Test Qt6::Core)

target_include_directories(test_everything_search PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/../src)

add_test(NAME test_everything_search COMMAND test_everything_search)
set_tests_properties(test_everything_search PROPERTIES WORKING_DIRECTORY "${CMAKE_BINARY_DIR}/install_run/bin")

install(TARGETS test_everything_search DESTINATION bin)

# Test executable: test_icon_utils
add_executable(test_icon_utils
    test_icon_utils.cpp
    ../src/icon_utils.cpp
    ../src/icon_utils.h
)

set_property(TARGET test_icon_utils PROPERTY AUTOMOC ON)

target_link_libraries(test_icon_utils PRIVATE Qt6::Test Qt6::Core Qt6::Gui Qt6::Widgets)

target_include_directories(test_icon_utils PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/../src)

add_test(NAME test_icon_utils COMMAND test_icon_utils)
set_tests_properties(test_icon_utils PROPERTIES WORKING_DIRECTORY "${CMAKE_BINARY_DIR}/install_run/bin")

install(TARGETS test_icon_utils DESTINATION bin)

if(TARGET tlRender::tlTimeline)
# Test executable: test_player_lab_playback_harness
add_executable(test_player_lab_playback_harness
    test_player_lab_playback_harness.cpp
    real_media_test_helper.cpp
    ../src/file_utils.cpp
    ../src/file_utils.h
    ../src/media/ffmpeg_mov_player.cpp
    ../src/media/ffmpeg_mov_player.h
    ../src/media/player_lab_player.cpp
    ../src/media/player_lab_player.h
    ../src/platform_session.h
    ../src/media/media_player_types.h
    ../src/media/player_lab_viewport.cpp
    ../src/media/player_lab_viewport.h
 ../player_lab/player_types.h
 ../player_lab/ffmpeg_gpu_decoder.cpp
 ../player_lab/ffmpeg_gpu_decoder.h
 ../player_lab/gpu_player.cpp
 ../player_lab/gpu_player.h
 ../player_lab/gpu_player_widget.cpp
 ../player_lab/gpu_player_widget.h
    ../src/video_metadata.cpp
    ../src/video_metadata.h
)

set_property(TARGET test_player_lab_playback_harness PROPERTY AUTOMOC ON)

target_link_libraries(test_player_lab_playback_harness PRIVATE
    Qt6::Test
    Qt6::Core
    Qt6::Gui
    Qt6::Widgets
    Qt6::Multimedia
    Qt6::MultimediaWidgets
    Qt6::Sql
    Qt6::Concurrent
    Qt6::Svg
    Qt6::SvgWidgets
    Qt6::OpenGL
    Qt6::OpenGLWidgets
    Qt6::Quick
    Qt6::QuickWidgets
    tlRender::tlTimeline
    tlRender::tlGL
    tlRender::tlIO
    tlRender::tlCore
    tlRender::tlUI
    tlRender::tlDevice
    tlRender::tlQt
    tlRender::tlQtWidget
    MINIZIP::minizip-ng
    PkgConfig::FFMPEG

)

if(FFMPEG_LIBRARY_DIRS)
    list(GET FFMPEG_LIBRARY_DIRS 0 _playerlab_ffmpeg_libdir)
    set_property(TARGET test_player_lab_playback_harness APPEND PROPERTY BUILD_RPATH "${_playerlab_ffmpeg_libdir}")
    set_property(TARGET test_player_lab_playback_harness APPEND PROPERTY INSTALL_RPATH "${_playerlab_ffmpeg_libdir}")
endif()

target_include_directories(test_player_lab_playback_harness PRIVATE
 ${CMAKE_CURRENT_SOURCE_DIR}/../src
 ${CMAKE_CURRENT_SOURCE_DIR}/../player_lab
)

target_compile_definitions(test_player_lab_playback_harness PRIVATE
    HAVE_TLRENDER=1
    HAVE_OPENIMAGEIO=1
    HAVE_FFMPEG
)

if(TLRENDER_FOUND)
    if(TLRENDER_INCLUDE_DIR)
        target_include_directories(test_player_lab_playback_harness PRIVATE ${TLRENDER_INCLUDE_DIR})
    elseif(TLRENDER_ROOT)
        target_include_directories(test_player_lab_playback_harness PRIVATE "${TLRENDER_ROOT}/include")
    endif()
endif()

add_test(NAME test_player_lab_playback_harness COMMAND test_player_lab_playback_harness)
set_tests_properties(test_player_lab_playback_harness PROPERTIES WORKING_DIRECTORY "${CMAKE_BINARY_DIR}/install_run/bin")

install(TARGETS test_player_lab_playback_harness DESTINATION bin)
endif()

get_filename_component(KASSET_REPO_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/../../.." ABSOLUTE)
set(KASSET_MDK_SDK_DIR "${KASSET_REPO_ROOT}/third_party/mdk-sdk")
set(KASSET_MDK_CMAKE "${KASSET_MDK_SDK_DIR}/lib/cmake/FindMDK.cmake")
if(EXISTS "${KASSET_MDK_CMAKE}")
    include("${KASSET_MDK_CMAKE}")

    add_executable(test_mdk_embedded_poc
        test_mdk_embedded_poc.cpp
    )

    set_property(TARGET test_mdk_embedded_poc PROPERTY AUTOMOC ON)

    target_link_libraries(test_mdk_embedded_poc PRIVATE
        Qt6::Test
        Qt6::Core
        Qt6::Gui
        Qt6::Widgets
        Qt6::OpenGL
        Qt6::OpenGLWidgets
        mdk
    )

    set_target_properties(test_mdk_embedded_poc PROPERTIES
        BUILD_RPATH "${KASSET_MDK_SDK_DIR}/lib/amd64"
        INSTALL_RPATH "$ORIGIN;${KASSET_MDK_SDK_DIR}/lib/amd64"
    )

    add_test(NAME test_mdk_embedded_poc COMMAND test_mdk_embedded_poc)
    set_tests_properties(test_mdk_embedded_poc PROPERTIES WORKING_DIRECTORY "${CMAKE_BINARY_DIR}/install_run/bin")
    install(TARGETS test_mdk_embedded_poc DESTINATION bin)
else()
    message(STATUS "MDK embedded PoC disabled: ${KASSET_MDK_CMAKE} was not found")
endif()

if(LIBMPV_FOUND)
    add_executable(test_libmpv_embedded_poc
        test_libmpv_embedded_poc.cpp
    )

    set_property(TARGET test_libmpv_embedded_poc PROPERTY AUTOMOC ON)

    target_link_libraries(test_libmpv_embedded_poc PRIVATE
        Qt6::Test
        Qt6::Core
        Qt6::Gui
        Qt6::Widgets
        Qt6::OpenGL
        Qt6::OpenGLWidgets
        PkgConfig::LIBMPV
    )

    target_include_directories(test_libmpv_embedded_poc PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/../src
    )

    add_test(NAME test_libmpv_embedded_poc COMMAND test_libmpv_embedded_poc)
    set_tests_properties(test_libmpv_embedded_poc PROPERTIES WORKING_DIRECTORY "${CMAKE_BINARY_DIR}/install_run/bin")
    install(TARGETS test_libmpv_embedded_poc DESTINATION bin)
else()
    message(STATUS "libmpv embedded PoC disabled: pkg-config package 'mpv' was not found")
endif()

# Test executable: test_grid_scrub
add_executable(test_grid_scrub
    test_grid_scrub.cpp
)
set_property(TARGET test_grid_scrub PROPERTY AUTOMOC ON)

target_link_libraries(test_grid_scrub PRIVATE Qt6::Test Qt6::Core Qt6::Widgets)

target_include_directories(test_grid_scrub PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/../src)

add_test(NAME test_grid_scrub COMMAND test_grid_scrub)
set_tests_properties(test_grid_scrub PROPERTIES WORKING_DIRECTORY "${CMAKE_BINARY_DIR}/install_run/bin")

install(TARGETS test_grid_scrub DESTINATION bin)

# Test executable: test_platform_session
add_executable(test_platform_session
    test_platform_session.cpp
)
set_property(TARGET test_platform_session PROPERTY AUTOMOC ON)

target_link_libraries(test_platform_session PRIVATE Qt6::Test Qt6::Core)

target_include_directories(test_platform_session PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/../src)

add_test(NAME test_platform_session COMMAND test_platform_session)
set_tests_properties(test_platform_session PROPERTIES WORKING_DIRECTORY "${CMAKE_BINARY_DIR}/install_run/bin")

install(TARGETS test_platform_session DESTINATION bin)

# Test executable: test_annotation_items
add_executable(test_annotation_items
    test_annotation_items.cpp
    ../src/annotation_items.cpp
    ../src/annotation_items.h
)

set_property(TARGET test_annotation_items PROPERTY AUTOMOC ON)

target_link_libraries(test_annotation_items PRIVATE Qt6::Test Qt6::Core Qt6::Gui Qt6::Widgets)

target_include_directories(test_annotation_items PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/../src)

add_test(NAME test_annotation_items COMMAND test_annotation_items)
set_tests_properties(test_annotation_items PROPERTIES WORKING_DIRECTORY "${CMAKE_BINARY_DIR}/install_run/bin")

install(TARGETS test_annotation_items DESTINATION bin)
# Test executable: test_file_utils
add_executable(test_file_utils
    test_file_utils.cpp
    ../src/file_utils.cpp
    ../src/file_utils.h
)
set_property(TARGET test_file_utils PROPERTY AUTOMOC ON)

target_link_libraries(test_file_utils PRIVATE Qt6::Test Qt6::Core)

target_include_directories(test_file_utils PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/../src)

add_test(NAME test_file_utils COMMAND test_file_utils)
set_tests_properties(test_file_utils PROPERTIES WORKING_DIRECTORY "${CMAKE_BINARY_DIR}/install_run/bin")

install(TARGETS test_file_utils DESTINATION bin)
