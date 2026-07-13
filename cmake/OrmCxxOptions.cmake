include_guard(GLOBAL)

function(_orm_cxx_deprecated_bool_option new_name legacy_name description default_value)
    set(resolved_default "${default_value}")

    if(DEFINED ${legacy_name})
        message(
            DEPRECATION
                "${legacy_name} is deprecated; use ${new_name} instead. Support for ${legacy_name} will be removed in a future release."
        )

        if(NOT DEFINED ${new_name})
            set(resolved_default "${${legacy_name}}")
        endif()
    endif()

    option(${new_name} "${description}" "${resolved_default}")
endfunction()

function(orm_cxx_define_options)
    if(PROJECT_IS_TOP_LEVEL)
        set(developer_default ON)
    else()
        set(developer_default OFF)
    endif()

    _orm_cxx_deprecated_bool_option(
        ORM_CXX_BUILD_TESTS BUILD_ORM_CXX_TESTS "Build the orm-cxx test suite" "${developer_default}"
    )
    _orm_cxx_deprecated_bool_option(
        ORM_CXX_BUILD_EXAMPLES BUILD_ORM_CXX_EXAMPLE "Build the orm-cxx examples" "${developer_default}"
    )
    _orm_cxx_deprecated_bool_option(
        ORM_CXX_ENABLE_COVERAGE CODE_COVERAGE "Instrument orm-cxx and generate code coverage reports" OFF
    )
    option(ORM_CXX_ENABLE_SQLITE_BACKEND "Build and register the SQLite backend" ON)
    option(ORM_CXX_WARNINGS_AS_ERRORS "Treat warnings from orm-cxx sources as errors" "${developer_default}")
endfunction()
