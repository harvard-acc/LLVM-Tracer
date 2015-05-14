FUNCTION(findLLVMTool_Fct var_name tool_name LLVM_ROOT LLVM_RECOMMEND_VERSION)
  set(LLVM_BIN_DIR "${LLVM_ROOT}/bin")
  find_program(${var_name}
  NAMES "${LLVM_BIN_DIR}/${tool_name}-${LLVM_RECOMMEND_VERSION}"
        "${LLVM_BIN_DIR}/${tool_name}")

  if(${${var_name}} STREQUAL "${var_name}-NOTFOUND")
    message(FATAL_ERROR "finds no ${var_name}")
  else()
    message(STATUS "${var_name} found : ${${var_name}}")
  endif()

  set(var_name ${var_name} PARENT_SCOPE)
ENDFUNCTION(findLLVMTool_Fct)

macro(findLLVMTool var_name tool_name)
  findLLVMTool_Fct(${var_name} ${tool_name} ${LLVM_ROOT} ${LLVM_RECOMMEND_VERSION})
endmacro(findLLVMTool)
