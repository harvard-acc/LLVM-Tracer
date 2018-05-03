set(FCTS "trace_logger")
file(GLOB SRC "*.cpp")

set(LLVMC_FLAGS ${LLVMC_FLAGS} "-O3" "-std=c++11" "-I${ZLIB_INCLUDE_DIRS}")

build_llvm_bitcode(${FCTS} SRC)

add_custom_target(PROFILE_FUNC ALL DEPENDS ${FCTS})
install(FILES ${CMAKE_CURRENT_BINARY_DIR}/${FCTS}.llvm DESTINATION lib)
#install(TARGETS PROFILE_FUNC DESTINATION lib)
