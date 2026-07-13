foreach(required_variable LLVM_COV_EXECUTABLE TARGET_BINARY PROFILE_DATA OUTPUT_FILE)
    if(NOT DEFINED ${required_variable} OR "${${required_variable}}" STREQUAL "")
        message(FATAL_ERROR "${required_variable} is required to export coverage")
    endif()
endforeach()

execute_process(
    COMMAND
        ${LLVM_COV_EXECUTABLE} export "${TARGET_BINARY}" "-instr-profile=${PROFILE_DATA}" -format=lcov --skip-branches
        "-ignore-filename-regex=.*/externals/.*" "-ignore-filename-regex=.*/tests/.*"
        "-ignore-filename-regex=.*/src/.*Test\\.cpp"
    OUTPUT_FILE "${OUTPUT_FILE}" COMMAND_ERROR_IS_FATAL ANY
)
