cmake_minimum_required(VERSION 3.22)

project(orm-cxx-consumer LANGUAGES CXX)

if(NOT ORM_CXX_SOURCE_DIR)
    message(FATAL_ERROR "ORM_CXX_SOURCE_DIR must point to the orm-cxx checkout")
endif()

add_subdirectory("${ORM_CXX_SOURCE_DIR}" orm-cxx)

if(ORM_CXX_BUILD_TESTS OR ORM_CXX_BUILD_EXAMPLES)
    message(FATAL_ERROR "Developer-only orm-cxx targets must default to OFF in a parent project")
endif()

add_executable(orm-cxx-consumer main.cpp)
set_target_properties(orm-cxx-consumer PROPERTIES RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin")
target_link_libraries(orm-cxx-consumer PRIVATE orm-cxx::orm-cxx)
