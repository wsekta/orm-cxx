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
    ModelTest.cpp
    CollectionRelationCoverageTest.cpp
    database/BackendConformanceTest.cpp
    database/DatabaseCoverageCompletionTest.cpp
    database/DatabaseErrorTest.cpp
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
    model/CollectionRelationTest.cpp
    model/IdInfoTest.cpp
    model/TableInfoTest.cpp
    reflection/TypeNameTest.cpp
    utils/ConstexprForTest.cpp
    utils/ConstexprStringViewTest.cpp
)

add_executable(orm-cxx-UT ${ORM_CXX_TEST_SOURCES})
set_target_properties(orm-cxx-UT PROPERTIES RUNTIME_OUTPUT_DIRECTORY "${PROJECT_BINARY_DIR}")
target_link_libraries(orm-cxx-UT PRIVATE gtest_main gmock_main orm-cxx::orm-cxx faker-cxx)
target_include_directories(orm-cxx-UT PRIVATE "${PROJECT_SOURCE_DIR}")

if(MSVC)
    set_source_files_properties(database/BackendConformanceTest.cpp PROPERTIES COMPILE_OPTIONS "/bigobj")
    set(orm_cxx_test_working_directory "${PROJECT_BINARY_DIR}/bin")
else()
    set(orm_cxx_test_working_directory "${PROJECT_SOURCE_DIR}")
endif()

add_test(
    NAME orm-cxx-UT
    COMMAND orm-cxx-UT --gtest_filter=-DatabaseTest/BackendConformanceTest.*
    WORKING_DIRECTORY "${orm_cxx_test_working_directory}"
)
add_test(
    NAME orm-cxx-sqlite-conformance
    COMMAND orm-cxx-UT --gtest_filter=DatabaseTest/BackendConformanceTest.*/Sqlite
    WORKING_DIRECTORY "${orm_cxx_test_working_directory}"
)
set_tests_properties(
    orm-cxx-UT
    PROPERTIES ENVIRONMENT "SOCI_BACKENDS_PATH=$<TARGET_FILE_DIR:soci_sqlite3>" LABELS "unit;backend-sqlite"
)
set_tests_properties(
    orm-cxx-sqlite-conformance
    PROPERTIES ENVIRONMENT "SOCI_BACKENDS_PATH=$<TARGET_FILE_DIR:soci_sqlite3>"
               LABELS "conformance;backend-sqlite"
               FAIL_REGULAR_EXPRESSION "\\[  PASSED  \\] 0 tests"
)

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
if(CMAKE_TOOLCHAIN_FILE)
    list(APPEND orm_cxx_consumer_common_configure_arguments "-DCMAKE_TOOLCHAIN_FILE=${CMAKE_TOOLCHAIN_FILE}")
endif()
if(VCPKG_INSTALLED_DIR)
    list(
        APPEND
        orm_cxx_consumer_common_configure_arguments
        "-DVCPKG_INSTALLED_DIR=${VCPKG_INSTALLED_DIR}"
        -DVCPKG_MANIFEST_INSTALL=OFF
    )
endif()
if(VCPKG_TARGET_TRIPLET)
    list(APPEND orm_cxx_consumer_common_configure_arguments "-DVCPKG_TARGET_TRIPLET=${VCPKG_TARGET_TRIPLET}")
endif()

set(orm_cxx_consumer_binary_dir "${PROJECT_BINARY_DIR}/consumer-test")
set(orm_cxx_consumer_configure_arguments
    ${orm_cxx_consumer_common_configure_arguments} -B "${orm_cxx_consumer_binary_dir}"
)

add_test(NAME orm-cxx-consumer-configure COMMAND "${CMAKE_COMMAND}" ${orm_cxx_consumer_configure_arguments})
add_test(NAME orm-cxx-consumer-build COMMAND "${CMAKE_COMMAND}" --build "${orm_cxx_consumer_binary_dir}" --config Debug)
set_tests_properties(orm-cxx-consumer-build PROPERTIES DEPENDS orm-cxx-consumer-configure)

add_test(
    NAME orm-cxx-consumer-run
    COMMAND "${orm_cxx_consumer_binary_dir}/bin/orm-cxx-consumer${CMAKE_EXECUTABLE_SUFFIX}"
    WORKING_DIRECTORY "${orm_cxx_consumer_binary_dir}/bin"
)
set_tests_properties(orm-cxx-consumer-run PROPERTIES DEPENDS orm-cxx-consumer-build)

set(orm_cxx_no_sqlite_consumer_binary_dir "${PROJECT_BINARY_DIR}/consumer-no-sqlite-test")
set(orm_cxx_no_sqlite_consumer_configure_arguments
    ${orm_cxx_consumer_common_configure_arguments}
    -B
    "${orm_cxx_no_sqlite_consumer_binary_dir}"
    -DORM_CXX_ENABLE_SQLITE_BACKEND=OFF
)

add_test(
    NAME orm-cxx-consumer-no-sqlite-configure
    COMMAND "${CMAKE_COMMAND}" ${orm_cxx_no_sqlite_consumer_configure_arguments}
)
add_test(
    NAME orm-cxx-consumer-no-sqlite-build
    COMMAND "${CMAKE_COMMAND}" --build "${orm_cxx_no_sqlite_consumer_binary_dir}" --config Debug
)
set_tests_properties(
    orm-cxx-consumer-no-sqlite-build PROPERTIES DEPENDS orm-cxx-consumer-no-sqlite-configure
)

add_test(
    NAME orm-cxx-consumer-no-sqlite-run
    COMMAND "${orm_cxx_no_sqlite_consumer_binary_dir}/bin/orm-cxx-consumer${CMAKE_EXECUTABLE_SUFFIX}"
    WORKING_DIRECTORY "${orm_cxx_no_sqlite_consumer_binary_dir}/bin"
)
set_tests_properties(orm-cxx-consumer-no-sqlite-run PROPERTIES DEPENDS orm-cxx-consumer-no-sqlite-build)
