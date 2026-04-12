cmake_minimum_required(VERSION 3.21)
project(wayland_probe LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

find_package(Qt6 REQUIRED COMPONENTS Widgets OpenGLWidgets)

add_executable(wayland_probe ../wayland_probe.cpp)
target_link_libraries(wayland_probe PRIVATE Qt6::Widgets Qt6::OpenGLWidgets)
