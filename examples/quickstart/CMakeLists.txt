cmake_minimum_required(VERSION 3.31)
project(orm-cxx-quickstart LANGUAGES CXX)

set(ORM_CXX_BUILD_TESTS
    OFF
    CACHE BOOL "Build orm-cxx tests"
)
set(ORM_CXX_BUILD_EXAMPLES
    OFF
    CACHE BOOL "Build orm-cxx examples"
)
set(ORM_CXX_ENABLE_SQLITE_BACKEND
    ON
    CACHE BOOL "Build SQLite backend"
)
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin")

add_subdirectory(../.. orm-cxx)

add_executable(quickstart main.cpp)
set_target_properties(quickstart PROPERTIES CXX_SCAN_FOR_MODULES ON)
target_link_libraries(quickstart PRIVATE orm-cxx::orm-cxx)

enable_testing()
add_test(NAME quickstart COMMAND quickstart)
file(READ "${CMAKE_CURRENT_SOURCE_DIR}/expected.txt" quickstart_expected_output)
string(REPLACE "\r" "" quickstart_expected_output "${quickstart_expected_output}")
set_tests_properties(quickstart PROPERTIES PASS_REGULAR_EXPRESSION "${quickstart_expected_output}")
