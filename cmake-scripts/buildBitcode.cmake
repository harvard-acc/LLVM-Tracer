# This is a utility which help user to create llvm bitcode easily
# while build time,

# These functions implicit uses :
#    LLVM_COMPILER
#    LLVMC_FLAGS
#    LLVM_OPT_FLAGS

if(NOT DEFINED LLVM_EXT)
  SET(LLVM_EXT llvm)
endif()


macro(collect_included_headers f_abso_temp_src)
  # collect the header dependency of source code.
  execute_process(COMMAND 
    ${LLVM_COMPILER} ${LLVMC_FLAGS} ${f_abso_temp_src} -MM
    OUTPUT_VARIABLE f_dep_var
    OUTPUT_STRIP_TRAILING_WHITESPACE)

  # clang -MM generate Makefile dep, so string truncation is necessery.
  if(NOT "${f_dep_var}" STREQUAL "")
    # remove the backslash and newline
    string(REPLACE "\\\n" ";" f_dep_var "${f_dep_var}")
    string(REPLACE " "    ""  f_dep_var "${f_dep_var}")
    # remove the first item, which normally be "main.o : main.cpp"
    LIST(REMOVE_AT f_dep_var 0)
  endif()
endmacro(collect_included_headers)

macro(build_llvm_bc_object f_temp_src)
  get_filename_component(f_abso_temp_src ${f_temp_src} ABSOLUTE)
  get_filename_component(f_file_ext ${f_temp_src} EXT)

  # file extension(.cpp .c) -> obj.bc
  STRING(REPLACE "${f_file_ext}" ".obj.${LLVM_EXT}" f_temp_object ${f_abso_temp_src})
  # source_dir -> binary_dir
  STRING(REPLACE ${CMAKE_CURRENT_SOURCE_DIR} ${CMAKE_CURRENT_BINARY_DIR} 
      	   f_temp_object ${f_temp_object})

  # In case that the corresponding directory of binary_dir does not exist.
  get_filename_component(f_temp_object_dir ${f_temp_object} PATH)
  if(NOT EXISTS ${f_temp_object_dir})
     file(MAKE_DIRECTORY ${f_temp_object_dir})
  endif()

  if (${GEN_HEADER_DEPENDENCY})
    # To generate header dependency for SystemC applications.
    collect_included_headers(${f_abso_temp_src})
  endif(${GEN_HEADER_DEPENDENCY})

  LIST(APPEND f_dep_var ${f_abso_temp_src})

  add_custom_command(OUTPUT ${f_temp_object} DEPENDS ${f_dep_var}
    COMMAND ${LLVM_COMPILER} ${LLVMC_FLAGS} -emit-llvm -c ${f_abso_temp_src}
    -o ${f_temp_object} VERBATIM)

  LIST(APPEND f_objects ${f_temp_object})
endmacro(build_llvm_bc_object)

function(build_llvm_bc f_target_name f_src_list f_target_directory)
  set(f_target_unopt_file
             ${CMAKE_CURRENT_BINARY_DIR}/${f_target_name}.unopt.${LLVM_EXT})

  if (${f_target_directory} STREQUAL "")
    message(FATAL_ERROR "unknown target directory")
  endif()

  set(f_target_file ${f_target_directory}/${f_target_name}.${LLVM_EXT})

  # for each .cpp file, LLVMC would generate .obj.bc file in binary dir.
  foreach(f_temp_src ${${f_src_list}})
    build_llvm_bc_object(${f_temp_src})
  endforeach(f_temp_src)

  # Generate a raw llvm bitcode
  add_custom_command(OUTPUT ${f_target_unopt_file} DEPENDS ${f_objects}
    COMMAND ${LLVM_LINK} ${f_objects} -o ${f_target_unopt_file} 
    VERBATIM)

  # Generate a opted llvm bitcode
  add_custom_command(OUTPUT ${f_target_file} DEPENDS ${f_target_unopt_file}
    COMMAND ${LLVM_OPT} ${LLVM_OPT_FLAGS} -o ${f_target_file}
    ${f_target_unopt_file} 
    VERBATIM)

  add_custom_target(${f_target_name} DEPENDS ${f_target_file})
endfunction(build_llvm_bc)

function(build_llvm_bitcode f_target_name f_src_list)
  build_llvm_bc(${f_target_name} ${f_src_list} ${CMAKE_CURRENT_BINARY_DIR})
endfunction(build_llvm_bitcode)
