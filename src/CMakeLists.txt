set(ORM_CXX_SOURCES
    Database.cpp
    model/ColumnType.cpp
    database/CommandGeneratorFactory.cpp
    utils/StringUtils.cpp
    database/defaults/DefaultCreateTableCommand.cpp
    database/defaults/DefaultCommandGeneratorFactory.cpp
    database/defaults/DefaultDropTableCommand.cpp
    database/defaults/DefaultInsertCommand.cpp
    database/defaults/DefaultSelectCommand.cpp
    database/defaults/DefaultUpdateCommand.cpp
    database/defaults/DefaultDeleteCommand.cpp
    database/defaults/SqlRenderer.cpp
    database/defaults/RelationStatements.cpp
    database/CommandGenerator.cpp
)

if(ORM_CXX_ENABLE_SQLITE_BACKEND)
    list(APPEND ORM_CXX_SOURCES database/sqlite/SqliteBackend.cpp database/sqlite/SqliteDialect.cpp
         database/sqlite/SqliteRelationStatements.cpp database/sqlite/SqliteTypeTranslator.cpp
    )
endif()
if(ORM_CXX_ENABLE_POSTGRESQL_BACKEND)
    list(APPEND ORM_CXX_SOURCES database/postgresql/PostgresqlBackend.cpp database/postgresql/PostgresqlDialect.cpp
         database/postgresql/PostgresqlTypeTranslator.cpp
    )
endif()

set(ORM_CXX_GENERATED_MODULE_DIR "${PROJECT_BINARY_DIR}/generated/modules")
file(MAKE_DIRECTORY "${ORM_CXX_GENERATED_MODULE_DIR}")
set(ORM_CXX_SQLITE_BACKEND_ENABLED false)
set(ORM_CXX_POSTGRESQL_BACKEND_ENABLED false)
set(ORM_CXX_SQLITE_RELATION_DECLARATIONS "")
set_property(
    DIRECTORY
    APPEND
    PROPERTY CMAKE_CONFIGURE_DEPENDS "${PROJECT_SOURCE_DIR}/cmake/SqliteBackendDeclarations.cppm.in"
             "${PROJECT_SOURCE_DIR}/cmake/PostgresqlBackendDeclarations.cppm.in"
             "${PROJECT_SOURCE_DIR}/cmake/SqliteRelationDeclarations.cppm.in"
)
file(READ "${PROJECT_SOURCE_DIR}/cmake/SqliteBackendDeclarations.cppm.in" ORM_CXX_SQLITE_BACKEND_DECLARATIONS)
file(READ "${PROJECT_SOURCE_DIR}/cmake/PostgresqlBackendDeclarations.cppm.in" ORM_CXX_POSTGRESQL_BACKEND_DECLARATIONS)
if(ORM_CXX_ENABLE_SQLITE_BACKEND)
    set(ORM_CXX_SQLITE_BACKEND_ENABLED true)
    file(READ "${PROJECT_SOURCE_DIR}/cmake/SqliteRelationDeclarations.cppm.in" ORM_CXX_SQLITE_RELATION_DECLARATIONS)
else()
    set(ORM_CXX_SQLITE_BACKEND_DECLARATIONS "namespace orm::db::sqlite { class SqliteBackend; }")
endif()
if(ORM_CXX_ENABLE_POSTGRESQL_BACKEND)
    set(ORM_CXX_POSTGRESQL_BACKEND_ENABLED true)
else()
    set(ORM_CXX_POSTGRESQL_BACKEND_DECLARATIONS "namespace orm::db::postgresql { class PostgresqlBackend; }")
endif()
configure_file(
    "${PROJECT_SOURCE_DIR}/cmake/BuildConfig.cppm.in" "${ORM_CXX_GENERATED_MODULE_DIR}/orm.config.cppm" @ONLY
)
configure_file(
    "${PROJECT_SOURCE_DIR}/cmake/BackendRelations.cppm.in" "${ORM_CXX_GENERATED_MODULE_DIR}/orm.backend_relations.cppm"
    @ONLY
)

