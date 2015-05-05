set(FCTS "trace_logger")
file(GLOB SRC "*.c")

set(LLVMC_FLAGS ${LLVMC_FLAGS} "-O3")
build_llvm_bitcode(${FCTS} SRC)

add_custom_target(PROFILE_FUNC ALL DEPENDS ${FCTS})
