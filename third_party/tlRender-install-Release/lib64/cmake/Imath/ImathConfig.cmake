# SPDX-License-Identifier: BSD-3-Clause
# Copyright Contributors to the OpenEXR Project.


####### Expanded from @PACKAGE_INIT@ by configure_package_config_file() #######
####### Any changes to this file will be overwritten by the next CMake run ####
####### The input file was ImathConfig.cmake.in                            ########

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

# Set PACKAGE_PREFIX_DIR to the framework root (Resources/..) for framework builds
get_filename_component(PACKAGE_PREFIX_DIR "${CMAKE_CURRENT_LIST_DIR}/.." ABSOLUTE)

include("${CMAKE_CURRENT_LIST_DIR}/ImathTargets.cmake")

if (NOT TARGET Imath::Config)
  add_library(Imath::Config INTERFACE IMPORTED)
  set_target_properties(Imath::Config PROPERTIES
    INTERFACE_LINK_LIBRARIES ImathConfig
  )
endif()

if(Imath_FIND_COMPONENTS)
  foreach(component IN LISTS Imath_FIND_COMPONENTS)
    if(component STREQUAL "PyImath")
      include("${CMAKE_CURRENT_LIST_DIR}/PyImathTargets.cmake")
    endif()
    if(component STREQUAL "PyBindImath")
      include("${CMAKE_CURRENT_LIST_DIR}/PyBindImathTargets.cmake")
    endif()
  endforeach()
endif()
