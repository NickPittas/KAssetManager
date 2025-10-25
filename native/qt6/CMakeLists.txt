cmake_minimum_required(VERSION 3.21)

project(KAssetManagerQt VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

find_package(QT NAMES Qt6 REQUIRED COMPONENTS Widgets Multimedia Sql)
find_package(Qt6 REQUIRED COMPONENTS Widgets Multimedia Sql)

# Standard Qt project setup (Qt6)
if (COMMAND qt_standard_project_setup)
  qt_standard_project_setup()
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
    src/importer.h
    src/importer.cpp
    src/thumbnail_generator.h
    src/thumbnail_generator.cpp
    src/log_manager.h
    src/log_manager.cpp
    src/progress_manager.h
    src/progress_manager.cpp
    src/tags_model.h
)

target_include_directories(kassetmanagerqt PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/src
)

target_link_libraries(kassetmanagerqt PRIVATE Qt6::Widgets Qt6::Multimedia Qt6::Sql)

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


