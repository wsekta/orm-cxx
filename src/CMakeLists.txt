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

add_library(orm-cxx STATIC ${ORM_CXX_SOURCES})
add_library(orm-cxx::orm-cxx ALIAS orm-cxx)

target_compile_features(orm-cxx PUBLIC cxx_std_20)
target_include_directories(orm-cxx PUBLIC "$<BUILD_INTERFACE:${PROJECT_SOURCE_DIR}/include>")
target_compile_definitions(
    orm-cxx PUBLIC ORM_CXX_ENABLE_SQLITE_BACKEND=$<BOOL:${ORM_CXX_ENABLE_SQLITE_BACKEND}>
                   ORM_CXX_ENABLE_POSTGRESQL_BACKEND=$<BOOL:${ORM_CXX_ENABLE_POSTGRESQL_BACKEND}>
)
target_link_libraries(
    orm-cxx
    PUBLIC soci_core orm-cxx-reflect-cpp
    PRIVATE orm-cxx-project-warnings
)

if(ORM_CXX_ENABLE_SQLITE_BACKEND)
    target_link_libraries(orm-cxx PUBLIC soci_sqlite3)
    target_include_directories(orm-cxx PRIVATE "$<TARGET_PROPERTY:soci_sqlite3,INCLUDE_DIRECTORIES>")
endif()

if(ORM_CXX_ENABLE_POSTGRESQL_BACKEND)
    target_link_libraries(orm-cxx PUBLIC soci_postgresql)
    target_include_directories(orm-cxx PRIVATE "$<TARGET_PROPERTY:soci_postgresql,INCLUDE_DIRECTORIES>")
endif()
