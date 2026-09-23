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

add_library(orm-cxx-reflection INTERFACE)
add_library(orm-cxx::reflection ALIAS orm-cxx-reflection)
set_target_properties(orm-cxx-reflection PROPERTIES EXPORT_NAME reflection)
target_compile_features(orm-cxx-reflection INTERFACE cxx_std_20)
target_include_directories(
    orm-cxx-reflection INTERFACE "$<BUILD_INTERFACE:${PROJECT_SOURCE_DIR}/include>"
                                 "$<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>"
)

include(CMakePackageConfigHelpers)
configure_package_config_file(
    "${PROJECT_SOURCE_DIR}/cmake/orm-cxx-reflection-config.cmake.in"
    "${PROJECT_BINARY_DIR}/orm-cxx-reflection-config.cmake"
    INSTALL_DESTINATION "${CMAKE_INSTALL_LIBDIR}/cmake/orm-cxx-reflection"
)
write_basic_package_version_file(
    "${PROJECT_BINARY_DIR}/orm-cxx-reflection-config-version.cmake"
    VERSION "${PROJECT_VERSION}"
    COMPATIBILITY SameMinorVersion ARCH_INDEPENDENT
)

install(TARGETS orm-cxx-reflection EXPORT orm-cxx-reflection-targets)
install(DIRECTORY "${PROJECT_SOURCE_DIR}/include/orm-cxx/reflection" DESTINATION "${CMAKE_INSTALL_INCLUDEDIR}/orm-cxx")
install(
    EXPORT orm-cxx-reflection-targets
    FILE orm-cxx-reflection-targets.cmake
    NAMESPACE orm-cxx::
    DESTINATION "${CMAKE_INSTALL_LIBDIR}/cmake/orm-cxx-reflection"
)
install(FILES "${PROJECT_BINARY_DIR}/orm-cxx-reflection-config.cmake"
              "${PROJECT_BINARY_DIR}/orm-cxx-reflection-config-version.cmake"
        DESTINATION "${CMAKE_INSTALL_LIBDIR}/cmake/orm-cxx-reflection"
)
install(FILES "${PROJECT_SOURCE_DIR}/LICENSE" "${PROJECT_SOURCE_DIR}/THIRD_PARTY_NOTICES.md"
        DESTINATION "${CMAKE_INSTALL_DATADIR}/orm-cxx"
)

add_library(orm-cxx STATIC ${ORM_CXX_SOURCES})
add_library(orm-cxx::orm-cxx ALIAS orm-cxx)
if(NOT DEFINED CMAKE_POSITION_INDEPENDENT_CODE)
    set_target_properties(orm-cxx PROPERTIES POSITION_INDEPENDENT_CODE ON)
endif()

target_compile_features(orm-cxx PUBLIC cxx_std_20)
target_include_directories(
    orm-cxx PUBLIC "$<BUILD_INTERFACE:${PROJECT_SOURCE_DIR}/include>"
                   "$<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>"
)
target_compile_definitions(
    orm-cxx PUBLIC ORM_CXX_ENABLE_SQLITE_BACKEND=$<BOOL:${ORM_CXX_ENABLE_SQLITE_BACKEND}>
                   ORM_CXX_ENABLE_POSTGRESQL_BACKEND=$<BOOL:${ORM_CXX_ENABLE_POSTGRESQL_BACKEND}>
)
target_link_libraries(
    orm-cxx
    PUBLIC "$<BUILD_INTERFACE:${ORM_CXX_SOCI_CORE_TARGET}>" "$<INSTALL_INTERFACE:${ORM_CXX_SOCI_CORE_INSTALLED_TARGET}>"
           orm-cxx::reflection
    PRIVATE "$<BUILD_INTERFACE:orm-cxx-project-warnings>"
)

if(ORM_CXX_ENABLE_SQLITE_BACKEND)
    target_link_libraries(
        orm-cxx PUBLIC "$<BUILD_INTERFACE:${ORM_CXX_SOCI_SQLITE3_TARGET}>"
                       "$<INSTALL_INTERFACE:${ORM_CXX_SOCI_SQLITE3_INSTALLED_TARGET}>"
    )
    target_include_directories(orm-cxx PRIVATE "$<TARGET_PROPERTY:${ORM_CXX_SOCI_SQLITE3_TARGET},INCLUDE_DIRECTORIES>")
endif()

if(ORM_CXX_ENABLE_POSTGRESQL_BACKEND)
    target_link_libraries(
        orm-cxx PUBLIC "$<BUILD_INTERFACE:${ORM_CXX_SOCI_POSTGRESQL_TARGET}>"
                       "$<INSTALL_INTERFACE:${ORM_CXX_SOCI_POSTGRESQL_INSTALLED_TARGET}>"
    )
    target_include_directories(
        orm-cxx PRIVATE "$<TARGET_PROPERTY:${ORM_CXX_SOCI_POSTGRESQL_TARGET},INCLUDE_DIRECTORIES>"
    )
endif()

configure_package_config_file(
    "${PROJECT_SOURCE_DIR}/cmake/orm-cxx-config.cmake.in" "${PROJECT_BINARY_DIR}/orm-cxx-config.cmake"
    INSTALL_DESTINATION "${CMAKE_INSTALL_LIBDIR}/cmake/orm-cxx"
)
write_basic_package_version_file(
    "${PROJECT_BINARY_DIR}/orm-cxx-config-version.cmake"
    VERSION "${PROJECT_VERSION}"
    COMPATIBILITY SameMinorVersion
)

install(
    TARGETS orm-cxx
    EXPORT orm-cxx-targets
    ARCHIVE DESTINATION "${CMAKE_INSTALL_LIBDIR}"
)
install(
    DIRECTORY "${PROJECT_SOURCE_DIR}/include/orm-cxx"
    DESTINATION "${CMAKE_INSTALL_INCLUDEDIR}"
    PATTERN "reflection" EXCLUDE
)
install(
    EXPORT orm-cxx-targets
    FILE orm-cxx-targets.cmake
    NAMESPACE orm-cxx::
    DESTINATION "${CMAKE_INSTALL_LIBDIR}/cmake/orm-cxx"
)
install(FILES "${PROJECT_BINARY_DIR}/orm-cxx-config.cmake" "${PROJECT_BINARY_DIR}/orm-cxx-config-version.cmake"
        DESTINATION "${CMAKE_INSTALL_LIBDIR}/cmake/orm-cxx"
)
