cmake_minimum_required(VERSION 3.22)

set(EXECUTABLE_NAME example)

set(CMAKE_CXX_STANDARD 20)

add_executable(${EXECUTABLE_NAME} main.cpp)

target_link_libraries(${EXECUTABLE_NAME} PRIVATE orm-cxx)