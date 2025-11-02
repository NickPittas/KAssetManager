find_package(Qt6 REQUIRED COMPONENTS Test)

# Test executable: test_simple
add_executable(test_simple
    test_simple.cpp
)

target_link_libraries(test_simple PRIVATE Qt6::Test)
target_include_directories(test_simple PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/../src)

add_test(NAME test_simple COMMAND test_simple)
set_tests_properties(test_simple PROPERTIES WORKING_DIRECTORY "${CMAKE_BINARY_DIR}/install_run/bin")

# Test executable: test_db
add_executable(test_db
    test_db.cpp
    ../src/db.cpp
    ../src/db.h
    ../src/log_manager.cpp
    ../src/log_manager.h
)

target_link_libraries(test_db PRIVATE Qt6::Test Qt6::Sql Qt6::Core)
target_include_directories(test_db PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/../src)

add_test(NAME test_db COMMAND test_db)

# Test executable: test_models
add_executable(test_models
    test_models.cpp
    ../src/assets_model.cpp
    ../src/assets_model.h
    ../src/db.cpp
    ../src/db.h
    ../src/log_manager.cpp
    ../src/log_manager.h
    ../src/progress_manager.cpp
    ../src/progress_manager.h
    ../src/virtual_folders.cpp
    ../src/virtual_folders.h
)

target_link_libraries(test_models PRIVATE Qt6::Test Qt6::Sql Qt6::Core Qt6::Gui)
target_include_directories(test_models PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/../src)

add_test(NAME test_models COMMAND test_models)

# Install test executables to bin directory
install(TARGETS test_simple test_db test_models DESTINATION bin)
