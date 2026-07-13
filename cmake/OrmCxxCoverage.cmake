include_guard(GLOBAL)

function(orm_cxx_enable_coverage library_target test_target)
    if(NOT TARGET "${library_target}" OR NOT TARGET "${test_target}")
        message(FATAL_ERROR "Coverage requires existing ${library_target} and ${test_target} targets.")
    endif()

    # The bundled module retains the project's established LLVM/LCOV report layout. Keep its legacy switch scoped to
    # this function.
    set(CODE_COVERAGE ON)
    include("${CMAKE_CURRENT_FUNCTION_LIST_DIR}/cmake-coverage.cmake")

    if(CMAKE_CXX_COMPILER_ID MATCHES "(Apple)?Clang")
        # llvm-cov accepts regular expressions.
        set(coverage_excludes ".*/externals/.*" ".*/tests/.*" ".*/src/.*Test\\.cpp")
    elseif(CMAKE_CXX_COMPILER_ID STREQUAL "GNU")
        # lcov accepts shell-style glob patterns.
        set(coverage_excludes "*/externals/*" "*/tests/*" "*/src/*Test.cpp")
    endif()

    add_code_coverage_all_targets(EXCLUDE ${coverage_excludes})
    target_code_coverage("${library_target}")
    target_code_coverage("${test_target}" ALL)

    if(CMAKE_CXX_COMPILER_ID MATCHES "(Apple)?Clang")
        add_custom_target(
            orm-cxx-coverage
            COMMAND
                "${CMAKE_COMMAND}" "-DLLVM_COV_EXECUTABLE=${LLVM_COV_PATH}"
                "-DTARGET_BINARY=$<TARGET_FILE:${test_target}>"
                "-DPROFILE_DATA=${CMAKE_COVERAGE_OUTPUT_DIRECTORY}/all-merged.profdata"
                "-DOUTPUT_FILE=${CMAKE_BINARY_DIR}/coverage.lcov" -P
                "${CMAKE_CURRENT_FUNCTION_LIST_DIR}/OrmCxxExportLlvmCoverage.cmake"
            DEPENDS orm-cxx-ccov-all
            COMMENT "Exporting LLVM coverage to ${CMAKE_BINARY_DIR}/coverage.lcov"
            VERBATIM
        )
    elseif(CMAKE_CXX_COMPILER_ID STREQUAL "GNU")
        add_custom_target(
            orm-cxx-coverage
            COMMAND "${CMAKE_COMMAND}" -E copy_if_different "${CMAKE_COVERAGE_OUTPUT_DIRECTORY}/all-merged.info"
                    "${CMAKE_BINARY_DIR}/coverage.lcov"
            DEPENDS orm-cxx-ccov-all
            COMMENT "Exporting GCC coverage to ${CMAKE_BINARY_DIR}/coverage.lcov"
            VERBATIM
        )
    endif()

    add_custom_target(orm-cxx-coverage-lcov)
    add_dependencies(orm-cxx-coverage-lcov orm-cxx-coverage)
endfunction()
