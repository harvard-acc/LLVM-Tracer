# Configure these CMake Variables from CMake module of LLVM package
# 
#    LLVM_LIBS
#    LLVM_DEFINITIONS
#    LLVM_FOUND
#    LLVM_VERSION
#    LLVM_INCLUDE_DIRS
#    LLVM_LIBRARY_DIRS



FUNCTION(loadLLVMModule LLVM_ROOT NEED_LLVM_LIB_LIST)
  set(NEED_LLVM_LIB ${${NEED_LLVM_LIB_LIST}})
  # Make sure there is a LLVM_ROOT
  if(NOT DEFINED LLVM_ROOT)
    message(FATAL_ERROR "LLVM_ROOT should exists here. it is a bug."
                        " please contact authors.")
  endif()

  # We incorporate the CMake modules provided by LLVM:
  # Using include(foo.cmake) searches CMAKE_MODULE_PATH,
  # but find_package(bar) searches CMAKE_PREFIX_PATH
  # so we must append llvm cmake module dir to both variables.
  SET(CMAKE_MODULE_PATH "${LLVM_ROOT}/share/llvm/cmake")
  SET(CMAKE_PREFIX_PATH "${LLVM_ROOT}/share/llvm/cmake")

  # Not using version check in find_package is that llvm version
  # is followed by "svn" for official release, which is different
  # to the pure number of package from debian package pool. So\
  # checks version later.

  # The llvm's cmake module only contained in the LLVM which is build
  # through CMake. Package pool of most linux distributions do not contained
  # this module. This command try the CMake module of LLVM.
  find_package(LLVM QUIET)

  if(${LLVM_FOUND})
    if(${LLVM_RECOMMEND_VERSION} VERSION_LESS 3.5)
      llvm_map_components_to_libraries(LLVM_LIBS ${NEED_LLVM_LIB})
    else()
      llvm_map_components_to_libnames(LLVM_LIBS ${NEED_LLVM_LIB})
    endif()
  endif(${LLVM_FOUND})

  set(LLVM_FOUND ${LLVM_FOUND} PARENT_SCOPE)
  set(LLVM_LIBS ${LLVM_LIBS} PARENT_SCOPE)
  set(LLVM_DEFINITIONS ${LLVM_DEFINITIONS} PARENT_SCOPE)
  set(LLVM_VERSION ${LLVM_VERSION} PARENT_SCOPE)
  set(LLVM_INCLUDE_DIRS ${LLVM_INCLUDE_DIRS} PARENT_SCOPE)
  set(LLVM_LIBRARY_DIRS ${LLVM_LIBRARY_DIRS} PARENT_SCOPE)
ENDFUNCTION(loadLLVMModule)
