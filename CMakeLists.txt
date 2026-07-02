cmake_minimum_required(VERSION 3.22)

set(PROJECT_NAME orm-cxx)
project(${PROJECT_NAME} CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED YES)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

option(BUILD_ORM_CXX_TESTS DEFAULT ON)
option(BUILD_ORM_CXX_EXAMPLE DEFAULT ON)

set(BUILD_SHARED_LIBS OFF CACHE INTERNAL "Static Build" FORCE)
set(SOCI_SHARED ON CACHE INTERNAL "SOCI Shared" FORCE)
set(SOCI_STATIC OFF CACHE INTERNAL "SOCI Static" FORCE)
set(SOCI_TESTS OFF CACHE INTERNAL "SOCI Tests" FORCE)
set(WITH_BOOST OFF CACHE INTERNAL "SOCI use Boost" FORCE)
set(SOCI_EMPTY OFF CACHE INTERNAL "SOCI Backend" FORCE)
set(WITH_DB2 OFF CACHE INTERNAL "SOCI Backend" FORCE)
set(WITH_FIREBIRD OFF CACHE INTERNAL "SOCI Backend" FORCE)
set(WITH_MYSQL OFF CACHE INTERNAL "SOCI Backend" FORCE)
set(WITH_ODBC OFF CACHE INTERNAL "SOCI Backend" FORCE)
set(WITH_ORACLE OFF CACHE INTERNAL "SOCI Backend" FORCE)
set(WITH_POSTGRESQL OFF CACHE INTERNAL "SOCI Backend" FORCE)
set(WITH_SQLITE3 ON CACHE INTERNAL "SOCI Backend" FORCE)
set(SOCI_SQLITE3_TEST_CONNSTR ":memory:")
add_subdirectory(externals/soci)

set(LIBRARY_NAME ${PROJECT_NAME})

set(SOURCES
        src/Database.cpp
        src/database/sqlite/SqliteTypeTranslator.cpp
        src/model/ColumnType.cpp
        src/database/CommandGeneratorFactory.cpp
        src/utils/StringUtils.cpp
        src/database/defaults/DefaultCreateTableCommand.cpp
        src/database/defaults/DefaultDropTableCommand.cpp
        src/database/defaults/DefaultInsertCommand.cpp
        src/database/defaults/DefaultSelectCommand.cpp
        src/database/defaults/DefaultUpdateCommand.cpp
        src/database/defaults/DefaultDeleteCommand.cpp
        src/database/defaults/SqlRenderer.cpp
        src/database/CommandGenerator.cpp
)

add_library(${LIBRARY_NAME} ${SOURCES})

target_include_directories(
        ${LIBRARY_NAME}
        INTERFACE "${CMAKE_CURRENT_LIST_DIR}/include"
        PRIVATE "${CMAKE_CURRENT_LIST_DIR}/include"
        PUBLIC externals/reflect-cpp/include)

target_link_libraries(${LIBRARY_NAME} PUBLIC soci_core soci_sqlite3)

file(GLOB_RECURSE EXTERNALS_DIR "externals/*.c" "externals/*.h" "externals/*.cpp" "externals/*.hpp")

if (MSVC)
    set_source_files_properties(
            ${EXTERNALS_DIR}
            PROPERTIES
            COMPILE_FLAGS "/IGNORE"
    )
    set_target_properties(${LIBRARY_NAME} PROPERTIES COMPILE_FLAGS " /std:c++20 /permissive- /W4 /WX /bigobj")
else ()
    set_source_files_properties(
            ${EXTERNALS_DIR}
            PROPERTIES
            COMPILE_FLAGS "-Wno-everything"
    )
    set_target_properties(${LIBRARY_NAME} PROPERTIES COMPILE_FLAGS " -Wall -Wextra -Wpedantic -Wconversion -Wshadow -Wformat -Werror")
endif ()

if (BUILD_ORM_CXX_TESTS)
    set(BUILD_FAKER_TESTS OFF)
    add_subdirectory(externals/faker-cxx)
    add_subdirectory(externals/googletest)

    set(UT_SOURCES
            src/database/sqlite/SqliteTypeTranslatorTest.cpp
            src/model/ColumnTypeTest.cpp
            src/utils/StringUtilsTest.cpp
            src/database/defaults/DefaultCreateTableCommandTest.cpp
            src/database/defaults/DefaultDropTableCommandTest.cpp
            src/database/defaults/DefaultInsertCommandTest.cpp
            src/database/defaults/DefaultSelectCommandTest.cpp
            src/database/defaults/DefaultUpdateCommandTest.cpp
            src/database/defaults/DefaultDeleteCommandTest.cpp
            src/query/PredicateTest.cpp
            src/query/ProjectionQueryTest.cpp
            src/query/QueryValueTest.cpp

            tests/ModelTest.cpp
            tests/database/StatementParameterBindingTest.cpp
            tests/database/ProjectionQueryTest.cpp
            tests/database/TransactionsTest.cpp
            tests/database/SimpleModelTest.cpp
            tests/database/WriteOperationsTest.cpp
            tests/database/IdModelsTest.cpp
            tests/database/RelatedModelTest.cpp
            tests/database/QueryLanguageTest.cpp
            tests/database/BaseOperationsTest.cpp
            tests/model/IdInfoTest.cpp
            tests/model/TableInfoTest.cpp
            tests/reflection/TypeNameTest.cpp
            tests/utils/ConstexprForTest.cpp
            tests/utils/ConstexprStringViewTest.cpp
    )

    set(GTEST_INCLUDE_DIR
            "${CMAKE_SOURCE_DIR}/externals/googletest/googletest/include")
    set(GMOCK_INCLUDE_DIR
            "${CMAKE_SOURCE_DIR}/externals/googletest/googlemock/include")

    enable_testing()

    set(target_code_coverage_ALL 1)
    set(ORM_CXX_COVERAGE_EXCLUDES
            ".*/externals/.*"
            ".*/tests/.*"
            ".*/src/.*Test\\.cpp")

    include("cmake/cmake-coverage.cmake")

    add_code_coverage_all_targets(EXCLUDE ${ORM_CXX_COVERAGE_EXCLUDES})

    target_code_coverage(${LIBRARY_NAME})

    add_executable(${LIBRARY_NAME}-UT ${UT_SOURCES})

    target_link_libraries(${LIBRARY_NAME}-UT PRIVATE gtest_main gmock_main orm-cxx faker-cxx)

    target_include_directories(
            ${LIBRARY_NAME}-UT
            PRIVATE ${FMT_INCLUDE_DIR} ${GTEST_INCLUDE_DIR} ${GMOCK_INCLUDE_DIR}
            ${CMAKE_CURRENT_LIST_DIR})

    if (MSVC)
        add_test(
                NAME ${LIBRARY_NAME}-UT
                COMMAND ${LIBRARY_NAME}-UT
                WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}/bin)
    else ()
        add_test(
                NAME ${LIBRARY_NAME}-UT
                COMMAND ${LIBRARY_NAME}-UT
                WORKING_DIRECTORY ${CMAKE_CURRENT_LIST_DIR})
    endif ()

    target_code_coverage(${LIBRARY_NAME}-UT ALL)
endif ()

if (BUILD_ORM_CXX_EXAMPLE)
    add_subdirectory(examples)
endif ()
