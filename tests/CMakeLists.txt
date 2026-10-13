set(ORM_CXX_TEST_SOURCES
    migrations/MigrationTest.cpp
    migrations/SnapshotTest.cpp
    migrations/GeneratorTest.cpp
    migrations/ProjectTest.cpp
    migrations/NativeGenerationTest.cpp
    migrations/TargetValuesTest.cpp
    migrations/IndexDriftTest.cpp
    ../src/database/BackendCapabilitiesTest.cpp
    ../src/database/CommandGeneratorFactoryTest.cpp
    ../src/database/NumericConversionTest.cpp
    ../src/model/ColumnTypeTest.cpp
    ../src/utils/StringUtilsTest.cpp
    ../src/database/defaults/DefaultCreateTableCommandTest.cpp
    ../src/database/defaults/DefaultDropTableCommandTest.cpp
    ../src/database/defaults/DefaultInsertCommandTest.cpp
    ../src/database/defaults/DefaultSelectCommandTest.cpp
    ../src/database/defaults/DefaultUpdateCommandTest.cpp
    ../src/database/defaults/DefaultDeleteCommandTest.cpp
    ../src/query/PredicateTest.cpp
    ../src/query/ProjectionQueryTest.cpp
    ../src/query/QueryTest.cpp
    ../src/query/QueryValueTest.cpp
    ../src/query/TypedQueryTest.cpp
    ../src/query/StaticPlanTest.cpp
    ../src/query/ExpressionRuntimeTest.cpp
    ../src/query/StaticPlanCoverageTest.cpp
    ../src/query/CompiledSqlCoverageTest.cpp
    CollectionRelationCoverageTest.cpp
    database/DatabaseValidationTest.cpp
    database/DatabaseErrorTest.cpp
    database/StaticDatabaseApiCompileTest.cpp
    model/StaticMetadataTest.cpp
    model/StringLengthTest.cpp
    reflection/GeneratedBindingArity0To15Test.cpp
    reflection/GeneratedBindingArity16To31Test.cpp
    reflection/GeneratedBindingArity32To47Test.cpp
    reflection/GeneratedBindingArity48To63Test.cpp
    reflection/GeneratedBindingArity64To79Test.cpp
    reflection/GeneratedBindingArity80To95Test.cpp
    reflection/GeneratedBindingArity96To111Test.cpp
    reflection/GeneratedBindingArity112To127Test.cpp
    reflection/GeneratedBindingArity128To128Test.cpp
    reflection/MemberNameModuleTest.cpp
    reflection/TypeNameTest.cpp
    reflection/TypeNameStorageConsumer.cpp
    utils/ConstexprForTest.cpp
)

if(ORM_CXX_ENABLE_SQLITE_BACKEND)
    list(
        APPEND
        ORM_CXX_TEST_SOURCES
        database/DatabaseCoverageCompletionTest.cpp
        database/SqliteConnectErrorTest.cpp
        database/StatementParameterBindingTest.cpp
        database/SqliteStaticPlanRendererTest.cpp
        database/SqliteContextBindingTest.cpp
    )
endif()
if(ORM_CXX_ENABLE_SQLITE_BACKEND
   OR (ORM_CXX_ENABLE_POSTGRESQL_BACKEND AND ORM_CXX_ENABLE_POSTGRESQL_INTEGRATION_TESTS)
   OR (ORM_CXX_ENABLE_MYSQL_BACKEND AND ORM_CXX_ENABLE_MYSQL_INTEGRATION_TESTS)
   OR (ORM_CXX_ENABLE_MARIADB_BACKEND AND ORM_CXX_ENABLE_MARIADB_INTEGRATION_TESTS)
)
    list(
        APPEND
        ORM_CXX_TEST_SOURCES
        database/BackendConformanceTest.cpp
        database/StringLengthTest.cpp
        database/OrmContextTest.cpp
        database/StaticPlanTest.cpp
        database/ProjectionQueryTest.cpp
        database/TransactionsTest.cpp
        database/SimpleModelTest.cpp
        database/WriteOperationsTest.cpp
        database/IdModelsTest.cpp
        database/RelatedModelTest.cpp
        database/CollectionRelationTest.cpp
        database/CollectionRelationAcceptanceTest.cpp
        database/QueryLanguageTest.cpp
        database/BaseOperationsTest.cpp
    )
endif()

