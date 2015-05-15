# In two-number form only. LLVM-Tracer will determine the patch version for you.
if(NOT DEFINED LLVM_RECOMMEND_VERSION)
  SET(LLVM_RECOMMEND_VERSION 3.4)
endif()

# TODO : not used now
# the llvm libraries which the executable need.
SET(NEED_LLVM_LIB mcjit native bitwriter jit interpreter
		nativecodegen linker irreader)

SET(CMAKE_CXX_FLAGS ${CMAKE_CXX_FLAGS} -Wall -W -Wno-unused-parameter
	-Wwrite-strings -pedantic -Wno-long-long -std=c++11)
STRING(REPLACE ";" " " CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS}")

# sets debug level to g3, which contains more infomation than g2.
# to use it : cmake /where/source/code -DCMAKE_BUILD_TYPE=DEBUG
set(CMAKE_CXX_FLAGS_DEBUG -g3)


if(NOT DEFINED TEST_CMAKE)
  SET(TEST_CMAKE FALSE)
endif()

SET(RECOMMAND_LLVM_PREFIX ${CMAKE_BINARY_DIR}/lib/llvm-${LLVM_RECOMMEND_VERSION})

if(NOT DEFINED BUILD_ON_SOURCE)
  SET(BUILD_ON_SOURCE TRUE)
endif()

if(NOT DEFINED AUTOINSTALL)
  SET(AUTOINSTALL FALSE)
endif()

if(DEFINED LLVM_ROOT)
  get_filename_component(LLVM_ROOT ${LLVM_ROOT} ABSOLUTE)
elseif("$ENV{LLVM_HOME}" STREQUAL "")
  set(LLVM_ROOT ${RECOMMAND_LLVM_PREFIX})
else()
  if(${AUTOINSTALL})
    message(FATAL_ERROR "please specify LLVM_ROOT explicitly if you ready want to autoinstall LLVM")
  endif()
  set(LLVM_ROOT "$ENV{LLVM_HOME}")
endif()

message(STATUS "use LLVM_ROOT : ${LLVM_ROOT}")

if(NOT DEFINED GEN_HEADER_DEPENDENCY)
  SET(GEN_HEADER_DEPENDENCY FALSE)
endif()
