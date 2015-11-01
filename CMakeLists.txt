cmake_minimum_required(VERSION 2.8.12)
project(LLVM_TRACER CXX)
enable_testing()

if(NOT DEFINED HAS_ALADDIN)
  SET(HAS_ALADDIN FALSE)
endif()

if (NOT ${HAS_ALADDIN})
  message(STATUS "not configure from aladdin,")
  set(SCRIPT_DIR "${CMAKE_CURRENT_SOURCE_DIR}/cmake-scripts")
  include("${SCRIPT_DIR}/TracerConfig.cmake")
  include("${SCRIPT_DIR}/findAndSetLLVM.cmake")
endif()

set(CMAKE_CXX_COMPILER ${CLANGXX})

add_subdirectory("${CMAKE_CURRENT_SOURCE_DIR}/full-trace")
add_subdirectory("${CMAKE_CURRENT_SOURCE_DIR}/profile-func")

if (NOT ${HAS_ALADDIN})
  add_subdirectory("${CMAKE_CURRENT_SOURCE_DIR}/example")
endif()
