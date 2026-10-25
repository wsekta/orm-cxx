add_executable(migrations-example migrations.cpp)
set_target_properties(migrations-example PROPERTIES CXX_SCAN_FOR_MODULES ON)
target_link_libraries(migrations-example PRIVATE orm-cxx::orm-cxx)

add_executable(temporal-values-example temporal_values.cpp)
set_target_properties(temporal-values-example PROPERTIES CXX_SCAN_FOR_MODULES ON)
target_link_libraries(temporal-values-example PRIVATE orm-cxx::orm-cxx)

if(ORM_CXX_ENABLE_SQLITE_BACKEND
   OR ORM_CXX_ENABLE_POSTGRESQL_BACKEND
   OR ORM_CXX_ENABLE_MYSQL_BACKEND
   OR ORM_CXX_ENABLE_MARIADB_BACKEND
)
    add_executable(example main.cpp)
    set_target_properties(example PROPERTIES CXX_SCAN_FOR_MODULES ON)
    target_link_libraries(example PRIVATE orm-cxx::orm-cxx)

    add_executable(relations-example relations.cpp)
    set_target_properties(relations-example PROPERTIES CXX_SCAN_FOR_MODULES ON)
    target_link_libraries(relations-example PRIVATE orm-cxx::orm-cxx)

    add_executable(static-queries-example static_queries.cpp)
    set_target_properties(static-queries-example PROPERTIES CXX_SCAN_FOR_MODULES ON)
    target_link_libraries(static-queries-example PRIVATE orm-cxx::orm-cxx)
    add_executable(backend-connection-example backend_connection.cpp)
    set_target_properties(backend-connection-example PROPERTIES CXX_SCAN_FOR_MODULES ON)
    target_link_libraries(backend-connection-example PRIVATE orm-cxx::orm-cxx)
endif()
