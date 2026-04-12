# Copyright Contributors to the OpenImageIO project.
# SPDX-License-Identifier: Apache-2.0
# https://github.com/AcademySoftwareFoundation/OpenImageIO


####### Expanded from @PACKAGE_INIT@ by configure_package_config_file() #######
####### Any changes to this file will be overwritten by the next CMake run ####
####### The input file was Config.cmake.in                            ########

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

include(CMakeFindDependencyMacro)

# add here all the find_dependency() whenever switching to config based dependencies
if (NOT OFF AND NOT OPENIMAGEIO_CONFIG_DO_NOT_FIND_IMATH)
    find_dependency(Imath 3.2.1
                    HINTS /home/npittas/KAssetManager/.worktrees/linux-port-fedora43-wt/third_party/tlRender-install-Release/lib64/cmake/Imath)
endif ()

if (NOT FALSE AND NOT ON)
    find_dependency(fmt)
endif ()

if (NOT OFF)
    # This is required in static library builds, as e.g. PNG::PNG appears among
    # INTERFACE_LINK_LIBRARIES. If the project does not know about PNG target, it will cause
    # configuration error about unknown targets being linked in.
    find_dependency(TIFF)
    find_dependency(OpenColorIO)
    if (TRUE)
        find_dependency(JPEG)
    endif()
    if (TRUE)
        find_dependency(PNG)
    endif()
    if (FALSE)
        find_dependency(DCMTK)
    endif()
    # The following have the same problem except that INTERFACE_LINK_LIBRARIES use
    # TARGET_NAME_IF_EXISTS, so the error only happens on link time.
    if ()
        find_dependency(TBB)
    endif ()
endif ()

# Compute the installation prefix relative to this file. Note that cmake files are installed
# to ${CMAKE_INSTALL_LIBDIR}/cmake/${PROJECT_NAME} (see OIIO_CONFIG_INSTALL_DIR)
get_filename_component(_CURR_INSTALL_LIBDIR "${CMAKE_CURRENT_LIST_DIR}/../../" ABSOLUTE)
get_filename_component(_ABS_CMAKE_INSTALL_LIBDIR "/home/npittas/KAssetManager/.worktrees/linux-port-fedora43-wt/third_party/tlRender-install-Release/lib64" ABSOLUTE)
get_filename_component(_ABS_CMAKE_INSTALL_INCLUDEDIR "/home/npittas/KAssetManager/.worktrees/linux-port-fedora43-wt/third_party/tlRender-install-Release/include" ABSOLUTE)
file(RELATIVE_PATH _INCLUDEDIR_RELATIVE_TO_LIBDIR
     "${_ABS_CMAKE_INSTALL_LIBDIR}" "${_ABS_CMAKE_INSTALL_INCLUDEDIR}")
get_filename_component(_CURR_INSTALL_INCLUDE_DIR
                       "${_CURR_INSTALL_LIBDIR}/${_INCLUDEDIR_RELATIVE_TO_LIBDIR}" ABSOLUTE)

set_and_check (OpenImageIO_INCLUDE_DIR "${_CURR_INSTALL_INCLUDE_DIR}")
set_and_check (OpenImageIO_INCLUDES    "${_CURR_INSTALL_INCLUDE_DIR}")
set_and_check (OpenImageIO_LIB_DIR     "${_CURR_INSTALL_LIBDIR}")
set (OpenImageIO_PLUGIN_SEARCH_PATH    "")

if (NOT 1)
    list (APPEND OpenImageIO_INCLUDES ${IMATH_INCLUDES} ${OPENEXR_INCLUDES})
endif ()

set (OIIO_USING_IMATH_VERSION_MAJOR 3)
set (OIIO_USING_IMATH_VERSION_MINOR 2)

#...logic to determine installedPrefix from the own location...
#set (OpenImageIO_CONFIG_DIR  "${installedPrefix}/")

include ("${CMAKE_CURRENT_LIST_DIR}/OpenImageIOTargets.cmake")

check_required_components ("OpenImageIO")

# Set a CMake variable that says if this OpenImageIO build has OCIO support
set (OpenImageIO_HAS_OpenColorIO 1)
