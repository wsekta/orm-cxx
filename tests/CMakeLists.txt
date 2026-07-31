set(ORM_CXX_TEST_SOURCES
    ../src/database/BackendCapabilitiesTest.cpp
    ../src/database/CommandGeneratorFactoryTest.cpp
    ../src/database/NumericConversionTest.cpp
    ../src/database/sqlite/SqliteBackendTest.cpp
    ../src/database/sqlite/SqliteDialectTest.cpp
    ../src/database/sqlite/SqliteTypeTranslatorTest.cpp
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
    CollectionRelationCoverageTest.cpp
    database/BackendConformanceTest.cpp
    database/DatabaseCoverageCompletionTest.cpp
    database/DatabaseErrorTest.cpp
    database/StaticDatabaseApiCompileTest.cpp
    database/StatementParameterBindingTest.cpp
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
    model/StaticMetadataTest.cpp
    reflection/MemberNameHeaderTest.cpp
    reflection/TypeNameTest.cpp
    utils/ConstexprForTest.cpp
)

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
set_target_properties(orm-cxx-UT PROPERTIES RUNTIME_OUTPUT_DIRECTORY "${PROJECT_BINARY_DIR}")
target_link_libraries(orm-cxx-UT PRIVATE gtest_main gmock_main orm-cxx::orm-cxx faker-cxx)
target_include_directories(orm-cxx-UT PRIVATE "${PROJECT_SOURCE_DIR}")
target_compile_definitions(
    orm-cxx-UT
    PRIVATE ORM_CXX_ENABLE_POSTGRESQL_INTEGRATION_TESTS=$<BOOL:${ORM_CXX_ENABLE_POSTGRESQL_INTEGRATION_TESTS}>
)

if(ORM_CXX_ENABLE_POSTGRESQL_BACKEND)
    target_include_directories(orm-cxx-UT PRIVATE "$<TARGET_PROPERTY:soci_postgresql,INCLUDE_DIRECTORIES>")
endif()

if(MSVC)
    set_source_files_properties(database/BackendConformanceTest.cpp PROPERTIES COMPILE_OPTIONS "/bigobj")
    set(orm_cxx_test_working_directory "${PROJECT_BINARY_DIR}/bin")
else()
    set(orm_cxx_test_working_directory "${PROJECT_SOURCE_DIR}")
endif()

add_test(
    NAME orm-cxx-UT
    COMMAND
        orm-cxx-UT
        --gtest_filter=-DatabaseTest/BackendConformanceTest.*:Postgresql/PostgresqlIntegrationTest.*:PostgresqlTestSchemaTest.*
    WORKING_DIRECTORY "${orm_cxx_test_working_directory}"
)
add_test(
    NAME orm-cxx-sqlite-conformance
    COMMAND orm-cxx-UT --gtest_filter=DatabaseTest/BackendConformanceTest.*/Sqlite
    WORKING_DIRECTORY "${orm_cxx_test_working_directory}"
)
set_tests_properties(
    orm-cxx-UT PROPERTIES ENVIRONMENT "SOCI_BACKENDS_PATH=$<TARGET_FILE_DIR:soci_sqlite3>" LABELS "unit;backend-sqlite"
)

if(ORM_CXX_ENABLE_POSTGRESQL_BACKEND AND ORM_CXX_ENABLE_POSTGRESQL_INTEGRATION_TESTS)
    add_test(
        NAME orm-cxx-postgresql-conformance
        COMMAND orm-cxx-UT --gtest_filter=DatabaseTest/BackendConformanceTest.*/Postgresql
        WORKING_DIRECTORY "${orm_cxx_test_working_directory}"
    )
    add_test(
        NAME orm-cxx-postgresql-integration
        COMMAND orm-cxx-UT --gtest_filter=Postgresql/PostgresqlIntegrationTest.*/Postgresql:PostgresqlTestSchemaTest.*
        WORKING_DIRECTORY "${orm_cxx_test_working_directory}"
    )
    set_tests_properties(
        orm-cxx-postgresql-conformance orm-cxx-postgresql-integration
        PROPERTIES ENVIRONMENT "SOCI_BACKENDS_PATH=$<TARGET_FILE_DIR:soci_postgresql>" LABELS
                   "integration;backend-postgresql" FAIL_REGULAR_EXPRESSION "\\[  PASSED  \\] 0 tests"
    )
    set_tests_properties(orm-cxx-postgresql-conformance PROPERTIES LABELS "conformance;backend-postgresql")
