# The table and cache are linked by Static targets. Source resolves the dispatch calls with
# source_guest_boundary.cpp and cannot pull the translation or interpreter objects from libraries.
# SPDX-License-Identifier: GPL-2.0-or-later
add_library(ppc_runtime_stencils OBJECT "${CMAKE_CURRENT_LIST_DIR}/leaf_stencils.cpp")
target_compile_features(ppc_runtime_stencils PRIVATE cxx_std_17)
target_compile_options(ppc_runtime_stencils PRIVATE /O2 /Ob3 /Gy /GS- /GL- /GR- /EHsc ${MELEE_ARCH_FLAG} /fp:precise)
target_compile_definitions(ppc_runtime_stencils PRIVATE NOMINMAX _CRT_SECURE_NO_WARNINGS)
set(ram_stencil_header "${CMAKE_CURRENT_BINARY_DIR}/ram-stencils/ppc_leaf_stencils.generated.h")
add_custom_command(OUTPUT "${ram_stencil_header}"
  COMMAND "${Python3_EXECUTABLE}" "${PROJECT_SOURCE_DIR}/tools/extract_ppc_stencils.py"
    "$<TARGET_OBJECTS:ppc_runtime_stencils>" --header "${ram_stencil_header}"
    --json "${CMAKE_CURRENT_BINARY_DIR}/ram-stencils/stencil-table.json"
  DEPENDS ppc_runtime_stencils "$<TARGET_OBJECTS:ppc_runtime_stencils>"
    "${PROJECT_SOURCE_DIR}/tools/extract_ppc_stencils.py"
  COMMAND_EXPAND_LISTS VERBATIM)
add_custom_target(ppc_runtime_stencil_table DEPENDS "${ram_stencil_header}")
add_library(ppc_ram_translator STATIC
  "${CMAKE_CURRENT_LIST_DIR}/ram_translator.cpp" "${CMAKE_CURRENT_LIST_DIR}/leaf_translator.cpp")
add_dependencies(ppc_ram_translator ppc_runtime_stencil_table)
target_include_directories(ppc_ram_translator PRIVATE
  "${CMAKE_CURRENT_LIST_DIR}" "${CMAKE_CURRENT_LIST_DIR}/../host" "${CMAKE_CURRENT_BINARY_DIR}/ram-stencils")
target_compile_features(ppc_ram_translator PRIVATE cxx_std_17)
target_compile_options(ppc_ram_translator PRIVATE /EHsc ${MELEE_ARCH_FLAG} /fp:precise /W3)
target_compile_definitions(ppc_ram_translator PRIVATE NOMINMAX _CRT_SECURE_NO_WARNINGS)
target_link_libraries(runtime INTERFACE ppc_ram_translator)

add_executable(port_ram_translator_test tests/ram_translator_test.cpp)
target_compile_options(port_ram_translator_test PRIVATE /EHsc ${MELEE_ARCH_FLAG} /fp:precise)
target_compile_definitions(port_ram_translator_test PRIVATE NOMINMAX _CRT_SECURE_NO_WARNINGS)
target_link_libraries(port_ram_translator_test PRIVATE runtime guest)
add_test(NAME port_ram_translator COMMAND port_ram_translator_test)
set_tests_properties(port_ram_translator PROPERTIES TIMEOUT 60)
