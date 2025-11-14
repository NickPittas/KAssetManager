find_package(Qt6 REQUIRED COMPONENTS Test)

# Test executable: test_simple
add_executable(test_simple
    test_simple.cpp
)

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
)

target_link_libraries(test_db PRIVATE Qt6::Test Qt6::Sql Qt6::Core)
target_include_directories(test_db PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/../src)

add_test(NAME test_db COMMAND test_db)

# Test executable: test_models
add_executable(test_models
    test_models.cpp
    ../src/assets_model.cpp
    ../src/assets_model.h
    ../src/db.cpp
    ../src/db.h
    ../src/log_manager.cpp
    ../src/log_manager.h
)

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

target_link_libraries(test_sequence_detector PRIVATE Qt6::Test Qt6::Core)

target_include_directories(test_sequence_detector PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/../src)

add_test(NAME test_sequence_detector COMMAND test_sequence_detector)
set_tests_properties(test_sequence_detector PROPERTIES WORKING_DIRECTORY "${CMAKE_BINARY_DIR}/install_run/bin")

# Install test executables to bin directory
install(TARGETS test_simple test_db test_models test_sequence_detector DESTINATION bin)


# Test executable: test_importer
add_executable(test_importer
    test_importer.cpp
    ../src/importer.cpp
    ../src/importer.h
    ../src/db.cpp
    ../src/db.h
    ../src/log_manager.cpp
    ../src/log_manager.h
    ../src/sequence_detector.cpp
    ../src/sequence_detector.h
    ../src/file_utils.h
)

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
    ../src/media/gstreamer_player.cpp
    ../src/media/gstreamer_player.h
    ../src/oiio_image_loader.cpp
    ../src/oiio_image_loader.h
    ../src/utils.cpp
    ../src/utils.h
)

# Needs Widgets for QPixmap, and Concurrent for QtConcurrent::run
# Find GStreamer libraries for linking
find_library(GSTREAMER_LIB gstreamer-1.0 HINTS ${GSTREAMER_LIBRARY_DIRS})
find_library(GSTREAMER_VIDEO_LIB gstvideo-1.0 HINTS ${GSTREAMER_LIBRARY_DIRS})
find_library(GSTREAMER_AUDIO_LIB gstaudio-1.0 HINTS ${GSTREAMER_LIBRARY_DIRS})
find_library(GSTREAMER_APP_LIB gstapp-1.0 HINTS ${GSTREAMER_LIBRARY_DIRS})
find_library(GSTREAMER_BASE_LIB gstbase-1.0 HINTS ${GSTREAMER_LIBRARY_DIRS})
find_library(GLIB_LIB glib-2.0 HINTS ${GSTREAMER_LIBRARY_DIRS})
find_library(GOBJECT_LIB gobject-2.0 HINTS ${GSTREAMER_LIBRARY_DIRS})

target_link_libraries(test_live_preview_manager PRIVATE
    Qt6::Test
    Qt6::Core
    Qt6::Widgets
    Qt6::Concurrent
    ${GSTREAMER_LIB}
    ${GSTREAMER_VIDEO_LIB}
    ${GSTREAMER_AUDIO_LIB}
    ${GSTREAMER_APP_LIB}
    ${GSTREAMER_BASE_LIB}
    ${GLIB_LIB}
    ${GOBJECT_LIB}
)

target_include_directories(test_live_preview_manager PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/../src
    ${GSTREAMER_INCLUDE_DIRS}
)

add_test(NAME test_live_preview_manager COMMAND test_live_preview_manager)
set_tests_properties(test_live_preview_manager PROPERTIES WORKING_DIRECTORY "${CMAKE_BINARY_DIR}/install_run/bin")


# Ensure external heavy features are disabled for this unit test to avoid extra SDK deps
# Use value-0 so we can leave directory-level defines intact and rely on #if defined(X) && X guards
# This keeps other tests and the main app unaffected
target_compile_definitions(test_live_preview_manager PRIVATE
    HAVE_OPENIMAGEIO=0
    HAVE_FFMPEG=0
    HAVE_GSTREAMER=1
)

install(TARGETS test_live_preview_manager DESTINATION bin)

# Test executable: test_media_converter_worker
add_executable(test_media_converter_worker
    test_media_converter_worker.cpp
    ../src/media_converter_worker.cpp
    ../src/media_converter_worker.h
)

target_link_libraries(test_media_converter_worker PRIVATE Qt6::Test Qt6::Core Qt6::Widgets)

target_include_directories(test_media_converter_worker PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/../src)

add_test(NAME test_media_converter_worker COMMAND test_media_converter_worker)
set_tests_properties(test_media_converter_worker PROPERTIES WORKING_DIRECTORY "${CMAKE_BINARY_DIR}/install_run/bin")

install(TARGETS test_media_converter_worker DESTINATION bin)