add_library(orm-cxx-reflection STATIC)
add_library(orm-cxx::reflection ALIAS orm-cxx-reflection)
set_target_properties(orm-cxx-reflection PROPERTIES EXPORT_NAME reflection CXX_SCAN_FOR_MODULES ON)
target_compile_features(orm-cxx-reflection PUBLIC cxx_std_20)
# GCC's lazy module reader corrupts this partition graph. Apply the option to producers, importers, and rebuilt
# installed interfaces through the public compile options.
target_compile_options(orm-cxx-reflection PUBLIC "$<$<CXX_COMPILER_ID:GNU>:-fno-module-lazy>")
target_sources(
    orm-cxx-reflection
    PUBLIC FILE_SET
           modules
           TYPE
           CXX_MODULES
           BASE_DIRS
           "${PROJECT_SOURCE_DIR}/modules"
           FILES
           "${PROJECT_SOURCE_DIR}/modules/orm.reflection.cppm"
           "${PROJECT_SOURCE_DIR}/modules/reflection/aggregate.cppm"
           "${PROJECT_SOURCE_DIR}/modules/reflection/binding_traits.cppm"
           "${PROJECT_SOURCE_DIR}/modules/reflection/compiler_signatures.cppm"
           "${PROJECT_SOURCE_DIR}/modules/reflection/field_metadata.cppm"
           "${PROJECT_SOURCE_DIR}/modules/reflection/fixed_string.cppm"
           "${PROJECT_SOURCE_DIR}/modules/reflection/generated/arity_0_7.cppm"
           "${PROJECT_SOURCE_DIR}/modules/reflection/generated/arity_100_104.cppm"
           "${PROJECT_SOURCE_DIR}/modules/reflection/generated/arity_105_109.cppm"
           "${PROJECT_SOURCE_DIR}/modules/reflection/generated/arity_110_113.cppm"
           "${PROJECT_SOURCE_DIR}/modules/reflection/generated/arity_114_117.cppm"
           "${PROJECT_SOURCE_DIR}/modules/reflection/generated/arity_118_121.cppm"
           "${PROJECT_SOURCE_DIR}/modules/reflection/generated/arity_122_125.cppm"
           "${PROJECT_SOURCE_DIR}/modules/reflection/generated/arity_126_127.cppm"
           "${PROJECT_SOURCE_DIR}/modules/reflection/generated/arity_128_128.cppm"
           "${PROJECT_SOURCE_DIR}/modules/reflection/generated/arity_16_23.cppm"
           "${PROJECT_SOURCE_DIR}/modules/reflection/generated/arity_24_31.cppm"
           "${PROJECT_SOURCE_DIR}/modules/reflection/generated/arity_32_39.cppm"
           "${PROJECT_SOURCE_DIR}/modules/reflection/generated/arity_40_47.cppm"
           "${PROJECT_SOURCE_DIR}/modules/reflection/generated/arity_48_55.cppm"
           "${PROJECT_SOURCE_DIR}/modules/reflection/generated/arity_56_63.cppm"
           "${PROJECT_SOURCE_DIR}/modules/reflection/generated/arity_64_70.cppm"
           "${PROJECT_SOURCE_DIR}/modules/reflection/generated/arity_71_77.cppm"
           "${PROJECT_SOURCE_DIR}/modules/reflection/generated/arity_78_83.cppm"
           "${PROJECT_SOURCE_DIR}/modules/reflection/generated/arity_84_89.cppm"
           "${PROJECT_SOURCE_DIR}/modules/reflection/generated/arity_8_15.cppm"
           "${PROJECT_SOURCE_DIR}/modules/reflection/generated/arity_90_94.cppm"
           "${PROJECT_SOURCE_DIR}/modules/reflection/generated/arity_95_99.cppm"
           "${PROJECT_SOURCE_DIR}/modules/reflection/generated.cppm"
           "${PROJECT_SOURCE_DIR}/modules/reflection/storage.cppm"
           "${PROJECT_SOURCE_DIR}/modules/reflection/type_names.cppm"
           "${PROJECT_SOURCE_DIR}/modules/reflection/value_names.cppm"
           "${PROJECT_SOURCE_DIR}/modules/reflection/visitation.cppm"
)
target_link_libraries(orm-cxx-reflection PRIVATE "$<BUILD_INTERFACE:orm-cxx-project-warnings>")