if(ORM_CXX_ENABLE_SQLITE_BACKEND)
    list(
        APPEND
        ORM_CXX_TEST_SOURCES
        ../src/database/sqlite/SqliteBackendTest.cpp
        ../src/database/sqlite/SqliteDialectTest.cpp
        ../src/database/sqlite/SqliteTypeTranslatorTest.cpp
        migrations/SqliteMigrationTest.cpp
        migrations/RebuildTest.cpp
        migrations/RebuildFailureTest.cpp
    )
endif()
if(ORM_CXX_ENABLE_MYSQL_BACKEND OR ORM_CXX_ENABLE_MARIADB_BACKEND)
    list(APPEND ORM_CXX_TEST_SOURCES ../src/database/mysql_family/MysqlFamilyTest.cpp)
endif()
foreach(backend Mysql Mariadb)
    string(TOUPPER "${backend}" backend_upper)
    string(TOLOWER "${backend}" backend_dir)
    if(ORM_CXX_ENABLE_${backend_upper}_BACKEND)
        list(APPEND ORM_CXX_TEST_SOURCES ../src/database/${backend_dir}/${backend}BackendTest.cpp
             ../src/database/${backend_dir}/${backend}DialectTest.cpp
             ../src/database/${backend_dir}/${backend}TypeTranslatorTest.cpp
        )
    endif()
endforeach()

if(ORM_CXX_ENABLE_POSTGRESQL_BACKEND)
    list(APPEND ORM_CXX_TEST_SOURCES ../src/database/postgresql/PostgresqlBackendTest.cpp
         ../src/database/postgresql/PostgresqlDialectTest.cpp
         ../src/database/postgresql/PostgresqlTypeTranslatorTest.cpp
    )

    if(ORM_CXX_ENABLE_POSTGRESQL_INTEGRATION_TESTS)
        list(APPEND ORM_CXX_TEST_SOURCES database/postgresql/PostgresqlIntegrationTest.cpp)
    endif()
endif()

add_executable(orm-cxx-UT ${ORM_CXX_TEST_SOURCES})
target_sources(orm-cxx-UT PRIVATE FILE_SET test_modules TYPE CXX_MODULES FILES Support.cppm)
set_target_properties(orm-cxx-UT PROPERTIES RUNTIME_OUTPUT_DIRECTORY "${PROJECT_BINARY_DIR}" CXX_SCAN_FOR_MODULES ON)
target_link_libraries(orm-cxx-UT PRIVATE gtest_main gmock_main orm-cxx::orm-cxx)
foreach(backend POSTGRESQL MYSQL MARIADB)
    string(TOLOWER "${backend}" backend_lower)
    set(orm_cxx_${backend_lower}_integration_enabled false)
    if(ORM_CXX_ENABLE_${backend}_BACKEND AND ORM_CXX_ENABLE_${backend}_INTEGRATION_TESTS)
        set(orm_cxx_${backend_lower}_integration_enabled true)
    endif()
endforeach()
configure_file(
    "${PROJECT_SOURCE_DIR}/cmake/OrmCxxTestConfig.hpp.in"
    "${PROJECT_BINARY_DIR}/generated/test-include/orm-cxx-tests/TestConfig.hpp" @ONLY
)
target_include_directories(orm-cxx-UT PRIVATE "${PROJECT_SOURCE_DIR}" "${PROJECT_BINARY_DIR}/generated/test-include")

add_executable(orm-cxx-static-plan-allocations static_plan_allocations/main.cpp static_plan_allocations/Plans.cpp)
target_link_libraries(orm-cxx-static-plan-allocations PRIVATE orm-cxx::orm-cxx orm-cxx-project-warnings)
set_target_properties(
    orm-cxx-static-plan-allocations PROPERTIES RUNTIME_OUTPUT_DIRECTORY "${PROJECT_BINARY_DIR}" CXX_SCAN_FOR_MODULES ON
)
add_test(NAME orm-cxx-static-plan-allocations COMMAND orm-cxx-static-plan-allocations)
set_tests_properties(orm-cxx-static-plan-allocations PROPERTIES LABELS "unit;query;allocation")
foreach(component SQLITE3 POSTGRESQL MYSQL)
    if(ORM_CXX_SOCI_${component}_TARGET)
        target_include_directories(
            orm-cxx-UT SYSTEM PRIVATE "$<TARGET_PROPERTY:${ORM_CXX_SOCI_${component}_TARGET},INCLUDE_DIRECTORIES>"
        )
    endif()
endforeach()

