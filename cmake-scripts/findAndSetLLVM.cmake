include(${SCRIPT_DIR}/LLVMFinder/findLLVM.cmake)
include(${SCRIPT_DIR}/LLVMFinder/findLLVMTools.cmake)
include(${SCRIPT_DIR}/buildBitcode.cmake)
include(${SCRIPT_DIR}/buildTracerBitcode.cmake)

# This function returns these variable
#
# LLVM_ROOT
# LLVM_LIBS
# LLVM_DEFINITIONS
# LLVM_INCLUDE_DIRS
# LLVM_LIBRARY_DIRS
FIND_LLVM(${LLVM_ROOT} ${LLVM_RECOMMEND_VERSION} NEED_LLVM_LIB ${AUTOINSTALL})
findLLVMTool(LLVM_LINK "llvm-link")
findLLVMTool(LLVM_OPT "opt")
findLLVMTool(LLVM_LLC "llc")
findLLVMTool(LLVM_COMPILER "clang")

set(LLVMC_FLAGS ${LLVM_DEFINITIONS}
	-fno-inline-functions -fno-use-cxa-atexit)
set(LLVM_OPT_FLAGS )

# Use settings from LLVM cmake module or llvm-config.
include_directories( ${LLVM_INCLUDE_DIRS} )
link_directories( ${LLVM_LIBRARY_DIRS} )
add_definitions( ${LLVM_DEFINITIONS} )

# remove the dot of LLVM_VERSION and pass it as macro
string(REPLACE "." "" LLVM_NO_DOT_VERSION "${LLVM_RECOMMEND_VERSION}")
add_definitions("-DLLVM_VERSION=${LLVM_NO_DOT_VERSION}")

# probably some llvm-config work to do...
SET(REQ_LLVM_LIBRARIES ${LLVM_LIBS})

# For debug use only
if(${TEST_CMAKE})
  message("debug messages below")
  message("LLVM_ROOT is ${LLVM_ROOT}")
  message("REQ_LLVM_LIBRARIES is ${REQ_LLVM_LIBRARIES}")
  message("LLVM_COMPILER is ${LLVM_COMPILER}")
  message("LLVMC_FLAGS is ${LLVMC_FLAGS}")
  message("LLVM_DEFINITIONS is ${LLVM_DEFINITIONS}")
  message("LLVM_LIBRARY_DIRS is ${LLVM_LIBRARY_DIRS}")
  message("LLVM_INCLUDE_DIRS is ${LLVM_INCLUDE_DIRS}")
endif()
