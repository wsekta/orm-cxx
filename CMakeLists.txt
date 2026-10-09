cmake_minimum_required(VERSION 3.31)

file(STRINGS "${CMAKE_CURRENT_SOURCE_DIR}/VERSION.txt" ORM_CXX_VERSION LIMIT_COUNT 1)
if(NOT ORM_CXX_VERSION MATCHES "^[0-9]+\\.[0-9]+\\.[0-9]+$")
    message(FATAL_ERROR "VERSION.txt must contain a three-part numeric version (for example 0.1.0).")
endif()
project(
    orm-cxx
    VERSION "${ORM_CXX_VERSION}"
    LANGUAGES CXX
)

if(NOT CMAKE_GENERATOR MATCHES "^Ninja")
    message(FATAL_ERROR "orm-cxx requires Ninja or Ninja Multi-Config for its C++20 modules.")
endif()
execute_process(COMMAND "${CMAKE_MAKE_PROGRAM}" --version
                OUTPUT_VARIABLE ORM_CXX_NINJA_VERSION OUTPUT_STRIP_TRAILING_WHITESPACE
                COMMAND_ERROR_IS_FATAL ANY)
if(ORM_CXX_NINJA_VERSION VERSION_LESS 1.11)
    message(FATAL_ERROR "orm-cxx requires Ninja 1.11 or newer for C++20 modules.")
endif()
if(CMAKE_CXX_COMPILER_ID STREQUAL "GNU" AND CMAKE_CXX_COMPILER_VERSION VERSION_LESS 14)
    message(FATAL_ERROR "orm-cxx requires GCC 14 or newer for C++20 modules.")
elseif(CMAKE_CXX_COMPILER_ID STREQUAL "Clang" AND CMAKE_CXX_COMPILER_VERSION VERSION_LESS 18)
    message(FATAL_ERROR "orm-cxx requires Clang 18 or newer for C++20 modules.")
elseif(MSVC AND CMAKE_CXX_COMPILER_VERSION VERSION_LESS 19.50)
    message(FATAL_ERROR "orm-cxx requires MSVC 19.50 or newer for C++20 modules.")
elseif(NOT CMAKE_CXX_COMPILER_ID MATCHES "^(GNU|Clang|MSVC)$")
    message(FATAL_ERROR "orm-cxx supports GCC, Clang and MSVC with C++20 module dependency scanning.")
endif()

# Dependencies retain their traditional headers; module scanning is enabled per ORM target.
set(CMAKE_CXX_SCAN_FOR_MODULES OFF)

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

if(ORM_CXX_BUILD_TESTS AND CMAKE_GENERATOR MATCHES "Ninja")
    # Generated arity tests instantiate large structured bindings. Keep their compilation below the memory limit of
    # hosted CI runners.
    set_property(GLOBAL APPEND PROPERTY JOB_POOLS orm_cxx_test_compile=1)
    set_property(TARGET orm-cxx-UT PROPERTY JOB_POOL_COMPILE orm_cxx_test_compile)
endif()
