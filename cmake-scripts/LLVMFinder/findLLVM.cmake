include(${SCRIPT_DIR}/LLVMFinder/loadLLVMConfig.cmake)
include(${SCRIPT_DIR}/LLVMFinder/loadLLVMModule.cmake)

set(AUTOINSTALLER_DIR ${SCRIPT_DIR}/AutoInstaller)
include(${AUTOINSTALLER_DIR}/installLLVM.cmake)

# find llvm-config. perfers to the one with version suffix, Ex:llvm-config-3.2
FUNCTION(find_LLVM_CONFIG_EXE LLVM_ROOT LLVM_RECOMMEND_VERSION)
  find_program(LLVM_CONFIG_EXE
  # TODO add PATCH version?
  NAMES "${LLVM_ROOT}/bin/llvm-config-${LLVM_RECOMMEND_VERSION}"
        "${LLVM_ROOT}/bin/llvm-config"
        "llvm-config-${LLVM_RECOMMEND_VERSION}"
        "llvm-config"
	)
  set(LLVM_CONFIG_EXE ${LLVM_CONFIG_EXE} PARENT_SCOPE)
ENDFUNCTION(find_LLVM_CONFIG_EXE)

# this function sets LLVM_CONFIG_EXE
FUNCTION(FIND_LLVM_CONFIG_EXE_OR_AUTOINSTALL LLVM_ROOT LLVM_RECOMMEND_VERSION
                  AUTOINSTALL)
  # this function set LLVM_CONFIG_EXE
  find_LLVM_CONFIG_EXE(${LLVM_ROOT} ${LLVM_RECOMMEND_VERSION})

  # In case of finds no LLVM, give user a hint to install LLVM
  if(${LLVM_CONFIG_EXE} STREQUAL "LLVM_CONFIG_EXE-NOTFOUND")
    if(${AUTOINSTALL})
      # if AUTOINSTALL is explicitly set to true, then run installLLVM.
      autoinstall_llvm(${LLVM_RECOMMEND_VERSION} ${LLVM_ROOT}
                     "${CMAKE_BINARY_DIR}/autoinstaller")
      set(AUTOINSTALL FALSE)
      find_LLVM_CONFIG_EXE(${LLVM_ROOT} ${LLVM_RECOMMEND_VERSION})
    else()
      # on condition that finds no LLVM and user not specify AUTOINSTALL.
      message(FATAL_ERROR "\tfinds no LLVM in your system.\n"
      	"\tPlease manually install LLVM.\n"
      	"\tOr\n"
      	"\t\"cmake /where/LLVM-Tracer -DAUTOINSTALL=TRUE\n"
	"\t\t-DLLVM_ROOT=/where/you/want/llvm/install/to\"\n"
      	"\twhich should automatically install LLVM for you"
      	" during cmake time.")
    endif()
  endif()
  set(LLVM_CONFIG_EXE ${LLVM_CONFIG_EXE} PARENT_SCOPE)
ENDFUNCTION(FIND_LLVM_CONFIG_EXE_OR_AUTOINSTALL)


# this function sets LLVM_CONFIG_EXE
FUNCTION(CHECK_LLVM_CONFIG_VERSION_OR_AUTOINSTALL LLVM_ROOT
                      LLVM_RECOMMEND_VERSION LLVM_CONFIG_EXE)
  message(STATUS "find LLVM-Config : ${LLVM_CONFIG_EXE}")
  execute_process(COMMAND ${LLVM_CONFIG_EXE} --version
		OUTPUT_VARIABLE LLVM_VERSION
		OUTPUT_STRIP_TRAILING_WHITESPACE)


  # Check whether the LLVM version meets our requirement.
  if(${LLVM_VERSION} MATCHES ^${LLVM_RECOMMEND_VERSION})
    message(STATUS "LLVM version : ${LLVM_VERSION}")
  else()
    if(${AUTOINSTALL})
      # if AUTOINSTALL is explicitly set to true, then run installLLVM.
      autoinstall_llvm(${LLVM_RECOMMEND_VERSION} ${LLVM_ROOT}
                     "${CMAKE_BINARY_DIR}/autoinstaller")
      set(AUTOINSTALL FALSE)
      find_LLVM_CONFIG_EXE(${LLVM_ROOT} ${LLVM_RECOMMEND_VERSION})
    else()
      message(FATAL_ERROR "LLVM version is recommanded to be : "
         "${LLVM_RECOMMEND_VERSION}\n"
         "Your current version is ${LLVM_VERSION}")
    endif()
  endif()
  set(LLVM_CONFIG_EXE ${LLVM_CONFIG_EXE} PARENT_SCOPE)
