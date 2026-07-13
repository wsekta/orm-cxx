add_executable(example main.cpp)
target_link_libraries(example PRIVATE orm-cxx::orm-cxx)

add_executable(relations-example relations.cpp)
target_link_libraries(relations-example PRIVATE orm-cxx::orm-cxx)
