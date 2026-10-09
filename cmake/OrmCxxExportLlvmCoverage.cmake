foreach(
    required_variable
    LLVM_COV_EXECUTABLE
    LLVM_PROFDATA_EXECUTABLE
    PYTHON_EXECUTABLE
    SOURCE_ROOT
    TARGET_BINARY
    PROFILE_DATA
    OUTPUT_FILE
)
    if(NOT DEFINED ${required_variable} OR "${${required_variable}}" STREQUAL "")
        message(FATAL_ERROR "${required_variable} is required to export coverage")
    endif()
endforeach()

execute_process(
    COMMAND
        "${PYTHON_EXECUTABLE}" "${CMAKE_CURRENT_LIST_DIR}/../scripts/export_llvm_coverage.py" --llvm-cov
        ${LLVM_COV_EXECUTABLE} --llvm-profdata ${LLVM_PROFDATA_EXECUTABLE} --target-binary "${TARGET_BINARY}"
        --profile-data "${PROFILE_DATA}" --source-root "${SOURCE_ROOT}" --output "${OUTPUT_FILE}"
        COMMAND_ERROR_IS_FATAL ANY
)
