cmake_minimum_required(VERSION 3.21)

project(KAssetManagerQt VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

find_package(QT NAMES Qt6 REQUIRED COMPONENTS Quick Multimedia Sql)
find_package(Qt6 REQUIRED COMPONENTS Quick Multimedia Sql)

# Standard Qt project setup (Qt6)
if (COMMAND qt_standard_project_setup)
  qt_standard_project_setup()
endif()

# Silence QML policy warnings (Qt 6)
if (COMMAND qt_policy)
  qt_policy(SET QTP0001 NEW)
  qt_policy(SET QTP0004 NEW)
endif()

qt_add_executable(kassetmanagerqt
    src/main.cpp
    src/virtual_drag.cpp
    src/virtual_drag.h
    src/db.cpp
    src/db.h
)

target_include_directories(kassetmanagerqt PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/src
)

target_link_libraries(kassetmanagerqt PRIVATE Qt6::Quick Qt6::Multimedia Qt6::Sql)

# QML module (embeds qml/Main.qml into resources)
qt_add_qml_module(kassetmanagerqt
    URI KAssetManager
    VERSION 1.0
    QML_FILES
        qml/Main.qml
    SOURCES
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
    RESOURCE_PREFIX /qt/qml
    OUTPUT_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}/KAssetManager
)

# Add AppState as a singleton
set_source_files_properties(qml/AppState.qml PROPERTIES
    QT_QML_SINGLETON_TYPE TRUE
)
qt_target_qml_sources(kassetmanagerqt
    QML_FILES qml/AppState.qml
)

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
    # Fallback to manual windeployqt helper with absolute include path, but runtime install vars
    set(_DEPLOY_QT_CMAKE "${CMAKE_CURRENT_SOURCE_DIR}/cmake/DeployQt.cmake")
    set(_QML_DIR_ABS     "${CMAKE_CURRENT_SOURCE_DIR}/qml")
    install(CODE "\ninclude(\"${_DEPLOY_QT_CMAKE}\")\nset(_exe \"\${CMAKE_INSTALL_PREFIX}/bin/kassetmanagerqt.exe\")\nset(_qml \"${_QML_DIR_ABS}\")\nmessage(STATUS \"Invoking windeployqt for \${_exe}\")\nqt_deploy(\"\${_exe}\" \"\${CMAKE_INSTALL_PREFIX}\" \"\${_qml}\")\n")
  endif()
endif()

# CPack configuration (NSIS + ZIP)
set(CPACK_PACKAGE_NAME "KAsset Manager Qt")
set(CPACK_PACKAGE_VENDOR "KAM")
set(CPACK_PACKAGE_VERSION ${PROJECT_VERSION})
set(CPACK_PACKAGE_INSTALL_DIRECTORY "KAssetManagerQt")
set(CPACK_GENERATOR "ZIP;NSIS")
include(CPack)


