include_guard(GLOBAL)

function(orm_cxx_require_oracle_mysql_client soci_target)
    # Resolve headers from the selected driver graph before trying platform defaults.
    set(pending_targets "${soci_target}")
    get_property(
        imported_targets
        DIRECTORY
        PROPERTY IMPORTED_TARGETS
    )
    foreach(imported_target IN LISTS imported_targets)
        if(imported_target MATCHES "[Mm][Yy][Ss][Qq][Ll]")
            list(APPEND pending_targets "${imported_target}")
        endif()
    endforeach()
    set(visited_targets)
    set(client_include_hints ${MYSQL_INCLUDE_DIR} ${MYSQL_INCLUDE_DIRS})
    while(pending_targets)
        list(POP_FRONT pending_targets current_target)
        if(NOT TARGET "${current_target}" OR current_target IN_LIST visited_targets)
            continue()
        endif()
        list(APPEND visited_targets "${current_target}")
        foreach(property INCLUDE_DIRECTORIES INTERFACE_INCLUDE_DIRECTORIES)
            get_target_property(target_includes "${current_target}" "${property}")
            # Conan CMakeDeps wraps its include paths in configuration expressions. Both configurations use the
            # package's public headers; unwrap these simple expressions before the configure-time probe.
            string(REGEX REPLACE "\\$<\\$<CONFIG:[^>]+>:([^<>]*)>" "\\1" target_includes "${target_includes}")
            string(REGEX REPLACE "\\$<BUILD_INTERFACE:([^<>]*)>" "\\1" target_includes "${target_includes}")
            foreach(directory IN LISTS target_includes)
                if(NOT directory MATCHES "\\$<" AND IS_DIRECTORY "${directory}")
                    list(APPEND client_include_hints "${directory}")
                endif()
            endforeach()
        endforeach()
        get_target_property(target_libraries "${current_target}" INTERFACE_LINK_LIBRARIES)
        foreach(library IN LISTS target_libraries)
            if(TARGET "${library}")
                list(APPEND pending_targets "${library}")
            endif()
        endforeach()
    endwhile()
    find_path(
        orm_cxx_mysql_client_include mysql.h
        HINTS ${client_include_hints}
        PATH_SUFFIXES mysql include/mysql include NO_CACHE
    )
    if(NOT orm_cxx_mysql_client_include)
        message(
            FATAL_ERROR
                "MySQL and MariaDB require the Oracle MySQL C client development headers (libmysql/libmysqlclient)."
        )
    endif()
    include(CheckCXXSourceCompiles)
    set(CMAKE_REQUIRED_INCLUDES "${orm_cxx_mysql_client_include}" ${client_include_hints})
    unset(orm_cxx_oracle_mysql_client CACHE)
    check_cxx_source_compiles(
        "#include <mysql.h>
#include <soci/version.h>
#if defined(MARIADB_BASE_VERSION) || defined(MARIADB_VERSION_ID)
#error ORM_CXX_REQUIRES_ORACLE_MYSQL_CLIENT
#endif
static_assert(SOCI_VERSION >= 400100, \"MySQL-family TLS requires SOCI 4.1.0 or newer\");
int main() { auto mode = MYSQL_OPT_SSL_MODE; (void)mode; }"
        orm_cxx_oracle_mysql_client
    )
    if(NOT orm_cxx_oracle_mysql_client)
        message(
            FATAL_ERROR
                "MySQL and MariaDB require Oracle libmysql/libmysqlclient with SSL mode support; MariaDB Connector/C is not supported by this dependency contract."
        )
    endif()
endfunction()

function(orm_cxx_add_runtime_dependencies source_dir)
    if(ORM_CXX_ENABLE_SQLITE_BACKEND)
        find_package(SQLite3 REQUIRED)
    endif()
    set(ORM_CXX_SOCI_NEEDS_OPENSSL
        OFF
        PARENT_SCOPE
    )
    set(soci_components core)
    if(ORM_CXX_ENABLE_SQLITE_BACKEND)
        list(APPEND soci_components sqlite3)
    endif()
    if(ORM_CXX_ENABLE_POSTGRESQL_BACKEND)
        list(APPEND soci_components postgresql)
    endif()
    set(mysql_family_enabled OFF)
    if(ORM_CXX_ENABLE_MYSQL_BACKEND OR ORM_CXX_ENABLE_MARIADB_BACKEND)
        set(mysql_family_enabled ON)
        list(APPEND soci_components mysql)
    endif()
    set(soci_export_core Core)
    set(soci_export_sqlite3 SQLite3)
    set(soci_export_postgresql PostgreSQL)
    set(soci_export_mysql MySQL)
    set(soci_export_components)
    foreach(component IN LISTS soci_components)
        list(APPEND soci_export_components "${soci_export_${component}}")
    endforeach()
    set(ORM_CXX_SOCI_COMPONENTS
        "${soci_export_components}"
        PARENT_SCOPE
    )
    # Clear inherited targets so disabled drivers never leak into the link interface.
    foreach(component CORE SQLITE3 POSTGRESQL MYSQL)
        set(ORM_CXX_SOCI_${component}_TARGET
            ""
            PARENT_SCOPE
        )
        set(ORM_CXX_SOCI_${component}_INSTALLED_TARGET
            ""
            PARENT_SCOPE
        )
    endforeach()
    if(ORM_CXX_USE_SYSTEM_SOCI)
        find_package(SOCI 4.1.0 CONFIG REQUIRED COMPONENTS ${soci_export_components})

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

            if(component STREQUAL "mysql")
                orm_cxx_require_oracle_mysql_client("${soci_target}")
            endif()

            # Older SOCI PostgreSQL exports name OpenSSL::SSL in their static link interface without finding it. Inspect
            # both modern and legacy configuration-specific properties, and resolve only declared dependencies.
            if(NOT component STREQUAL "core")
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
        foreach(component IN LISTS soci_components)
            if(NOT TARGET soci_${component})
                message(
                    FATAL_ERROR "The selected backend requires parent-provided soci_${component} alongside soci_core."
                )
            endif()
        endforeach()
    elseif(
        TARGET soci_sqlite3
        OR TARGET soci_postgresql
        OR TARGET soci_mysql
    )
        message(FATAL_ERROR "orm-cxx requires soci_core when SOCI targets are supplied by a parent project.")
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
        set(WITH_MYSQL "${mysql_family_enabled}")
        set(SOCI_MYSQL "${mysql_family_enabled}")
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

        foreach(component IN LISTS soci_components)
            if(NOT TARGET soci_${component})
                message(
                    FATAL_ERROR
                        "Bundled SOCI could not create soci_${component}. Install the selected backend client development package."
                )
            endif()
        endforeach()

    endif()

    foreach(component IN LISTS soci_components)
        if(TARGET soci_${component})
            if(component STREQUAL "mysql")
                orm_cxx_require_oracle_mysql_client(soci_mysql)
            endif()
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
