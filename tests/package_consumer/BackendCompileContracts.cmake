# Verify disabled provider declarations against the same installed module graph as a positive consumer.
function(orm_cxx_backend_compile_contracts positive_target)
    add_custom_target(orm-cxx-compile-controls DEPENDS ${positive_target})
    set(manifest "${CMAKE_CURRENT_BINARY_DIR}/BackendCompileContracts.cmake")
    file(WRITE "${manifest}" "set(orm_cxx_failure_cases\n")
    foreach(backend Sqlite Postgresql Mysql Mariadb)
        string(TOUPPER "${backend}" backend_upper)
        string(TOLOWER "${backend}" backend_lower)
        if(DEFINED ORM_CXX_EXPECT_${backend_upper} AND NOT ORM_CXX_EXPECT_${backend_upper})
            set(target "orm-cxx-disabled-${backend_lower}")
            set(source "${CMAKE_CURRENT_BINARY_DIR}/Disabled${backend}Backend.cpp")
            file(WRITE "${source}"
                 "import orm;\nvoid disabledProvider() { orm::db::${backend_lower}::${backend}Backend backend; }\n"
            )
            add_library(${target} OBJECT EXCLUDE_FROM_ALL "${source}")
            set_target_properties(${target} PROPERTIES CXX_SCAN_FOR_MODULES ON)
            target_link_libraries(${target} PRIVATE orm-cxx::orm-cxx)
            file(APPEND "${manifest}" "    [==[${target}|${backend}Backend]==]\n")
        endif()
    endforeach()
    file(APPEND "${manifest}" ")\n")
    add_test(NAME orm-cxx-backend-compile-contracts
             COMMAND "${CMAKE_COMMAND}" "-DORM_CXX_BUILD_DIR=${CMAKE_BINARY_DIR}" "-DORM_CXX_BUILD_CONFIG=$<CONFIG>"
                     "-DORM_CXX_CASE_MANIFEST=${manifest}" -P "${CMAKE_CURRENT_FUNCTION_LIST_DIR}/CheckBuild.cmake"
    )
endfunction()
