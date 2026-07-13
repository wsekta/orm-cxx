include_guard(GLOBAL)

function(orm_cxx_add_runtime_dependencies source_dir)
    if(TARGET soci_core OR TARGET soci_sqlite3)
        if(NOT TARGET soci_core OR NOT TARGET soci_sqlite3)
            message(
                FATAL_ERROR
                    "orm-cxx requires both soci_core and soci_sqlite3 when SOCI targets are supplied by a parent project."
            )
        endif()
    else()
        # Keep all SOCI configuration local to this function/directory tree. In particular, do not overwrite a parent
        # project's cache or BUILD_SHARED_LIBS setting.
        set(SOCI_SHARED ON)
        set(SOCI_STATIC OFF)
        set(SOCI_TESTS OFF)
        set(WITH_BOOST OFF)
        set(SOCI_BOOST OFF)
        set(SOCI_EMPTY OFF)
        set(WITH_DB2 OFF)
        set(WITH_FIREBIRD OFF)
        set(WITH_MYSQL OFF)
        set(WITH_ODBC OFF)
        set(WITH_ORACLE OFF)
        set(WITH_POSTGRESQL OFF)
        set(WITH_SQLITE3 ON)
        set(SOCI_SQLITE3 ON)
        set(SOCI_SQLITE3_TEST_CONNSTR ":memory:")

        add_subdirectory("${source_dir}/externals/soci" "${CMAKE_CURRENT_BINARY_DIR}/externals/soci" EXCLUDE_FROM_ALL)

        if(NOT TARGET soci_core OR NOT TARGET soci_sqlite3)
            message(
                FATAL_ERROR
                    "The bundled SOCI configuration could not create its SQLite backend. Install the SQLite development package or configure through the repository's vcpkg toolchain."
            )
        endif()
    endif()

    if(NOT TARGET orm-cxx-reflect-cpp)
        add_library(orm-cxx-reflect-cpp INTERFACE)
        target_include_directories(
            orm-cxx-reflect-cpp SYSTEM INTERFACE "$<BUILD_INTERFACE:${source_dir}/externals/reflect-cpp/include>"
        )
    endif()
endfunction()

function(orm_cxx_add_test_dependencies source_dir)
    if(NOT TARGET faker-cxx)
        set(BUILD_FAKER_TESTS OFF)
        set(BUILD_SHARED_LIBS OFF)
        add_subdirectory(
            "${source_dir}/externals/faker-cxx" "${CMAKE_CURRENT_BINARY_DIR}/externals/faker-cxx" EXCLUDE_FROM_ALL
        )
    endif()

    if(NOT TARGET gtest_main OR NOT TARGET gmock_main)
        if(TARGET gtest_main OR TARGET gmock_main)
            message(
                FATAL_ERROR
                    "orm-cxx requires both gtest_main and gmock_main when GoogleTest targets are supplied by a parent project."
            )
        endif()

        set(BUILD_GMOCK ON)
        set(INSTALL_GTEST OFF)
        set(BUILD_SHARED_LIBS OFF)
        add_subdirectory(
            "${source_dir}/externals/googletest" "${CMAKE_CURRENT_BINARY_DIR}/externals/googletest" EXCLUDE_FROM_ALL
        )
    endif()
endfunction()
