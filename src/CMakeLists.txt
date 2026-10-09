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
         database/sqlite/SqliteRelationStatements.cpp database/sqlite/SqliteTypeTranslator.cpp)
endif()
if(ORM_CXX_ENABLE_POSTGRESQL_BACKEND)
    list(APPEND ORM_CXX_SOURCES database/postgresql/PostgresqlBackend.cpp database/postgresql/PostgresqlDialect.cpp
         database/postgresql/PostgresqlTypeTranslator.cpp)
endif()

set(ORM_CXX_GENERATED_MODULE_DIR "${PROJECT_BINARY_DIR}/generated/modules")
file(MAKE_DIRECTORY "${ORM_CXX_GENERATED_MODULE_DIR}")
set(ORM_CXX_SQLITE_BACKEND_ENABLED false)
set(ORM_CXX_POSTGRESQL_BACKEND_ENABLED false)
set(ORM_CXX_SQLITE_RELATION_DECLARATIONS "")
if(ORM_CXX_ENABLE_SQLITE_BACKEND)
    set(ORM_CXX_SQLITE_BACKEND_ENABLED true)
    file(READ "${PROJECT_SOURCE_DIR}/cmake/SqliteRelationDeclarations.cppm.in" ORM_CXX_SQLITE_RELATION_DECLARATIONS)
endif()
if(ORM_CXX_ENABLE_POSTGRESQL_BACKEND)
    set(ORM_CXX_POSTGRESQL_BACKEND_ENABLED true)
endif()
configure_file("${PROJECT_SOURCE_DIR}/cmake/BuildConfig.cppm.in"
               "${ORM_CXX_GENERATED_MODULE_DIR}/orm.config.cppm" @ONLY)
configure_file("${PROJECT_SOURCE_DIR}/cmake/BackendRelations.cppm.in"
               "${ORM_CXX_GENERATED_MODULE_DIR}/orm.backend_relations.cppm" @ONLY)

add_library(orm-cxx-reflection STATIC)
add_library(orm-cxx::reflection ALIAS orm-cxx-reflection)
set_target_properties(orm-cxx-reflection PROPERTIES EXPORT_NAME reflection CXX_SCAN_FOR_MODULES ON)
target_compile_features(orm-cxx-reflection PUBLIC cxx_std_20)
target_sources(orm-cxx-reflection PUBLIC FILE_SET modules TYPE CXX_MODULES
    BASE_DIRS "${PROJECT_SOURCE_DIR}/modules"
    FILES "${PROJECT_SOURCE_DIR}/modules/orm.reflection.cppm"
          "${PROJECT_SOURCE_DIR}/modules/orm.reflection.generated.cppm")
target_link_libraries(orm-cxx-reflection PRIVATE "$<BUILD_INTERFACE:orm-cxx-project-warnings>")

add_library(orm-cxx STATIC ${ORM_CXX_SOURCES})
add_library(orm-cxx::orm-cxx ALIAS orm-cxx)
set_target_properties(orm-cxx PROPERTIES CXX_SCAN_FOR_MODULES ON)
if(NOT DEFINED CMAKE_POSITION_INDEPENDENT_CODE)
    set_target_properties(orm-cxx orm-cxx-reflection PROPERTIES POSITION_INDEPENDENT_CODE ON)
endif()
target_compile_features(orm-cxx PUBLIC cxx_std_20)
target_sources(orm-cxx PUBLIC FILE_SET modules TYPE CXX_MODULES
    BASE_DIRS "${PROJECT_SOURCE_DIR}/modules" "${ORM_CXX_GENERATED_MODULE_DIR}"
    FILES "${PROJECT_SOURCE_DIR}/modules/orm.cppm"
          "${PROJECT_SOURCE_DIR}/modules/orm.foundation.cppm"
          "${PROJECT_SOURCE_DIR}/modules/orm.model.cppm"
          "${PROJECT_SOURCE_DIR}/modules/orm.expressions.cppm"
          "${PROJECT_SOURCE_DIR}/modules/orm.dynamic_query.cppm"
          "${PROJECT_SOURCE_DIR}/modules/orm.static_plan.cppm"
          "${PROJECT_SOURCE_DIR}/modules/orm.sql.cppm"
          "${PROJECT_SOURCE_DIR}/modules/orm.database.cppm"
          "${PROJECT_SOURCE_DIR}/modules/orm.internal.cppm"
          "${ORM_CXX_GENERATED_MODULE_DIR}/orm.config.cppm"
          "${ORM_CXX_GENERATED_MODULE_DIR}/orm.backend_relations.cppm")
target_link_libraries(orm-cxx
    PUBLIC "$<BUILD_INTERFACE:${ORM_CXX_SOCI_CORE_TARGET}>"
           "$<INSTALL_INTERFACE:${ORM_CXX_SOCI_CORE_INSTALLED_TARGET}>" orm-cxx::reflection
    PRIVATE "$<BUILD_INTERFACE:orm-cxx-project-warnings>")
foreach(backend SQLITE3 POSTGRESQL)
    if(ORM_CXX_SOCI_${backend}_TARGET)
        target_link_libraries(orm-cxx
            PUBLIC "$<BUILD_INTERFACE:${ORM_CXX_SOCI_${backend}_TARGET}>"
                   "$<INSTALL_INTERFACE:${ORM_CXX_SOCI_${backend}_INSTALLED_TARGET}>")
        target_include_directories(orm-cxx PRIVATE "$<TARGET_PROPERTY:${ORM_CXX_SOCI_${backend}_TARGET},INCLUDE_DIRECTORIES>")
    endif()
endforeach()

include(CMakePackageConfigHelpers)
foreach(package orm-cxx orm-cxx-reflection)
    configure_package_config_file("${PROJECT_SOURCE_DIR}/cmake/${package}-config.cmake.in"
        "${PROJECT_BINARY_DIR}/${package}-config.cmake"
        INSTALL_DESTINATION "${CMAKE_INSTALL_LIBDIR}/cmake/${package}")
    write_basic_package_version_file("${PROJECT_BINARY_DIR}/${package}-config-version.cmake"
        VERSION "${PROJECT_VERSION}" COMPATIBILITY SameMinorVersion)
    install(TARGETS ${package} EXPORT ${package}-targets
        ARCHIVE DESTINATION "${CMAKE_INSTALL_LIBDIR}"
        FILE_SET modules DESTINATION "${CMAKE_INSTALL_DATADIR}/orm-cxx/modules"
        CXX_MODULES_BMI DESTINATION "")
    install(EXPORT ${package}-targets FILE ${package}-targets.cmake NAMESPACE orm-cxx::
        DESTINATION "${CMAKE_INSTALL_LIBDIR}/cmake/${package}" CXX_MODULES_DIRECTORY cxx-modules)
    install(FILES "${PROJECT_BINARY_DIR}/${package}-config.cmake"
                  "${PROJECT_BINARY_DIR}/${package}-config-version.cmake"
        DESTINATION "${CMAKE_INSTALL_LIBDIR}/cmake/${package}")
endforeach()
install(FILES "${PROJECT_SOURCE_DIR}/LICENSE" "${PROJECT_SOURCE_DIR}/THIRD_PARTY_NOTICES.md"
        DESTINATION "${CMAKE_INSTALL_DATADIR}/orm-cxx")
