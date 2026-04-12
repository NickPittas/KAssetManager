
####### Expanded from @PACKAGE_INIT@ by configure_package_config_file() #######
####### Any changes to this file will be overwritten by the next CMake run ####
####### The input file was tlRenderConfig.cmake.in                            ########

get_filename_component(PACKAGE_PREFIX_DIR "${CMAKE_CURRENT_LIST_DIR}/../../../" ABSOLUTE)

macro(set_and_check _var _file)
  set(${_var} "${_file}")
  if(NOT EXISTS "${_file}")
    message(FATAL_ERROR "File or directory ${_file} referenced by variable ${_var} does not exist !")
  endif()
endmacro()

macro(check_required_components _NAME)
  foreach(comp ${${_NAME}_FIND_COMPONENTS})
    if(NOT ${_NAME}_${comp}_FOUND)
      if(${_NAME}_FIND_REQUIRED_${comp})
        set(${_NAME}_FOUND FALSE)
      endif()
    endif()
  endforeach()
endmacro()

####################################################################################

# \todo Is this the correct way to locate the custom Find*.cmake files?
list(APPEND CMAKE_MODULE_PATH ${CMAKE_CURRENT_LIST_DIR})

include(CMakeFindDependencyMacro)

# Base dependencies
find_package(Imath REQUIRED)
find_package(nlohmann_json REQUIRED)
find_package(ZLIB REQUIRED)
find_package(minizip-ng REQUIRED)
find_package(OpenTimelineIO REQUIRED)

# OpenColorIO dependencies
if(ON)
    find_package(OpenColorIO REQUIRED)
    add_definitions(-DTLRENDER_OCIO)
endif()

# SDL dependencies
if(ON)
    find_package(SDL2 REQUIRED)
    add_definitions(-DTLRENDER_SDL2)
endif()
if(OFF)
    find_package(SDL3 REQUIRED)
    add_definitions(-DTLRENDER_SDL3)
endif()

# I/O dependencies
if(ON)
    find_package(libjpeg-turbo)
    if(libjpeg-turbo_FOUND)
        add_definitions(-DTLRENDER_JPEG)
    endif()
endif()
if(ON)
    find_package(TIFF)
    if(TIFF_FOUND)
        add_definitions(-DTLRENDER_TIFF)
    endif()
endif()
if(ON)
    find_package(PNG)
    if(PNG_FOUND)
        add_definitions(-DTLRENDER_PNG)
    endif()
endif()
if(ON)
    find_package(OpenEXR)
    if(OpenEXR_FOUND)
        add_definitions(-DTLRENDER_EXR)
    endif()
