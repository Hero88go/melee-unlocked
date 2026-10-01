# Included only by tools/ppc_stencils/CMakeLists.txt, the standalone test project.
# Off by default: no shipped executable or runtime dispatch is changed by this test slice.
# SPDX-License-Identifier: GPL-2.0-or-later
option(MELEE_BUILD_PPC_STENCIL_TESTS "Build the isolated AMD64 copy-and-patch leaf test" OFF)
get_filename_component(MELEE_STENCIL_REPO_ROOT "${CMAKE_CURRENT_LIST_DIR}/../../.." ABSOLUTE)
add_test(NAME port_ppc_stencil_extract COMMAND "${Python3_EXECUTABLE}"
  "${MELEE_STENCIL_REPO_ROOT}/port/tests/ppc_stencil_extract_test.py")
if(MELEE_BUILD_PPC_STENCIL_TESTS)
  if(NOT MSVC OR NOT CMAKE_SIZEOF_VOID_P EQUAL 8)
    message(FATAL_ERROR "The initial copy-and-patch stencils require MSVC AMD64")
  endif()
  add_library(ppc_leaf_stencils OBJECT "${CMAKE_CURRENT_LIST_DIR}/leaf_stencils.cpp")
  target_compile_features(ppc_leaf_stencils PRIVATE cxx_std_17)
  target_compile_options(ppc_leaf_stencils PRIVATE /O2 /Ob2 /Gy /GS- /GL- /GR- /EHsc ${MELEE_ARCH_FLAG} /fp:precise)
  target_compile_definitions(ppc_leaf_stencils PRIVATE NOMINMAX _CRT_SECURE_NO_WARNINGS)
  set(ppc_stencil_header "${CMAKE_CURRENT_BINARY_DIR}/ppc-stencils/ppc_leaf_stencils.generated.h")
  add_custom_command(OUTPUT "${ppc_stencil_header}"
    COMMAND "${Python3_EXECUTABLE}" "${MELEE_STENCIL_REPO_ROOT}/tools/extract_ppc_stencils.py"
      "$<TARGET_OBJECTS:ppc_leaf_stencils>" --header "${ppc_stencil_header}"
      --json "${CMAKE_CURRENT_BINARY_DIR}/ppc-stencils/stencil-table.json"
    DEPENDS ppc_leaf_stencils "$<TARGET_OBJECTS:ppc_leaf_stencils>"
      "${MELEE_STENCIL_REPO_ROOT}/tools/extract_ppc_stencils.py"
    COMMAND_EXPAND_LISTS VERBATIM)
  # A single target owns the extraction, so parallel test projects never run it twice.
  add_custom_target(ppc_leaf_stencil_table DEPENDS "${ppc_stencil_header}")
  # One native test executable per source in port/tests, each with its own copy of the translator.
  function(melee_ppc_leaf_test name)
    add_executable(port_${name}_test
      "${MELEE_STENCIL_REPO_ROOT}/port/tests/${name}_test.cpp"
      "${MELEE_STENCIL_REPO_ROOT}/port/runtime/ppc/leaf_translator.cpp" ${ARGN})
    add_dependencies(port_${name}_test ppc_leaf_stencil_table)
    target_include_directories(port_${name}_test PRIVATE
      "${MELEE_STENCIL_REPO_ROOT}/port/runtime/ppc" "${CMAKE_CURRENT_BINARY_DIR}/ppc-stencils")
    target_compile_features(port_${name}_test PRIVATE cxx_std_17)
    target_compile_options(port_${name}_test PRIVATE /EHsc ${MELEE_ARCH_FLAG} /fp:precise)
    target_compile_definitions(port_${name}_test PRIVATE NOMINMAX _CRT_SECURE_NO_WARNINGS)
    add_test(NAME port_${name} COMMAND port_${name}_test)
    # A wrong loop translation never returns; no test here needs more than a few seconds.
    set_tests_properties(port_${name} PROPERTIES TIMEOUT 300)
  endfunction()
  melee_ppc_leaf_test(ppc_leaf_translator)
  melee_ppc_leaf_test(ppc_leaf_integer)
  melee_ppc_leaf_test(ppc_leaf_branch)
  # Guest functions with the C++ the unmodified recompiler emitter writes for them.
  set(ppc_emit_corpus_header "${CMAKE_CURRENT_BINARY_DIR}/ppc-stencils/ppc_emit_corpus.generated.h")
  add_custom_command(OUTPUT "${ppc_emit_corpus_header}"
    COMMAND "${Python3_EXECUTABLE}" "${MELEE_STENCIL_REPO_ROOT}/tools/ppc_stencils/generate_emit_corpus.py"
      --header "${ppc_emit_corpus_header}"
    DEPENDS "${MELEE_STENCIL_REPO_ROOT}/tools/ppc_stencils/generate_emit_corpus.py"
      "${MELEE_STENCIL_REPO_ROOT}/port/recomp/emit.py" "${MELEE_STENCIL_REPO_ROOT}/port/recomp/gekko.py"
    VERBATIM)
  add_custom_target(ppc_emit_corpus DEPENDS "${ppc_emit_corpus_header}")
  melee_ppc_leaf_test(ppc_leaf_emit_corpus)
  add_dependencies(port_ppc_leaf_emit_corpus_test ppc_emit_corpus)
endif()