add_library(orm-cxx STATIC ${ORM_CXX_SOURCES})
add_library(orm-cxx::orm-cxx ALIAS orm-cxx)
set_target_properties(orm-cxx PROPERTIES CXX_SCAN_FOR_MODULES ON)
if(NOT DEFINED CMAKE_POSITION_INDEPENDENT_CODE)
    set_target_properties(orm-cxx orm-cxx-reflection PROPERTIES POSITION_INDEPENDENT_CODE ON)
endif()
target_compile_features(orm-cxx PUBLIC cxx_std_20)
target_compile_options(orm-cxx PUBLIC "$<$<CXX_COMPILER_ID:GNU>:-fno-module-lazy>")
target_sources(
    orm-cxx
    PUBLIC FILE_SET
           modules
           TYPE
           CXX_MODULES
           BASE_DIRS
           "${PROJECT_SOURCE_DIR}/modules"
           "${ORM_CXX_GENERATED_MODULE_DIR}"
           FILES
           "${PROJECT_SOURCE_DIR}/modules/orm.cppm"
           "${PROJECT_SOURCE_DIR}/modules/orm/database/connection.cppm"
           "${PROJECT_SOURCE_DIR}/modules/orm/database/context.cppm"
           "${PROJECT_SOURCE_DIR}/modules/orm/database/numeric.cppm"
           "${PROJECT_SOURCE_DIR}/modules/orm/database/object_binding.cppm"
           "${PROJECT_SOURCE_DIR}/modules/orm/database/payload.cppm"
           "${PROJECT_SOURCE_DIR}/modules/orm/database/result_binding.cppm"
           "${PROJECT_SOURCE_DIR}/modules/orm/database/soci.cppm"
           "${PROJECT_SOURCE_DIR}/modules/orm/database.cppm"
           "${PROJECT_SOURCE_DIR}/modules/orm/dynamic_query/projection.cppm"
           "${PROJECT_SOURCE_DIR}/modules/orm/dynamic_query/select.cppm"
           "${PROJECT_SOURCE_DIR}/modules/orm/dynamic_query/update.cppm"
           "${PROJECT_SOURCE_DIR}/modules/orm/dynamic_query.cppm"
           "${PROJECT_SOURCE_DIR}/modules/orm/expressions.cppm"
           "${PROJECT_SOURCE_DIR}/modules/orm/foundation/relation_collections.cppm"
           "${PROJECT_SOURCE_DIR}/modules/orm/foundation/relation_descriptors.cppm"
           "${PROJECT_SOURCE_DIR}/modules/orm/foundation/types.cppm"
           "${PROJECT_SOURCE_DIR}/modules/orm/foundation/utilities.cppm"
           "${PROJECT_SOURCE_DIR}/modules/orm/foundation.cppm"
           "${PROJECT_SOURCE_DIR}/modules/orm/internal.cppm"
           "${PROJECT_SOURCE_DIR}/modules/orm/model/columns.cppm"
           "${PROJECT_SOURCE_DIR}/modules/orm/model/mapping.cppm"
           "${PROJECT_SOURCE_DIR}/modules/orm/model/relations.cppm"
           "${PROJECT_SOURCE_DIR}/modules/orm/model/schema.cppm"
           "${PROJECT_SOURCE_DIR}/modules/orm/model/views.cppm"
           "${PROJECT_SOURCE_DIR}/modules/orm/model.cppm"
           "${PROJECT_SOURCE_DIR}/modules/orm/query/aggregate.cppm"
           "${PROJECT_SOURCE_DIR}/modules/orm/query/columns.cppm"
           "${PROJECT_SOURCE_DIR}/modules/orm/query/expression.cppm"
           "${PROJECT_SOURCE_DIR}/modules/orm/query/parameters.cppm"
           "${PROJECT_SOURCE_DIR}/modules/orm/query/projection.cppm"
           "${PROJECT_SOURCE_DIR}/modules/orm/query/runtime_predicate.cppm"
           "${PROJECT_SOURCE_DIR}/modules/orm/query/value.cppm"
           "${PROJECT_SOURCE_DIR}/modules/orm/sql/commands.cppm"
           "${PROJECT_SOURCE_DIR}/modules/orm/sql/compiled.cppm"
           "${PROJECT_SOURCE_DIR}/modules/orm/sql/contracts.cppm"
           "${PROJECT_SOURCE_DIR}/modules/orm/sql/emitter.cppm"
           "${PROJECT_SOURCE_DIR}/modules/orm/sql/program.cppm"
           "${PROJECT_SOURCE_DIR}/modules/orm/sql.cppm"
           "${PROJECT_SOURCE_DIR}/modules/orm/static_plan.cppm"
           "${ORM_CXX_GENERATED_MODULE_DIR}/orm.config.cppm"
           "${ORM_CXX_GENERATED_MODULE_DIR}/orm.backend_relations.cppm"
)
target_link_libraries(
    orm-cxx
    PUBLIC "$<BUILD_INTERFACE:${ORM_CXX_SOCI_CORE_TARGET}>" "$<INSTALL_INTERFACE:${ORM_CXX_SOCI_CORE_INSTALLED_TARGET}>"
           orm-cxx::reflection
    PRIVATE "$<BUILD_INTERFACE:orm-cxx-project-warnings>"
)
foreach(backend SQLITE3 POSTGRESQL)
    if(ORM_CXX_SOCI_${backend}_TARGET)
        target_link_libraries(
            orm-cxx PUBLIC "$<BUILD_INTERFACE:${ORM_CXX_SOCI_${backend}_TARGET}>"
                           "$<INSTALL_INTERFACE:${ORM_CXX_SOCI_${backend}_INSTALLED_TARGET}>"
        )
        target_include_directories(
            orm-cxx
            PRIVATE "$<BUILD_INTERFACE:$<TARGET_PROPERTY:${ORM_CXX_SOCI_${backend}_TARGET},INCLUDE_DIRECTORIES>>"
        )
    endif()
