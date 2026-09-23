cmake_minimum_required(VERSION 3.22)

file(STRINGS "${CMAKE_CURRENT_SOURCE_DIR}/VERSION.txt" ORM_CXX_VERSION LIMIT_COUNT 1)
if(NOT ORM_CXX_VERSION MATCHES "^[0-9]+\\.[0-9]+\\.[0-9]+$")
    message(FATAL_ERROR "VERSION.txt must contain a three-part numeric version (for example 0.1.0).")
endif()
project(
    orm-cxx
    VERSION "${ORM_CXX_VERSION}"
    LANGUAGES CXX
)

include(GNUInstallDirs)

list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/cmake")

include(OrmCxxDependencies)
include(OrmCxxOptions)
include(OrmCxxProjectSettings)

orm_cxx_define_options()

if(ORM_CXX_BUILD_EXAMPLES
   AND NOT ORM_CXX_ENABLE_SQLITE_BACKEND
   AND NOT ORM_CXX_ENABLE_POSTGRESQL_BACKEND
)
    message(FATAL_ERROR "ORM_CXX_BUILD_EXAMPLES requires at least one enabled database backend.")
endif()

if(ORM_CXX_BUILD_TESTS AND NOT ORM_CXX_ENABLE_SQLITE_BACKEND)
    message(
        FATAL_ERROR
            "The legacy developer test suite still exercises SQLite. Configure ORM_CXX_BUILD_TESTS=OFF for PostgreSQL-only and core-only consumer builds."
    )
endif()

if(ORM_CXX_ENABLE_POSTGRESQL_INTEGRATION_TESTS AND NOT ORM_CXX_ENABLE_POSTGRESQL_BACKEND)
    message(FATAL_ERROR "ORM_CXX_ENABLE_POSTGRESQL_INTEGRATION_TESTS requires ORM_CXX_ENABLE_POSTGRESQL_BACKEND=ON.")
endif()

if(ORM_CXX_ENABLE_POSTGRESQL_INTEGRATION_TESTS AND NOT ORM_CXX_BUILD_TESTS)
    message(FATAL_ERROR "ORM_CXX_ENABLE_POSTGRESQL_INTEGRATION_TESTS requires ORM_CXX_BUILD_TESTS=ON.")
endif()

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
