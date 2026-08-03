cmake_minimum_required(VERSION 3.22)

project(orm-cxx-reflection-consumer LANGUAGES CXX)

find_package(orm-cxx-reflection CONFIG REQUIRED)

add_executable(orm-cxx-reflection-consumer main.cpp)
target_link_libraries(orm-cxx-reflection-consumer PRIVATE orm-cxx::reflection)