endforeach()
install(FILES "${PROJECT_SOURCE_DIR}/modules/detail/SociTypes.inc"
        DESTINATION "${CMAKE_INSTALL_DATADIR}/orm-cxx/modules/detail"
)

include(CMakePackageConfigHelpers)
foreach(package orm-cxx orm-cxx-reflection)
    get_target_property(ORM_CXX_MODULE_MSVC_RUNTIME_LIBRARY ${package} MSVC_RUNTIME_LIBRARY)
    if(ORM_CXX_MODULE_MSVC_RUNTIME_LIBRARY STREQUAL "ORM_CXX_MODULE_MSVC_RUNTIME_LIBRARY-NOTFOUND")
        set(ORM_CXX_MODULE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>DLL")
    endif()
    configure_package_config_file(
        "${PROJECT_SOURCE_DIR}/cmake/${package}-config.cmake.in" "${PROJECT_BINARY_DIR}/${package}-config.cmake"
        INSTALL_DESTINATION "${CMAKE_INSTALL_LIBDIR}/cmake/${package}"
    )
    write_basic_package_version_file(
        "${PROJECT_BINARY_DIR}/${package}-config-version.cmake"
        VERSION "${PROJECT_VERSION}"
        COMPATIBILITY SameMinorVersion
    )
    install(
        TARGETS ${package}
        EXPORT ${package}-targets
        ARCHIVE DESTINATION "${CMAKE_INSTALL_LIBDIR}"
                FILE_SET modules
                DESTINATION "${CMAKE_INSTALL_DATADIR}/orm-cxx/modules"
                CXX_MODULES_BMI
                DESTINATION ""
    )
    install(
        EXPORT ${package}-targets
        FILE ${package}-targets.cmake
        NAMESPACE orm-cxx::
        DESTINATION "${CMAKE_INSTALL_LIBDIR}/cmake/${package}"
        CXX_MODULES_DIRECTORY cxx-modules
    )
    install(FILES "${PROJECT_BINARY_DIR}/${package}-config.cmake"
                  "${PROJECT_BINARY_DIR}/${package}-config-version.cmake"
            DESTINATION "${CMAKE_INSTALL_LIBDIR}/cmake/${package}"
    )
endforeach()
install(FILES "${PROJECT_SOURCE_DIR}/LICENSE" "${PROJECT_SOURCE_DIR}/THIRD_PARTY_NOTICES.md"
        DESTINATION "${CMAKE_INSTALL_DATADIR}/orm-cxx"
)
