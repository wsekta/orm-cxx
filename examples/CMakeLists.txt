if(ORM_CXX_ENABLE_SQLITE_BACKEND)
    add_executable(example main.cpp)
    target_link_libraries(example PRIVATE orm-cxx::orm-cxx)

    add_executable(relations-example relations.cpp)
    target_link_libraries(relations-example PRIVATE orm-cxx::orm-cxx)
endif()

if(ORM_CXX_ENABLE_POSTGRESQL_BACKEND)
    add_executable(postgresql-example postgresql.cpp)
    target_link_libraries(postgresql-example PRIVATE orm-cxx::orm-cxx)
endif()
