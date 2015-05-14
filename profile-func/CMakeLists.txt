set(FCTS "trace_logger")
file(GLOB SRC "*.c")

set(LLVMC_FLAGS ${LLVMC_FLAGS} "-O3")

if(${BUILD_ON_SOURCE})
  build_llvm_bc(${FCTS} SRC ${CMAKE_CURRENT_SOURCE_DIR})
else()
  build_llvm_bitcode(${FCTS} SRC)
endif()

add_custom_target(PROFILE_FUNC ALL DEPENDS ${FCTS})