endif()
if(ON)
    if(WIN32)
        set(FFmpeg_FOUND TRUE)
        add_library(FFmpeg::avdevice STATIC IMPORTED)
        set_property(TARGET FFmpeg::avdevice PROPERTY
             IMPORTED_LOCATION "${CMAKE_INSTALL_PREFIX}/lib/avdevice.lib")
        add_library(FFmpeg::avformat STATIC IMPORTED)
        set_property(TARGET FFmpeg::avformat PROPERTY
             IMPORTED_LOCATION "${CMAKE_INSTALL_PREFIX}/lib/avformat.lib")
        add_library(FFmpeg::avcodec STATIC IMPORTED)
        set_property(TARGET FFmpeg::avcodec PROPERTY
             IMPORTED_LOCATION "${CMAKE_INSTALL_PREFIX}/lib/avcodec.lib")
        add_library(FFmpeg::swresample STATIC IMPORTED)
        set_property(TARGET FFmpeg::swresample PROPERTY
             IMPORTED_LOCATION "${CMAKE_INSTALL_PREFIX}/lib/swresample.lib")
        add_library(FFmpeg::swscale STATIC IMPORTED)
        set_property(TARGET FFmpeg::swscale PROPERTY
             IMPORTED_LOCATION "${CMAKE_INSTALL_PREFIX}/lib/swscale.lib")
        add_library(FFmpeg::avutil STATIC IMPORTED)
        set_property(TARGET FFmpeg::avutil PROPERTY
             IMPORTED_LOCATION "${CMAKE_INSTALL_PREFIX}/lib/avutil.lib")
        add_library(FFmpeg::FFmpeg INTERFACE IMPORTED)
        set_property(TARGET FFmpeg::FFmpeg PROPERTY
            INTERFACE_LINK_LIBRARIES
            FFmpeg::avdevice
            FFmpeg::avformat
            FFmpeg::avcodec
            FFmpeg::swresample
            FFmpeg::swscale
            FFmpeg::avutil)
    elseif(APPLE)
        set(FFmpeg_FOUND TRUE)
        add_library(FFmpeg::avdevice STATIC IMPORTED)
        set_property(TARGET FFmpeg::avdevice PROPERTY
             IMPORTED_LOCATION "${CMAKE_INSTALL_PREFIX}/lib/libavdevice.dylib")
        add_library(FFmpeg::avformat STATIC IMPORTED)
        set_property(TARGET FFmpeg::avformat PROPERTY
             IMPORTED_LOCATION "${CMAKE_INSTALL_PREFIX}/lib/libavformat.dylib")
        add_library(FFmpeg::avcodec STATIC IMPORTED)
        set_property(TARGET FFmpeg::avcodec PROPERTY
             IMPORTED_LOCATION "${CMAKE_INSTALL_PREFIX}/lib/libavcodec.dylib")
        add_library(FFmpeg::swresample STATIC IMPORTED)
        set_property(TARGET FFmpeg::swresample PROPERTY
             IMPORTED_LOCATION "${CMAKE_INSTALL_PREFIX}/lib/libswresample.dylib")
        add_library(FFmpeg::swscale STATIC IMPORTED)
        set_property(TARGET FFmpeg::swscale PROPERTY
             IMPORTED_LOCATION "${CMAKE_INSTALL_PREFIX}/lib/libswscale.dylib")
        add_library(FFmpeg::avutil STATIC IMPORTED)
        set_property(TARGET FFmpeg::avutil PROPERTY
             IMPORTED_LOCATION "${CMAKE_INSTALL_PREFIX}/lib/libavutil.dylib")
        add_library(FFmpeg::FFmpeg INTERFACE IMPORTED)
        set_property(TARGET FFmpeg::FFmpeg PROPERTY
            INTERFACE_LINK_LIBRARIES
            FFmpeg::avdevice
            FFmpeg::avformat
            FFmpeg::avcodec
            FFmpeg::swresample
            FFmpeg::swscale
            FFmpeg::avutil)
    else()
        find_package(PkgConfig REQUIRED)
        pkg_check_modules(
            swresample REQUIRED IMPORTED_TARGET
            libswresample)
        pkg_check_modules(
            avutil REQUIRED IMPORTED_TARGET
            libavutil)
        pkg_check_modules(
            FFmpeg REQUIRED IMPORTED_TARGET
            libavdevice
            libavformat
            libavcodec
            libswresample
            libswscale
            libavutil)
        add_library(FFmpeg::FFmpeg ALIAS PkgConfig::FFmpeg)
        add_library(FFmpeg::swresample ALIAS PkgConfig::swresample)
        add_library(FFmpeg::avutil ALIAS PkgConfig::avutil)
    endif()
    if(FFmpeg_FOUND)
        add_definitions(-DTLRENDER_FFMPEG)
        if(ON)
            add_definitions(-DTLRENDER_FFMPEG_PLUGIN)
        endif()
        if(OFF)
            add_definitions(-DTLRENDER_FFMPEG_PIPE)
        endif()
    endif()
endif()
if(ON)
    find_package(OpenImageIO)
    if(OpenImageIO_FOUND)
        add_definitions(-DTLRENDER_OIIO)
    endif()
endif()
if(OFF)
    find_package(pxr)
    if(pxr_FOUND)
        add_definitions(-DTLRENDER_USD)
    endif()
endif()
if(OFF)
    add_definitions(-DTLRENDER_BMD)
endif()

# feather-tk dependencies
find_package(ftk REQUIRED)

# Qt dependencies
if(ON)
    set(CMAKE_AUTOMOC ON)
    set(CMAKE_AUTORCC ON)
    set(CMAKE_AUTOUIC ON)
    find_package(Qt6 REQUIRED COMPONENTS Quick Widgets OpenGLWidgets Svg HINTS "$ENV{QTDIR}")
endif()

include("${CMAKE_CURRENT_LIST_DIR}/tlResourceTargets.cmake")
include("${CMAKE_CURRENT_LIST_DIR}/tlCoreTargets.cmake")
include("${CMAKE_CURRENT_LIST_DIR}/tlIOTargets.cmake")
include("${CMAKE_CURRENT_LIST_DIR}/tlTimelineTargets.cmake")
include("${CMAKE_CURRENT_LIST_DIR}/tlGLTargets.cmake")
include("${CMAKE_CURRENT_LIST_DIR}/tlDeviceTargets.cmake")
include("${CMAKE_CURRENT_LIST_DIR}/tlUITargets.cmake")
if(ON)
    include("${CMAKE_CURRENT_LIST_DIR}/tlQtTargets.cmake")
    include("${CMAKE_CURRENT_LIST_DIR}/tlQtWidgetTargets.cmake")
    include("${CMAKE_CURRENT_LIST_DIR}/tlQtQuickTargets.cmake")
endif()

# \todo Is this the correct way to add the include directory?
include_directories("${PACKAGE_PREFIX_DIR}/include/tlRender")

check_required_components(tlRender)
