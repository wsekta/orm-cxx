if(NOT ORM_CXX_BUILD_DIR OR NOT ORM_CXX_CASE_MANIFEST)
    message(FATAL_ERROR "The compile-contract runner requires a build directory and case manifest.")
endif()

include("${ORM_CXX_CASE_MANIFEST}")

function(orm_cxx_build_contract target result_variable output_variable)
    set(build_command "${CMAKE_COMMAND}" --build "${ORM_CXX_BUILD_DIR}" --target "${target}" --parallel 1)
    if(ORM_CXX_BUILD_CONFIG)
        list(APPEND build_command --config "${ORM_CXX_BUILD_CONFIG}")
    endif()
    execute_process(
        COMMAND ${build_command}
        RESULT_VARIABLE build_result
        OUTPUT_VARIABLE build_stdout
        ERROR_VARIABLE build_stderr
    )
    set(${result_variable} "${build_result}" PARENT_SCOPE)
    set(${output_variable} "${build_stdout}\n${build_stderr}" PARENT_SCOPE)
endfunction()

# Build real positive consumers first. A missing module, BMI or dependency must
# fail the suite before any expected-failure source is evaluated.
orm_cxx_build_contract(orm-cxx-compile-controls controls_result controls_output)
if(NOT controls_result EQUAL 0)
    message(FATAL_ERROR "Module import controls failed to build.\n${controls_output}")
endif()

foreach(case IN LISTS orm_cxx_failure_cases)
    string(REPLACE "|" ";" fields "${case}")
    list(GET fields 0 target)
    list(GET fields 1 diagnostic)
    orm_cxx_build_contract("${target}" build_result compiler_output)
    if(build_result EQUAL 0)
        message(FATAL_ERROR "${target} unexpectedly compiled successfully.")
    endif()
    # Several deliberately removed overloads have compiler-dependent diagnostics
    # and identify their source file. Never let a module graph failure satisfy
    # one of those broader checks.
    string(TOLOWER "${compiler_output}" lowercase_output)
    if(lowercase_output MATCHES "module[^\n]*(not found|cannot find|could not find|failed to load|unable to load)"
       OR lowercase_output MATCHES "failed to read compiled module|module file[^\n]*(out of date|not found)"
       OR lowercase_output MATCHES "module provider[^\n]*(not found|could not be found)"
       OR lowercase_output MATCHES "cannot open include file|fatal error:.*file not found")
        message(FATAL_ERROR "${target} failed because a module or dependency was unavailable.\n${compiler_output}")
    endif()
    string(FIND "${compiler_output}" "${diagnostic}" diagnostic_position)
    if(diagnostic_position EQUAL -1)
        message(FATAL_ERROR "${target} failed without the required diagnostic '${diagnostic}'.\n${compiler_output}")
    endif()
endforeach()

list(LENGTH orm_cxx_failure_cases failure_count)
message(STATUS "Verified ${failure_count} negative compile contracts and all positive module import controls.")
