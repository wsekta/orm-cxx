cmake_minimum_required(VERSION 3.22)

project(orm-cxx LANGUAGES CXX)

list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/cmake")

include(OrmCxxDependencies)
include(OrmCxxOptions)
include(OrmCxxProjectSettings)

orm_cxx_define_options()

if(ORM_CXX_ENABLE_COVERAGE AND NOT ORM_CXX_BUILD_TESTS)
    message(
        FATAL_ERROR
            "ORM_CXX_ENABLE_COVERAGE requires ORM_CXX_BUILD_TESTS=ON because the coverage report is produced by orm-cxx-UT."
    )
endif()

orm_cxx_add_project_settings()
orm_cxx_add_runtime_dependencies("${CMAKE_CURRENT_SOURCE_DIR}")

add_subdirectory(src)

if(ORM_CXX_BUILD_TESTS)
    orm_cxx_add_test_dependencies("${CMAKE_CURRENT_SOURCE_DIR}")
    enable_testing()
    add_subdirectory(tests)
endif()

if(ORM_CXX_BUILD_EXAMPLES)
    add_subdirectory(examples)
endif()

if(ORM_CXX_ENABLE_COVERAGE)
    include(OrmCxxCoverage)
    orm_cxx_enable_coverage(orm-cxx orm-cxx-UT)
endif()
