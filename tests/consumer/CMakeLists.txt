cmake_minimum_required(VERSION 3.31)

project(orm-cxx-consumer LANGUAGES CXX)

if(NOT ORM_CXX_SOURCE_DIR)
    message(FATAL_ERROR "ORM_CXX_SOURCE_DIR must point to the orm-cxx checkout")
endif()

add_subdirectory("${ORM_CXX_SOURCE_DIR}" orm-cxx)

if(ORM_CXX_BUILD_TESTS OR ORM_CXX_BUILD_EXAMPLES)
    message(FATAL_ERROR "Developer-only orm-cxx targets must default to OFF in a parent project")
endif()

add_executable(orm-cxx-consumer main.cpp ModelIdentity.cpp)
target_sources(orm-cxx-consumer PRIVATE FILE_SET consumer_modules TYPE CXX_MODULES FILES Models.cppm)
set_target_properties(
    orm-cxx-consumer PROPERTIES RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin" CXX_SCAN_FOR_MODULES ON
)
target_link_libraries(orm-cxx-consumer PRIVATE orm-cxx::orm-cxx)
if(MSVC)
    target_compile_options(orm-cxx-consumer PRIVATE /W4 /WX)
else()
    target_compile_options(orm-cxx-consumer PRIVATE -Wall -Wextra -Wpedantic -Werror)
endif()
