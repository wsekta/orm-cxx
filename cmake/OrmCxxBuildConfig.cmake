include_guard(GLOBAL)

# Keep configuration in generated C++ headers so consumers use the exact backend selection compiled into their library,
# without exported preprocessor flags.
function(orm_cxx_generate_build_config generated_include_dir sqlite_enabled postgresql_enabled)
    if(sqlite_enabled)
        set(ORM_CXX_SQLITE_BACKEND_ENABLED true)
    else()
        set(ORM_CXX_SQLITE_BACKEND_ENABLED false)
    endif()
    if(postgresql_enabled)
        set(ORM_CXX_POSTGRESQL_BACKEND_ENABLED true)
    else()
        set(ORM_CXX_POSTGRESQL_BACKEND_ENABLED false)
    endif()

    file(MAKE_DIRECTORY "${generated_include_dir}/orm-cxx/database/detail")
    configure_file(
        "${CMAKE_CURRENT_FUNCTION_LIST_DIR}/BuildConfig.hpp.in" "${generated_include_dir}/orm-cxx/BuildConfig.hpp"
        @ONLY
    )

    if(sqlite_enabled)
        set(sqlite_declarations_template SqliteRelationDeclarations.hpp.in)
    else()
        set(sqlite_declarations_template EmptyBackendDeclarations.hpp.in)
    endif()
    configure_file(
        "${CMAKE_CURRENT_FUNCTION_LIST_DIR}/${sqlite_declarations_template}"
        "${generated_include_dir}/orm-cxx/database/detail/SqliteRelationDeclarations.hpp" COPYONLY
    )
endfunction()
