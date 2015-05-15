# This script downloads, configure, build and install LLVM for you,
# and those actions are running during cmake time, so the drawback
# is that users may find it takes a long time to finish this cmake
# script.

# NOTE that this script requires tar and make command in your OS.

include(${AUTOINSTALLER_DIR}/LLVM_MD5.cmake)
include(${AUTOINSTALLER_DIR}/LLVMPatchVersion.cmake)

MACRO(configure_processor_count)
  # offer ProcessCount variable
  include(ProcessorCount)
  ProcessorCount(PROCESSOR_COUNT)
  if(NOT DEFINED PROCESSOR_COUNT)
      message(FATAL_ERROR "No PROCESSOR_COUNT defined")
  endif()
  if(PROCESSOR_COUNT EQUAL 0)
      message(FATAL_ERROR "PROCESSOR_COUNT = 0")
  endif()
ENDMACRO()

MACRO(find_tar)
  find_program(TAR_EXE NAMES tar)
  if(${TAR_EXE} STREQUAL "TAR_EXE-NOTFOUND")
    message(FATAL_ERROR "find no tar")
  endif()
ENDMACRO()

MACRO(find_xz TAR_EXE)
  execute_process(COMMAND ${TAR_EXE} --version
		OUTPUT_VARIABLE TAR_VERSION
		OUTPUT_STRIP_TRAILING_WHITESPACE)

  set(version_pattern "[0-9]*\\.[0-9]*\\.[0-9]*")
  string(REGEX MATCH ${version_pattern} TAR_VERSION "${TAR_VERSION}")

  # tar version newer than 1.22 has support to xz compression
  if(${TAR_VERSION} VERSION_LESS 1.22)
    message(FATAL_ERROR "tar version not less than 1.22 : ${TAR_VERSION}")
  else()
    message(STATUS "tar version : ${TAR_VERSION}")
  endif()

  find_program(XZ_EXE NAMES xz)
  if(${XZ_EXE} STREQUAL "XZ_EXE-NOTFOUND")
    message(FATAL_ERROR "find no xz")
  endif()
ENDMACRO()

MACRO(llvm_install_assert_arg LLVM_RECOMMEND_VERSION_ARG LLVM_ROOT_ARG)
  if (${LLVM_ROOT_ARG} STREQUAL "")
    message(FATAL_ERROR "LLVM_ROOT empty")
  endif()

  if (${LLVM_RECOMMEND_VERSION_ARG} STREQUAL "")
    message(FATAL_ERROR "LLVM_VERSION empty")
  endif()

  string(REPLACE "." ";" version_list "${LLVM_RECOMMEND_VERSION_ARG}")
  list(LENGTH version_list version_len)

  if(NOT (${version_len} EQUAL 2))
    message(FATAL_ERROR "only accept LLVM_VERSION in X.Y form")
  endif()

  if (${LLVM_RECOMMEND_VERSION_ARG} VERSION_LESS 3.1)
    message(FATAL_ERROR "Only Support LLVM version > 3.0, your version : "
                         "${LLVM_RECOMMEND_VERSION_ARG}")
  endif()
ENDMACRO()

