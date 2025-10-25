# DeployQt.cmake - helper to run windeployqt during install or on demand

function(qt_find_windeploy OUT_VAR)
    # Try CMake prefix first
    set(_HINTS)
    if(DEFINED CMAKE_PREFIX_PATH)
        foreach(_p IN LISTS CMAKE_PREFIX_PATH)
            list(APPEND _HINTS "${_p}/bin")
        endforeach()
    endif()
    # Environment variables
    if(DEFINED ENV{CMAKE_PREFIX_PATH})
        list(APPEND _HINTS "$ENV{CMAKE_PREFIX_PATH}/bin")
    endif()
    if(DEFINED ENV{Qt6_DIR})
        list(APPEND _HINTS "$ENV{Qt6_DIR}/bin")
    endif()
    if(DEFINED ENV{QT_DIR})
        list(APPEND _HINTS "$ENV{QT_DIR}/bin")
    endif()
    if(DEFINED ENV{Qt6})
        list(APPEND _HINTS "$ENV{Qt6}/bin")
    endif()
    # Common Qt install locations
    list(APPEND _HINTS "C:/Qt/6.*/msvc*/bin" "C:/Qt/6.*/mingw*/bin")

    find_program(WINDEPLOYQT_EXECUTABLE NAMES windeployqt windeployqt6 HINTS ${_HINTS} NO_DEFAULT_PATH)
    if(NOT WINDEPLOYQT_EXECUTABLE)
        find_program(WINDEPLOYQT_EXECUTABLE NAMES windeployqt windeployqt6)
    endif()
    if(NOT WINDEPLOYQT_EXECUTABLE)
        message(FATAL_ERROR "windeployqt not found. Ensure Qt bin is in PATH or set CMAKE_PREFIX_PATH/Qt6_DIR.")
    endif()
    set(${OUT_VAR} ${WINDEPLOYQT_EXECUTABLE} PARENT_SCOPE)
endfunction()

# qt_deploy(exe_path install_root qml_dir)
function(qt_deploy EXE_PATH INSTALL_ROOT QML_DIR)
    if(NOT WIN32)
        message(STATUS "qt_deploy skipped (not Windows)")
        return()
    endif()
    qt_find_windeploy(WINDEPLOYQT)
    message(STATUS "windeployqt: ${WINDEPLOYQT}")
    message(STATUS "Deploying Qt runtime for: ${EXE_PATH}")

    get_filename_component(_EXE_DIR "${EXE_PATH}" DIRECTORY)
    # Deploy into the exe directory so DLLs sit next to the binary
    execute_process(
        COMMAND "${WINDEPLOYQT}" "${EXE_PATH}" --qmldir "${QML_DIR}" --dir "${_EXE_DIR}"
        RESULT_VARIABLE _res
        OUTPUT_VARIABLE _out
        ERROR_VARIABLE _err
    )
    if(NOT _res EQUAL 0)
        message(FATAL_ERROR "windeployqt failed (code ${_res})\n${_out}\n${_err}")
    endif()
endfunction()

