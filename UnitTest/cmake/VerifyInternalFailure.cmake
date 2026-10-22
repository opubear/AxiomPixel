execute_process(COMMAND "${PIXEL_TEST_PROGRAM}" "${PIXEL_FAILURE_CASE}"
    RESULT_VARIABLE result OUTPUT_VARIABLE output ERROR_VARIABLE error)
if(NOT result STREQUAL "86")
    message(FATAL_ERROR "Expected uncaught internal failure, got '${result}':\n${output}\n${error}")
endif()
if(NOT error MATCHES "${PIXEL_EXPECTED_ERROR}")
    message(FATAL_ERROR "Failed for an unrelated reason:\n${output}\n${error}")
endif()
message(STATUS "${PIXEL_FAILURE_CASE} propagated the expected internal failure")
