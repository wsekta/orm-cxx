include_guard(GLOBAL)

function(orm_cxx_define_options)
    if(PROJECT_IS_TOP_LEVEL)
        set(developer_default ON)
    else()
        set(developer_default OFF)
    endif()

    option(ORM_CXX_BUILD_TESTS "Build the orm-cxx test suite" "${developer_default}")
    option(ORM_CXX_BUILD_EXAMPLES "Build the orm-cxx examples" "${developer_default}")
    option(ORM_CXX_ENABLE_COVERAGE "Instrument orm-cxx and generate code coverage reports" OFF)
    option(ORM_CXX_ENABLE_SQLITE_BACKEND "Build and register the SQLite backend" ON)
    option(ORM_CXX_ENABLE_POSTGRESQL_BACKEND "Build and register the PostgreSQL backend" OFF)
    option(ORM_CXX_USE_SYSTEM_SOCI "Use an installed SOCI package instead of the bundled submodule" OFF)
    option(ORM_CXX_ENABLE_POSTGRESQL_INTEGRATION_TESTS
           "Build live PostgreSQL integration tests (requires ORM_CXX_POSTGRESQL_TEST_DSN at runtime)" OFF
    )
    option(ORM_CXX_WARNINGS_AS_ERRORS "Treat warnings from orm-cxx sources as errors" "${developer_default}")
endfunction()
