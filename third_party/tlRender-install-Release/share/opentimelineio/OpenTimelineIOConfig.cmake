
####### Expanded from @PACKAGE_INIT@ by configure_package_config_file() #######
####### Any changes to this file will be overwritten by the next CMake run ####
####### The input file was OpenTimelineIOConfig.cmake.in                            ########

get_filename_component(PACKAGE_PREFIX_DIR "${CMAKE_CURRENT_LIST_DIR}/../../" ABSOLUTE)

####################################################################################

include(CMakeFindDependencyMacro)
find_dependency(OpenTime)
find_dependency(Imath)

include("${CMAKE_CURRENT_LIST_DIR}/OpenTimelineIOTargets.cmake")