if(MSVC)
    set_source_files_properties(database/BackendConformanceTest.cpp PROPERTIES COMPILE_OPTIONS "/bigobj")
    set_source_files_properties(reflection/TypeNameTest.cpp PROPERTIES COMPILE_OPTIONS "/bigobj")
    set(orm_cxx_test_working_directory "${PROJECT_BINARY_DIR}/bin")
else()
    set(orm_cxx_test_working_directory "${PROJECT_SOURCE_DIR}")
endif()

set_tests_properties(orm-cxx-static-plan-allocations PROPERTIES WORKING_DIRECTORY "${orm_cxx_test_working_directory}")

add_test(
    NAME orm-cxx-UT
    COMMAND orm-cxx-UT --gtest_filter=-DatabaseTest/*:Postgresql/*:PostgresqlTestSchemaTest.*
    WORKING_DIRECTORY "${orm_cxx_test_working_directory}"
)
set_tests_properties(orm-cxx-UT PROPERTIES LABELS "unit;core")

foreach(backend Sqlite Postgresql Mysql Mariadb)
    string(TOUPPER "${backend}" backend_upper)
    string(TOLOWER "${backend}" backend_lower)
    if(ORM_CXX_ENABLE_${backend_upper}_BACKEND AND (backend STREQUAL "Sqlite"
                                                    OR ORM_CXX_ENABLE_${backend_upper}_INTEGRATION_TESTS)
    )
        add_test(
            NAME orm-cxx-${backend_lower}-conformance
            COMMAND orm-cxx-UT "--gtest_filter=DatabaseTest/*.*/${backend}"
            WORKING_DIRECTORY "${orm_cxx_test_working_directory}"
        )
        set_tests_properties(
            orm-cxx-${backend_lower}-conformance PROPERTIES LABELS "conformance;backend-${backend_lower}"
                                                            FAIL_REGULAR_EXPRESSION "\\[  PASSED  \\] 0 tests"
        )
    endif()
endforeach()
if(ORM_CXX_ENABLE_POSTGRESQL_BACKEND AND ORM_CXX_ENABLE_POSTGRESQL_INTEGRATION_TESTS)
    add_test(
        NAME orm-cxx-postgresql-integration
        COMMAND orm-cxx-UT --gtest_filter=Postgresql/PostgresqlIntegrationTest.*/Postgresql:PostgresqlTestSchemaTest.*
        WORKING_DIRECTORY "${orm_cxx_test_working_directory}"
    )
    set_tests_properties(
        orm-cxx-postgresql-integration PROPERTIES LABELS "integration;backend-postgresql" FAIL_REGULAR_EXPRESSION
                                                  "\\[  PASSED  \\] 0 tests"
    )
endif()

set(orm_cxx_make_program "${CMAKE_MAKE_PROGRAM}")
if(orm_cxx_make_program AND NOT IS_ABSOLUTE "${orm_cxx_make_program}")
    find_program(orm_cxx_make_program_absolute NAMES "${orm_cxx_make_program}")
    if(orm_cxx_make_program_absolute)
        set(orm_cxx_make_program "${orm_cxx_make_program_absolute}")
    endif()
endif()

add_subdirectory(compile_fail)

set(orm_cxx_consumer_common_configure_arguments
    -S
    "${CMAKE_CURRENT_SOURCE_DIR}/consumer"
    -G
    "${CMAKE_GENERATOR}"
    "-DORM_CXX_SOURCE_DIR=${PROJECT_SOURCE_DIR}"
    "-DCMAKE_CXX_COMPILER=${CMAKE_CXX_COMPILER}"
    "-DCMAKE_BUILD_TYPE=Debug"
)

if(CMAKE_GENERATOR_PLATFORM)
    list(APPEND orm_cxx_consumer_common_configure_arguments -A "${CMAKE_GENERATOR_PLATFORM}")
endif()
if(CMAKE_GENERATOR_TOOLSET)
    list(APPEND orm_cxx_consumer_common_configure_arguments -T "${CMAKE_GENERATOR_TOOLSET}")
endif()
if(orm_cxx_make_program)
    list(APPEND orm_cxx_consumer_common_configure_arguments "-DCMAKE_MAKE_PROGRAM=${orm_cxx_make_program}")
endif()
if(CMAKE_RC_COMPILER)
    list(APPEND orm_cxx_consumer_common_configure_arguments "-DCMAKE_RC_COMPILER:FILEPATH=${CMAKE_RC_COMPILER}")
endif()
if(CMAKE_MT)
    list(APPEND orm_cxx_consumer_common_configure_arguments "-DCMAKE_MT=${CMAKE_MT}")
