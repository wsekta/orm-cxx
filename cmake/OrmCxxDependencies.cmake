include_guard(GLOBAL)

function(orm_cxx_add_runtime_dependencies source_dir)
    set(ORM_CXX_SOCI_NEEDS_OPENSSL
        OFF
        PARENT_SCOPE
    )
    if(ORM_CXX_USE_SYSTEM_SOCI)
        find_package(SOCI 4.0.3 CONFIG REQUIRED)
        set(soci_export_core Core)
        set(soci_export_sqlite3 SQLite3)
        set(soci_export_postgresql PostgreSQL)
        set(soci_components core)
        if(ORM_CXX_ENABLE_SQLITE_BACKEND)
            list(APPEND soci_components sqlite3)
        endif()
        if(ORM_CXX_ENABLE_POSTGRESQL_BACKEND)
            list(APPEND soci_components postgresql)
        endif()

        foreach(component IN LISTS soci_components)
            # Recent SOCI exports component names; older packages and Conan distinguish static and shared targets. Keep
            # the exact exported name so installed consumers use the same linkage.
            if(TARGET SOCI::${soci_export_${component}})
                set(soci_target "SOCI::${soci_export_${component}}")
            elseif(TARGET SOCI::soci_${component})
                set(soci_target "SOCI::soci_${component}")
            elseif(TARGET SOCI::soci_${component}_static)
                set(soci_target "SOCI::soci_${component}_static")
            else()
                message(FATAL_ERROR "The installed SOCI package does not provide its ${component} target.")
            endif()

            # Older SOCI PostgreSQL exports name OpenSSL::SSL in their static link interface without finding it. Inspect
            # both modern and legacy configuration-specific properties, and resolve only declared dependencies.
            if(component STREQUAL "postgresql")
                get_target_property(soci_link_libraries "${soci_target}" INTERFACE_LINK_LIBRARIES)
                get_target_property(soci_configurations "${soci_target}" IMPORTED_CONFIGURATIONS)
                foreach(config IN LISTS soci_configurations)
                    string(TOUPPER "${config}" config_upper)
                    get_target_property(
                        soci_config_libraries "${soci_target}" "IMPORTED_LINK_INTERFACE_LIBRARIES_${config_upper}"
                    )
                    list(APPEND soci_link_libraries "${soci_config_libraries}")
                endforeach()
                if(soci_link_libraries MATCHES "OpenSSL::(SSL|Crypto)")
                    find_package(OpenSSL REQUIRED)
                    set(ORM_CXX_SOCI_NEEDS_OPENSSL
                        ON
                        PARENT_SCOPE
                    )
                endif()
            endif()
            string(TOUPPER "${component}" component_upper)
            set(ORM_CXX_SOCI_${component_upper}_TARGET
                "${soci_target}"
                PARENT_SCOPE
            )
            set(ORM_CXX_SOCI_${component_upper}_INSTALLED_TARGET
                "${soci_target}"
                PARENT_SCOPE
            )
        endforeach()
        return()
    endif()

    if(TARGET soci_core)
        if(ORM_CXX_ENABLE_SQLITE_BACKEND AND NOT TARGET soci_sqlite3)
            message(
                FATAL_ERROR
                    "ORM_CXX_ENABLE_SQLITE_BACKEND=ON requires the parent project to provide the soci_sqlite3 target alongside soci_core."
            )
        endif()
        if(ORM_CXX_ENABLE_POSTGRESQL_BACKEND AND NOT TARGET soci_postgresql)
            message(
                FATAL_ERROR
                    "ORM_CXX_ENABLE_POSTGRESQL_BACKEND=ON requires the parent project to provide the soci_postgresql target alongside soci_core."
            )
        endif()
    elseif(TARGET soci_sqlite3 OR TARGET soci_postgresql)
        message(FATAL_ERROR "orm-cxx requires the soci_core target when SOCI targets are supplied by a parent project.")
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
        set(WITH_POSTGRESQL "${ORM_CXX_ENABLE_POSTGRESQL_BACKEND}")
        set(SOCI_POSTGRESQL "${ORM_CXX_ENABLE_POSTGRESQL_BACKEND}")
        set(WITH_SQLITE3 "${ORM_CXX_ENABLE_SQLITE_BACKEND}")
        set(SOCI_SQLITE3 "${ORM_CXX_ENABLE_SQLITE_BACKEND}")

        if(ORM_CXX_ENABLE_SQLITE_BACKEND)
            set(SOCI_SQLITE3_TEST_CONNSTR ":memory:")
        endif()

        add_subdirectory("${source_dir}/externals/soci" "${CMAKE_CURRENT_BINARY_DIR}/externals/soci" EXCLUDE_FROM_ALL)

        if(NOT TARGET soci_core)
            message(FATAL_ERROR "The bundled SOCI configuration could not create the soci_core target.")
        endif()

        if(ORM_CXX_ENABLE_SQLITE_BACKEND AND NOT TARGET soci_sqlite3)
            message(
                FATAL_ERROR
                    "The bundled SOCI configuration could not create its SQLite backend. Install the SQLite development package or configure through the repository's vcpkg toolchain."
            )
        endif()

        if(ORM_CXX_ENABLE_POSTGRESQL_BACKEND AND NOT TARGET soci_postgresql)
            message(
                FATAL_ERROR
                    "The bundled SOCI configuration could not create its PostgreSQL backend. Install the libpq development package or configure through the repository's vcpkg toolchain with the postgresql manifest feature."
            )
        endif()
    endif()

    foreach(component core sqlite3 postgresql)
        if(TARGET soci_${component})
            string(TOUPPER "${component}" component_upper)
            set(ORM_CXX_SOCI_${component_upper}_TARGET
                "soci_${component}"
                PARENT_SCOPE
            )
            set(ORM_CXX_SOCI_${component_upper}_INSTALLED_TARGET
                "SOCI::soci_${component}"
                PARENT_SCOPE
            )
        endif()
    endforeach()
endfunction()

function(orm_cxx_add_test_dependencies source_dir)
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
