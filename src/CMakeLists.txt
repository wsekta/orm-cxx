set(ORM_CXX_SOURCES
    Database.cpp
    database/sqlite/SqliteTypeTranslator.cpp
    model/ColumnType.cpp
    database/CommandGeneratorFactory.cpp
    utils/StringUtils.cpp
    database/defaults/DefaultCreateTableCommand.cpp
    database/defaults/DefaultDropTableCommand.cpp
    database/defaults/DefaultInsertCommand.cpp
    database/defaults/DefaultSelectCommand.cpp
    database/defaults/DefaultUpdateCommand.cpp
    database/defaults/DefaultDeleteCommand.cpp
    database/defaults/SqlRenderer.cpp
    database/defaults/RelationStatements.cpp
    database/CommandGenerator.cpp
)

add_library(orm-cxx STATIC ${ORM_CXX_SOURCES})
add_library(orm-cxx::orm-cxx ALIAS orm-cxx)

target_compile_features(orm-cxx PUBLIC cxx_std_20)
target_include_directories(orm-cxx PUBLIC "$<BUILD_INTERFACE:${PROJECT_SOURCE_DIR}/include>")
target_link_libraries(
    orm-cxx
    PUBLIC soci_core soci_sqlite3 orm-cxx-reflect-cpp
    PRIVATE orm-cxx-project-warnings
)