endif()
if(CMAKE_TOOLCHAIN_FILE)
    list(APPEND orm_cxx_consumer_common_configure_arguments "-DCMAKE_TOOLCHAIN_FILE=${CMAKE_TOOLCHAIN_FILE}")
endif()
if(CMAKE_MSVC_DEBUG_INFORMATION_FORMAT)
    list(APPEND orm_cxx_consumer_common_configure_arguments
         "-DCMAKE_MSVC_DEBUG_INFORMATION_FORMAT=${CMAKE_MSVC_DEBUG_INFORMATION_FORMAT}"
    )
endif()
if(VCPKG_INSTALLED_DIR)
    list(APPEND orm_cxx_consumer_common_configure_arguments "-DVCPKG_INSTALLED_DIR=${VCPKG_INSTALLED_DIR}"
         -DVCPKG_MANIFEST_INSTALL=OFF
    )
endif()
if(VCPKG_TARGET_TRIPLET)
    list(APPEND orm_cxx_consumer_common_configure_arguments "-DVCPKG_TARGET_TRIPLET=${VCPKG_TARGET_TRIPLET}")
endif()

function(orm_cxx_add_consumer_profile profile sqlite_enabled postgresql_enabled mysql_enabled mariadb_enabled)
    set(binary_dir "${PROJECT_BINARY_DIR}/consumer-${profile}-test")
    set(configure_test "orm-cxx-consumer-${profile}-configure")
    set(build_test "orm-cxx-consumer-${profile}-build")
    set(run_test "orm-cxx-consumer-${profile}-run")
    set(configure_arguments
        ${orm_cxx_consumer_common_configure_arguments}
        -B
        "${binary_dir}"
        "-DORM_CXX_ENABLE_SQLITE_BACKEND=${sqlite_enabled}"
        "-DORM_CXX_ENABLE_POSTGRESQL_BACKEND=${postgresql_enabled}"
        "-DORM_CXX_ENABLE_MYSQL_BACKEND=${mysql_enabled}"
        "-DORM_CXX_ENABLE_MARIADB_BACKEND=${mariadb_enabled}"
    )

    add_test(NAME "${configure_test}" COMMAND "${CMAKE_COMMAND}" ${configure_arguments})
    add_test(NAME "${build_test}" COMMAND "${CMAKE_COMMAND}" --build "${binary_dir}" --config Debug)
    set_tests_properties("${configure_test}" "${build_test}" PROPERTIES LABELS "consumer;${profile}")
    set_tests_properties("${build_test}" PROPERTIES DEPENDS "${configure_test}")

    add_test(
        NAME "${run_test}"
        COMMAND "${CMAKE_CTEST_COMMAND}" --test-dir "${binary_dir}" --build-config Debug --output-on-failure
        WORKING_DIRECTORY "${binary_dir}/bin"
    )
    set_tests_properties("${run_test}" PROPERTIES DEPENDS "${build_test}" LABELS "consumer;${profile}")
    if(WIN32
       AND VCPKG_INSTALLED_DIR
       AND VCPKG_TARGET_TRIPLET
    )
        set(vcpkg_debug_bin_dir "${VCPKG_INSTALLED_DIR}/${VCPKG_TARGET_TRIPLET}/debug/bin")
        set_property(
            TEST "${run_test}"
            APPEND
            PROPERTY ENVIRONMENT_MODIFICATION "PATH=path_list_prepend:${vcpkg_debug_bin_dir}"
        )
    endif()
endfunction()

# Exercise every subset of the drivers installed for this build, including core-only.
foreach(mask RANGE 0 15)
    set(profile_parts)
    set(profile_available ON)
    set(index 0)
    foreach(backend SQLITE POSTGRESQL MYSQL MARIADB)
        math(EXPR selected "(${mask} >> ${index}) & 1")
        string(TOLOWER "${backend}" backend_lower)
        set(${backend_lower}_enabled OFF)
        if(selected)
            set(${backend_lower}_enabled ON)
            list(APPEND profile_parts "${backend_lower}")
            if(NOT ORM_CXX_ENABLE_${backend}_BACKEND)
                set(profile_available OFF)
            endif()
        endif()
        math(EXPR index "${index} + 1")
    endforeach()
    if(profile_available)
        if(profile_parts)
            list(JOIN profile_parts "+" profile)
        else()
            set(profile core)
        endif()
        orm_cxx_add_consumer_profile(
            "${profile}" ${sqlite_enabled} ${postgresql_enabled} ${mysql_enabled} ${mariadb_enabled}
        )
    endif()
endforeach()
