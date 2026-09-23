cmake_minimum_required(VERSION 3.22)

project(orm-cxx-package-consumer LANGUAGES CXX)

find_package(orm-cxx CONFIG REQUIRED)

add_executable(orm-cxx-package-consumer main.cpp)
target_link_libraries(orm-cxx-package-consumer PRIVATE orm-cxx::orm-cxx)

# CI supplies these expectations independently of the package's exported macros.
if(DEFINED ORM_CXX_EXPECT_SQLITE)
    target_compile_definitions(orm-cxx-package-consumer PRIVATE ORM_CXX_EXPECT_SQLITE=$<BOOL:${ORM_CXX_EXPECT_SQLITE}>)
endif()
if(DEFINED ORM_CXX_EXPECT_POSTGRESQL)
    target_compile_definitions(
        orm-cxx-package-consumer PRIVATE ORM_CXX_EXPECT_POSTGRESQL=$<BOOL:${ORM_CXX_EXPECT_POSTGRESQL}>
    )
endif()

enable_testing()
add_test(NAME orm-cxx-package-consumer COMMAND orm-cxx-package-consumer)
