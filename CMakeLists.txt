cmake_minimum_required(VERSION 3.22)

set(PROJECT_NAME orm-cxx)
project(${PROJECT_NAME} CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED YES)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

option(BUILD_ORM_CXX_TESTS DEFAULT ON)

if (MSVC)
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} /std:c++20 /permissive- /bigobj")
else ()
    set(CMAKE_CXX_FLAGS
        "${CMAKE_CXX_FLAGS} -Wall -Wextra -Wpedantic -Wconversion -Wformat -Werror"
    )
endif ()

set(LIBRARY_NAME ${PROJECT_NAME})

set(SOURCES
    src/Dummy.cpp
)

add_library(${LIBRARY_NAME} ${SOURCES})

add_subdirectory(externals/pfr)

target_link_libraries(${LIBRARY_NAME} PUBLIC Boost::pfr)

target_include_directories(
    ${LIBRARY_NAME}
    INTERFACE "${CMAKE_CURRENT_LIST_DIR}/include"
    PRIVATE "${CMAKE_CURRENT_LIST_DIR}/include")

if (BUILD_ORM_CXX_TESTS)
    set(BUILD_FAKER_TESTS OFF)
    add_subdirectory(externals/faker-cxx)
    add_subdirectory(externals/googletest)

    set(UT_SOURCES
        tests/ModelTest.cpp
    )

    set(GTEST_INCLUDE_DIR
        "${CMAKE_SOURCE_DIR}/externals/googletest/googletest/include")
    set(GMOCK_INCLUDE_DIR
        "${CMAKE_SOURCE_DIR}/externals/googletest/googlemock/include")

    enable_testing()

    add_executable(${LIBRARY_NAME}-UT ${UT_SOURCES})

    target_link_libraries(${LIBRARY_NAME}-UT PRIVATE gtest_main gmock_main
        orm-cxx faker-cxx)

    target_include_directories(
        ${LIBRARY_NAME}-UT
        PRIVATE ${FMT_INCLUDE_DIR} ${GTEST_INCLUDE_DIR} ${GMOCK_INCLUDE_DIR}
        ${CMAKE_CURRENT_LIST_DIR})

    add_test(
        NAME ${LIBRARY_NAME}-UT
        COMMAND ${LIBRARY_NAME}-UT
        WORKING_DIRECTORY ${CMAKE_CURRENT_LIST_DIR})
endif ()