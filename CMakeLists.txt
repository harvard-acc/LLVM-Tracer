cmake_minimum_required(VERSION 2.8.12)
project(LLVM_TRACER CXX)
enable_testing()

set(CMAKE_INSTALL_PREFIX $ENV{TRACER_HOME}/install)
set(SCRIPT_DIR "${CMAKE_CURRENT_SOURCE_DIR}/cmake-scripts")
include("${SCRIPT_DIR}/TracerConfig.cmake")
include("${SCRIPT_DIR}/findAndSetLLVM.cmake")

set(CMAKE_CXX_COMPILER ${CLANGXX})
option(DEBUG "Build in debugging mode (-O0, -g)" OFF)

add_subdirectory("${CMAKE_CURRENT_SOURCE_DIR}/full-trace")
add_subdirectory("${CMAKE_CURRENT_SOURCE_DIR}/profile-func")
add_subdirectory("${CMAKE_CURRENT_SOURCE_DIR}/ast-pass")
add_subdirectory("${CMAKE_CURRENT_SOURCE_DIR}/example")