endif()
set_tests_properties(
    orm-cxx-sqlite-conformance
    PROPERTIES ENVIRONMENT "SOCI_BACKENDS_PATH=$<TARGET_FILE_DIR:soci_sqlite3>" LABELS "conformance;backend-sqlite"
               FAIL_REGULAR_EXPRESSION "\\[  PASSED  \\] 0 tests"
)

set(orm_cxx_make_program "${CMAKE_MAKE_PROGRAM}")
if(orm_cxx_make_program AND NOT IS_ABSOLUTE "${orm_cxx_make_program}")
    find_program(orm_cxx_make_program_absolute NAMES "${orm_cxx_make_program}")
    if(orm_cxx_make_program_absolute)
        set(orm_cxx_make_program "${orm_cxx_make_program_absolute}")
    endif()
endif()

set(orm_cxx_compile_fail_arguments
    -S
    "${CMAKE_CURRENT_SOURCE_DIR}/compile_fail"
    -B
    "${PROJECT_BINARY_DIR}/compile-fail-test"
    -G
    "${CMAKE_GENERATOR}"
    "-DORM_CXX_SOURCE_DIR=${PROJECT_SOURCE_DIR}"
    "-DCMAKE_CXX_COMPILER=${CMAKE_CXX_COMPILER}"
)
if(orm_cxx_make_program)
    list(APPEND orm_cxx_compile_fail_arguments "-DCMAKE_MAKE_PROGRAM=${orm_cxx_make_program}")
endif()
add_test(NAME orm-cxx-compile-fail COMMAND "${CMAKE_COMMAND}" ${orm_cxx_compile_fail_arguments})
set_tests_properties(orm-cxx-compile-fail PROPERTIES LABELS "compile-fail;reflection;schema")

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
if(VCPKG_INSTALLED_DIR)
    list(APPEND orm_cxx_consumer_common_configure_arguments "-DVCPKG_INSTALLED_DIR=${VCPKG_INSTALLED_DIR}"
         -DVCPKG_MANIFEST_INSTALL=OFF
    )
endif()
if(VCPKG_TARGET_TRIPLET)
    list(APPEND orm_cxx_consumer_common_configure_arguments "-DVCPKG_TARGET_TRIPLET=${VCPKG_TARGET_TRIPLET}")
endif()

function(orm_cxx_add_consumer_profile profile sqlite_enabled postgresql_enabled)
    set(binary_dir "${PROJECT_BINARY_DIR}/consumer-${profile}-test")
    set(configure_test "orm-cxx-consumer-${profile}-configure")
    set(build_test "orm-cxx-consumer-${profile}-build")
    set(run_test "orm-cxx-consumer-${profile}-run")
    set(configure_arguments
        ${orm_cxx_consumer_common_configure_arguments} -B "${binary_dir}"
        "-DORM_CXX_ENABLE_SQLITE_BACKEND=${sqlite_enabled}" "-DORM_CXX_ENABLE_POSTGRESQL_BACKEND=${postgresql_enabled}"
    )

    add_test(NAME "${configure_test}" COMMAND "${CMAKE_COMMAND}" ${configure_arguments})
    add_test(NAME "${build_test}" COMMAND "${CMAKE_COMMAND}" --build "${binary_dir}" --config Debug)
    set_tests_properties("${configure_test}" "${build_test}" PROPERTIES LABELS "consumer;${profile}")
    set_tests_properties("${build_test}" PROPERTIES DEPENDS "${configure_test}")

    add_test(
        NAME "${run_test}"
        COMMAND "${binary_dir}/bin/orm-cxx-consumer${CMAKE_EXECUTABLE_SUFFIX}"
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

orm_cxx_add_consumer_profile(sqlite-only ON OFF)
orm_cxx_add_consumer_profile(core-only OFF OFF)

if(ORM_CXX_ENABLE_POSTGRESQL_BACKEND)
    orm_cxx_add_consumer_profile(postgresql-only OFF ON)
    orm_cxx_add_consumer_profile(all-backends ON ON)
endif()
