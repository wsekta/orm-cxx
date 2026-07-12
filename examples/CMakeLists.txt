cmake_minimum_required(VERSION 3.22)

set(CMAKE_CXX_STANDARD 20)

add_executable(example main.cpp)
target_link_libraries(example PRIVATE orm-cxx)

add_executable(relations-example relations.cpp)
target_link_libraries(relations-example PRIVATE orm-cxx)
