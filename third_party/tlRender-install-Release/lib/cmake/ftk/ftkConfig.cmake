
####### Expanded from @PACKAGE_INIT@ by configure_package_config_file() #######
####### Any changes to this file will be overwritten by the next CMake run ####
####### The input file was ftkConfig.cmake.in                            ########

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
find_dependency(Threads)
find_dependency(ZLIB)
find_dependency(nlohmann_json)
find_dependency(PNG)
find_dependency(Freetype)
find_dependency(lunasvg)

if(GL_4_1 STREQUAL "GL_4_1" OR
    GL_4_1 STREQUAL "GL_4_1_Debug" OR
    GL_4_1 STREQUAL "GLES_2")
    find_dependency(OpenGL)
endif()
set(ftk_API GL_4_1)
add_definitions(-DFTK_API_GL_4_1)
if(GL_4_1 STREQUAL "GL_4_1_Debug")
    add_definitions(-DFTK_API_GL_4_1)
endif()

if(ON)
    find_dependency(SDL2)
elseif(OFF)
    find_dependency(SDL3)
endif()

if(OFF)
    find_dependency(nfd)
    if(nfd_FOUND)
        add_definitions(-DFTK_NFD)
    endif()
endif()

if(OFF)
    add_definitions(-DFTK_PYTHON)
    find_dependency(Python3)
    find_dependency(pybind11)
endif()

include("${CMAKE_CURRENT_LIST_DIR}/ftk-resourceTargets.cmake")
include("${CMAKE_CURRENT_LIST_DIR}/ftkResourceTargets.cmake")
include("${CMAKE_CURRENT_LIST_DIR}/ftkCoreTargets.cmake")
if(GL_4_1 STREQUAL "GL_4_1" OR
    GL_4_1 STREQUAL "GL_4_1_Debug" OR
    GL_4_1 STREQUAL "GLES_2")
    include("${CMAKE_CURRENT_LIST_DIR}/ftkgladTargets.cmake")
    include("${CMAKE_CURRENT_LIST_DIR}/ftkGLTargets.cmake")
endif()
include("${CMAKE_CURRENT_LIST_DIR}/ftkUITargets.cmake")

# \todo Is this the correct way to add the include directory?
include_directories("${PACKAGE_PREFIX_DIR}/include")

check_required_components(ftk)