ENDFUNCTION(CHECK_LLVM_CONFIG_VERSION_OR_AUTOINSTALL)

# Configure these CMake Variables from llvm-config executable.
#
#    LLVM_ROOT
#    LLVM_LIBS
#    LLVM_DEFINITIONS
#    LLVM_VERSION
#    LLVM_INCLUDE_DIRS
#    LLVM_LIBRARY_DIRS
#
FUNCTION(LOAD_LLVM_SETTINGS LLVM_CONFIG_EXE NEED_LLVM_LIB_ARG)
  # In here. LLVM_CONFIG_EXE is found. We can get valid LLVM_ROOT.
  execute_process(COMMAND ${LLVM_CONFIG_EXE} --prefix
		OUTPUT_VARIABLE LLVM_ROOT
                OUTPUT_STRIP_TRAILING_WHITESPACE )

  get_filename_component(LLVM_BIN_DIR ${LLVM_CONFIG_EXE} PATH)
  get_filename_component(LLVM_ROOT ${LLVM_BIN_DIR} PATH)

  # try to load the CMake module of LLVM.
  loadLLVMModule(${LLVM_ROOT} ${NEED_LLVM_LIB_ARG})

  # Check whether LLVM package is found.
  if(NOT ${LLVM_FOUND})
    # if CMake module of LLVM is not found, we collect infomation
    # through llvm-config.
    loadLLVMConfig(${LLVM_CONFIG_EXE} ${NEED_LLVM_LIB_ARG})
    if(NOT ${LLVM_FOUND})
      message(FATAL_ERROR "(${LLVM_ROOT}) is not a valid LLVM install\n"
		  "You can explicitly specify your llvm_root by\n"
		  "\"cmake /where/LLVM-Tracer/is -DLLVM_ROOT=/my/llvm/install/dir\"\n"
		  "or make llvm-config visible in $PATH")
    endif()
  endif()

  set(LLVM_ROOT ${LLVM_ROOT} PARENT_SCOPE)
  set(LLVM_LIBS ${LLVM_LIBS} PARENT_SCOPE)
  set(LLVM_DEFINITIONS ${LLVM_DEFINITIONS} PARENT_SCOPE)
  set(LLVM_VERSION ${LLVM_VERSION} PARENT_SCOPE)
  set(LLVM_INCLUDE_DIRS ${LLVM_INCLUDE_DIRS} PARENT_SCOPE)
  set(LLVM_LIBRARY_DIRS ${LLVM_LIBRARY_DIRS} PARENT_SCOPE)
ENDFUNCTION(LOAD_LLVM_SETTINGS)


# This function returns these variable
#
# LLVM_ROOT
# LLVM_LIBS
# LLVM_DEFINITIONS
# LLVM_INCLUDE_DIRS
# LLVM_LIBRARY_DIRS

FUNCTION(FIND_LLVM LLVM_ROOT LLVM_RECOMMEND_VERSION NEED_LLVM_LIB_ARG
         AUTOINSTALL)
  # this function sets LLVM_CONFIG_EXE
  FIND_LLVM_CONFIG_EXE_OR_AUTOINSTALL(${LLVM_ROOT} ${LLVM_RECOMMEND_VERSION}
                    ${AUTOINSTALL})
  # this function sets LLVM_CONFIG_EXE
  CHECK_LLVM_CONFIG_VERSION_OR_AUTOINSTALL(${LLVM_ROOT}
                        ${LLVM_RECOMMEND_VERSION} ${LLVM_CONFIG_EXE})
  LOAD_LLVM_SETTINGS(${LLVM_CONFIG_EXE} ${NEED_LLVM_LIB_ARG})
  set(LLVM_ROOT ${LLVM_ROOT} PARENT_SCOPE)
  set(LLVM_LIBS ${LLVM_LIBS} PARENT_SCOPE)
  set(LLVM_DEFINITIONS ${LLVM_DEFINITIONS} PARENT_SCOPE)
  set(LLVM_INCLUDE_DIRS ${LLVM_INCLUDE_DIRS} PARENT_SCOPE)
  set(LLVM_LIBRARY_DIRS ${LLVM_LIBRARY_DIRS} PARENT_SCOPE)
ENDFUNCTION(FIND_LLVM)