MACRO(configure_autoinstall AUTOINSTALL_DIR)
  # sets the md5 checksum for certain version.
  decide_patch_version(${LLVM_RECOMMEND_VERSION_ARG})
  set_llvm_md5()
  find_tar()

  if(${TEST_CMAKE})
    # for test purpose, I don't want to waste bandwidth of llvm.org.
    SET(SITE_URL file://${CMAKE_SOURCE_DIR}/../source-tgz)
  else()
    # normal situation.
    SET(SITE_URL http://llvm.org/releases)
  endif()

  SET(DOWNLOAD_DIR ${AUTOINSTALL_DIR}/download)

  if (${LLVM_RECOMMEND_VERSION_ARG} VERSION_LESS "3.5")
    SET(URL_SUFFIX src.tar.gz)
    SET(COMPRESS_OPTION gzip)
  else()
    SET(URL_SUFFIX src.tar.xz)
    SET(COMPRESS_OPTION xz)
    find_xz(${TAR_EXE})
  endif()

  if (${LLVM_RECOMMEND_VERSION_ARG} VERSION_LESS "3.3")
    SET(CLANG_NAME_SHORT clang)
  else()
    SET(CLANG_NAME_SHORT cfe)
  endif()


  # set LLVM path names
  SET(LLVM_NAME         llvm-${LLVM_PATCH_VERSION}.${URL_SUFFIX})
  SET(LLVM_URL          ${SITE_URL}/${LLVM_PATCH_VERSION}/${LLVM_NAME})
  SET(LLVM_FILE         ${DOWNLOAD_DIR}/${LLVM_NAME})
  SET(LLVM_SOURCE_DIR   ${AUTOINSTALL_DIR}/llvm-source/llvm)
  SET(LLVM_MD5          ${LLVM_MD5_${LLVM_PATCH_VERSION}})


  # Clang uses cfe(C frontend) as its file name from version 3.3. In
  # order to make cmake of llvm automatically find and compile Clang,
  # a tools/clang directory is still needed.
  SET(CLANG_NAME       ${CLANG_NAME_SHORT}-${CLANG_PATCH_VERSION}.${URL_SUFFIX})
  SET(CLANG_URL        ${SITE_URL}/${CLANG_PATCH_VERSION}/${CLANG_NAME})
  SET(CLANG_FILE       ${DOWNLOAD_DIR}/${CLANG_NAME})
  SET(CLANG_SOURCE_DIR ${LLVM_SOURCE_DIR}/tools/clang)
  SET(CLANG_MD5        ${CLANG_MD5_${CLANG_PATCH_VERSION}})

  # set compiler-rt path names
  SET(COMPILER-RT_NAME compiler-rt-${RT_PATCH_VERSION}.${URL_SUFFIX})
  SET(COMPILER-RT_URL ${SITE_URL}/${RT_PATCH_VERSION}/${COMPILER-RT_NAME})
  SET(COMPILER-RT_FILE       ${DOWNLOAD_DIR}/${COMPILER-RT_NAME})
  SET(COMPILER-RT_SOURCE_DIR ${LLVM_SOURCE_DIR}/projects/compiler-rt)
  SET(COMPILER-RT_MD5        ${COMPILER-RT_MD5_${RT_PATCH_VERSION}})


  # assertion ofr md5
  if (("${LLVM_MD5}" STREQUAL "") OR
      ("${CLANG_MD5}" STREQUAL "") OR
      ("${COMPILER-RT_MD5}" STREQUAL ""))
    message(FATAL_ERROR "Undefined MD5 for your LLVM versions."
             " there is a bug in installLLVM.cmake")
  endif()
ENDMACRO()

FUNCTION(check_and_download target_url target_file target_md5)
  if(("${target_md5}" STREQUAL "" ) OR
     ("${target_url}" STREQUAL "" ) OR
     ("${target_file}" STREQUAL ""))
    message(FATAL_ERROR "calling undefined variable."
             " there is a bug in installLLVM.cmake")
  endif()

  # if file not exists, download it.
  if(NOT EXISTS ${target_file})
    message(STATUS "finds no ${target_file}, so download it!")
    FILE(DOWNLOAD ${target_url} ${target_file} SHOW_PROGRESS
         EXPECTED_MD5 ${target_md5})
  else()
    # if exist, then check integrity.
    FILE(MD5 ${target_file} checksum)
    # if checksum is inequal, re-download it.
    if(NOT (${checksum} STREQUAL ${target_md5}))
      message(WARNING "error checksum for ${target_file}, so download it!")
      FILE(DOWNLOAD ${target_url} ${target_file} SHOW_PROGRESS
           EXPECTED_MD5 ${target_md5})
    endif()
  endif()
ENDFUNCTION()

FUNCTION(extract_file target_file target_dir)
  if(("${target_file}" STREQUAL "") OR 
     ("${target_dir}" STREQUAL ""))
    message(FATAL_ERROR "calling undefined variable."
             " there is a bug in installLLVM.cmake")
  endif()


  # if finds no directory, then create one.
  if(NOT EXISTS ${target_dir})
    FILE(MAKE_DIRECTORY ${target_dir})
  endif()

  # Extract the archive. Note that CMake-built-in tar does not
  # support --strip-components.
  if(EXISTS ${target_file})
    execute_process(COMMAND ${TAR_EXE} -x --${COMPRESS_OPTION}
			-f ${target_file} --strip-components=1 
			WORKING_DIRECTORY ${target_dir})
  else()
    message(FATAL_ERROR "finds no ${target_file}")
  endif()

ENDFUNCTION()

FUNCTION(install_llvm AUTOINSTALL_DIR)
  SET(llvm_build_dir ${AUTOINSTALL_DIR}/build-llvm)
  # Create necessary directories.
  if (NOT EXISTS ${LLVM_ROOT_ARG})
    FILE(MAKE_DIRECTORY ${LLVM_ROOT_ARG})
  endif()

  if (NOT EXISTS ${llvm_build_dir})
    FILE(MAKE_DIRECTORY ${llvm_build_dir})
  endif()

  # This script configures llvm for you.
  execute_process(COMMAND ${CMAKE_COMMAND} ${LLVM_SOURCE_DIR}
		  -DCMAKE_INSTALL_PREFIX=${LLVM_ROOT_ARG}
		  WORKING_DIRECTORY ${llvm_build_dir})

  message(STATUS "finish configure the source code.")
  message(STATUS "Start to build and install LLVM, it may take tens of minutes."
		" If you worry about whether this script is still running,"
		" you can use \"top\" to monitor CPU usage.")

  # Create job number according to the processor count. In hope of
  # shortening build time.
  message(STATUS "start : make -j${PROCESSOR_COUNT}")
  execute_process(COMMAND make -j${PROCESSOR_COUNT}
		  WORKING_DIRECTORY ${llvm_build_dir})

  execute_process(COMMAND make install
		  WORKING_DIRECTORY ${llvm_build_dir})
ENDFUNCTION()



MACRO(check_llvm_version)
  # check whether llvm-config is installed again.
  find_program(llvm-config-temp
		NAMES "llvm-config-${LLVM_PATCH_VERSION}" "llvm-config"
		HINTS ${LLVM_ROOT_ARG}/bin)

  # llvm should exists in user's system.
  # if notfound, which means that install script failed, give an error.
  if (${llvm-config-temp} STREQUAL "llvm-config-temp-NOTFOUND")
    message(FATAL_ERROR "This is a bug. Please contact developers.")
  endif()

  execute_process(COMMAND ${llvm-config-temp} --version
		OUTPUT_VARIABLE LLVM_VERSION
		OUTPUT_STRIP_TRAILING_WHITESPACE)

  # Check whether the LLVM version meets our requirement.
  if(${LLVM_VERSION} MATCHES ^${LLVM_RECOMMEND_VERSION_ARG})
    # set LLVM_CONFIG_EXE for parent, so that not to find it in parent
    # process again.
    set(LLVM_CONFIG_EXE ${llvm-config-temp} PARENT_SCOPE)
    message(STATUS "Finished installing LLVM.")
  else()
    message(FATAL_ERROR "wrong version of LLVM\n"
			"This is a bug. Please contact developers.")
  endif()
ENDMACRO()



FUNCTION(autoinstall_llvm LLVM_RECOMMEND_VERSION_ARG LLVM_ROOT_ARG AUTOINSTALL_DIR_ARG)
  if (NOT DEFINED TEST_CMAKE)
    set(TEST_CMAKE false)
  endif()

  # check whether llvm version bigger 3.1
  llvm_install_assert_arg(${LLVM_RECOMMEND_VERSION_ARG} ${LLVM_ROOT_ARG})
  configure_processor_count()

  configure_autoinstall(${AUTOINSTALL_DIR_ARG})

  # download llvm, clang and compiler-rt here.
  check_and_download(${LLVM_URL} ${LLVM_FILE} ${LLVM_MD5})
  check_and_download(${CLANG_URL} ${CLANG_FILE} ${CLANG_MD5})
  check_and_download(${COMPILER-RT_URL} ${COMPILER-RT_FILE} ${COMPILER-RT_MD5})

  message(STATUS "finish llvm source download.")

  # extract the source code. keep 2nd arg null means same with 1st arg.
  extract_file(${LLVM_FILE} ${LLVM_SOURCE_DIR})
  extract_file(${CLANG_FILE} ${CLANG_SOURCE_DIR})
  extract_file(${COMPILER-RT_FILE} ${COMPILER-RT_SOURCE_DIR})

  message(STATUS "finish extraction for the source code.")

  # temperary commement out this for test usage.
  install_llvm(${AUTOINSTALL_DIR_ARG})
  check_llvm_version()
ENDFUNCTION()
