cmake_minimum_required(VERSION 3.31)

project(orm-cxx-package-consumer LANGUAGES CXX)

find_package(orm-cxx CONFIG REQUIRED)

add_executable(orm-cxx-package-consumer main.cpp ValueTypesImport.cpp)
set_target_properties(orm-cxx-package-consumer PROPERTIES CXX_SCAN_FOR_MODULES ON)
target_link_libraries(orm-cxx-package-consumer PRIVATE orm-cxx::orm-cxx)
if(MSVC)
    # SOCI's public headers trigger DLL-interface warnings under /W4. Keep consumer warnings fatal while excluding these
    # third-party diagnostics.
    target_compile_options(orm-cxx-package-consumer PRIVATE /W4 /WX /wd4996 /wd4251 /wd4275)
else()
    target_compile_options(orm-cxx-package-consumer PRIVATE -Wall -Wextra -Wpedantic -Werror)
endif()

# CI supplies expectations independently of the package's generated configuration.
foreach(backend SQLITE POSTGRESQL MYSQL MARIADB)
    string(TOLOWER "${backend}" backend_lower)
    set(orm_cxx_expected_${backend_lower} std::nullopt)
    if(DEFINED ORM_CXX_EXPECT_${backend})
        if(ORM_CXX_EXPECT_${backend})
            set(orm_cxx_expected_${backend_lower} true)
        else()
            set(orm_cxx_expected_${backend_lower} false)
        endif()
    endif()
endforeach()
configure_file(BackendExpectations.hpp.in "${CMAKE_CURRENT_BINARY_DIR}/generated/BackendExpectations.hpp" @ONLY)
target_include_directories(orm-cxx-package-consumer PRIVATE "${CMAKE_CURRENT_BINARY_DIR}/generated")

enable_testing()
add_test(NAME orm-cxx-package-consumer COMMAND orm-cxx-package-consumer)
include(BackendCompileContracts.cmake)
orm_cxx_backend_compile_contracts(orm-cxx-package-consumer)
