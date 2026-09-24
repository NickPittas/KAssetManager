# Standalone CMake entry point for the GPU player lab (Slice 1).
#
# This target is intentionally independent of the main KAssetManagerQt /
# tlRender build. It only needs Qt6 (Core/Gui/Widgets/OpenGL/OpenGLWidgets)
# and FFmpeg (avcodec/avformat/avutil/swscale), discovered via CMake's
# find_package and pkg-config respectively.
#
# Configure and build on its own:
#   cmake -B build-player_lab -S native/qt6/player_lab
#   cmake --build build-player_lab -j
#   ./build-player_lab/gpu_player_demo <file.mp4>

cmake_minimum_required(VERSION 3.21)

project(gpu_player_lab LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC OFF)

# --- Qt6 ---
# Slice 2: Multimedia is required for QAudioSink (audio playback).
find_package(Qt6 REQUIRED COMPONENTS Core Gui Widgets OpenGL OpenGLWidgets Multimedia)

# --- FFmpeg via pkg-config ---
find_package(PkgConfig REQUIRED)
pkg_check_modules(FFMPEG REQUIRED IMPORTED_TARGET
    libavcodec libavformat libavutil libswscale libswresample)

qt_add_executable(gpu_player_demo
    gpu_player_demo.cpp
    gpu_player.h
    gpu_player.cpp
    gpu_player_widget.h
    gpu_player_widget.cpp
    ffmpeg_gpu_decoder.h
    ffmpeg_gpu_decoder.cpp
    player_types.h
)

target_link_libraries(gpu_player_demo PRIVATE
    Qt6::Core
    Qt6::Gui
    Qt6::Widgets
    Qt6::OpenGL
    Qt6::OpenGLWidgets
    Qt6::Multimedia
    PkgConfig::FFMPEG
)

qt_add_executable(gpu_player_autotest
    gpu_player_autotest.cpp
    gpu_player.h
    gpu_player.cpp
    ffmpeg_gpu_decoder.h
    ffmpeg_gpu_decoder.cpp
    player_types.h
)

target_link_libraries(gpu_player_autotest PRIVATE
    Qt6::Core
    PkgConfig::FFMPEG
)

# Headless CI / X11-Wayland: don't die if there's no display during build.
# (The binary still needs a display at runtime.)
