set(TEST_NAME triad)
set(SRC "${CMAKE_CURRENT_SOURCE_DIR}/${TEST_NAME}.c")
set(WORKLOAD "triad")
build_tracer_bitcode(${TEST_NAME} SRC ${WORKLOAD})

