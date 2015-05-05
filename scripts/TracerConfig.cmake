# In two-number form only. PinaVM will determine the patch version for you.
SET(LLVM_RECOMMAND_VERSION 3.5)

# TODO the llvm libraries which PinaVM need.
SET(NEED_LLVM_LIB mcjit native bitwriter jit interpreter
		nativecodegen linker irreader)

SET(CMAKE_CXX_FLAGS ${CMAKE_CXX_FLAGS} -Wall -W -Wno-unused-parameter
	-Wwrite-strings -pedantic -Wno-long-long -std=c++11)
STRING(REPLACE ";" " " CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS}")

# sets debug level to g3, which contains more infomation than g2.
# to use it : cmake /pinavm/source/ -DCMAKE_BUILD_TYPE=DEBUG
set(CMAKE_CXX_FLAGS_DEBUG -g3)


if(NOT DEFINED TEST_CMAKE)
  SET(TEST_CMAKE FALSE)
endif()

SET(RECOMMAND_LLVM_PREFIX ${CMAKE_BINARY_DIR}/lib/llvm-${LLVM_RECOMMAND_VERSION})

if(DEFINED LLVM_ROOT)
  get_filename_component(LLVM_ROOT ${LLVM_ROOT} ABSOLUTE)
elseif("$ENV{LLVM_HOME}" STREQUAL "")
  set(LLVM_ROOT ${RECOMMAND_LLVM_PREFIX})
else()
  set(LLVM_ROOT "$ENV{LLVM_HOME}")
endif()

message(STATUS "use LLVM_ROOT : ${LLVM_ROOT}")

if(NOT DEFINED AUTOINSTALL)
  SET(AUTOINSTALL FALSE)
endif()

if(NOT DEFINED GEN_HEADER_DEPENDENCY)
  SET(GEN_HEADER_DEPENDENCY FALSE)
endif()
