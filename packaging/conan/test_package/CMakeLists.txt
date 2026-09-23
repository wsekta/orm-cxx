cmake_minimum_required(VERSION 3.22)
project(orm-cxx-test-package LANGUAGES CXX)

find_package(orm-cxx CONFIG REQUIRED)

add_executable(test_package test_package.cpp)
target_compile_features(test_package PRIVATE cxx_std_20)
target_link_libraries(test_package PRIVATE orm-cxx::orm-cxx)
target_compile_definitions(
    test_package PRIVATE ORM_CXX_EXPECT_SQLITE=$<BOOL:${ORM_CXX_EXPECT_SQLITE}>
                         ORM_CXX_EXPECT_POSTGRESQL=$<BOOL:${ORM_CXX_EXPECT_POSTGRESQL}>
)

add_executable(test_reflection test_reflection.cpp)
target_compile_features(test_reflection PRIVATE cxx_std_20)
target_link_libraries(test_reflection PRIVATE orm-cxx::reflection)
