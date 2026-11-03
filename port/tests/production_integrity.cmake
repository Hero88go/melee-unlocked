# Window/game-free checks of the production scanner, overlay and session policy.
add_executable(port_production_integrity_test
  "${PROJECT_SOURCE_DIR}/port/tests/production_integrity_test.cpp"
  "${PROJECT_SOURCE_DIR}/port/runtime/host/mod_scan.cpp"
  "${PROJECT_SOURCE_DIR}/port/runtime/host/mod_profile.cpp"
  "${PROJECT_SOURCE_DIR}/port/runtime/host/source_mod_overlay.cpp")
target_compile_features(port_production_integrity_test PRIVATE cxx_std_17)
target_include_directories(port_production_integrity_test PRIVATE
  "${PROJECT_SOURCE_DIR}/port/runtime/host")
if(WIN32)
  target_compile_definitions(port_production_integrity_test PRIVATE NOMINMAX)
  target_compile_options(port_production_integrity_test PRIVATE /EHsc /utf-8)
  target_link_libraries(port_production_integrity_test PRIVATE bcrypt shell32 comdlg32)
elseif(APPLE)
  target_sources(port_production_integrity_test PRIVATE
    "${PROJECT_SOURCE_DIR}/port/tests/identity_stubs/hash_apple.cpp")
  target_include_directories(port_production_integrity_test PRIVATE
    "${PROJECT_SOURCE_DIR}/port/tests/identity_stubs")
endif()
add_test(NAME port_production_integrity COMMAND port_production_integrity_test)
add_test(NAME port_real_disc_vanilla COMMAND port_production_integrity_test --real-disc)
set_tests_properties(port_real_disc_vanilla PROPERTIES SKIP_RETURN_CODE 77)
target_compile_definitions(port_production_integrity_test PRIVATE
  MU_INTEGRITY_FIXTURE_DIR="${PROJECT_SOURCE_DIR}")
# Compile the actual small C policy functions against window-free host/online stubs.
# Keep the test tied to the game implementation without compiling or launching the game.
file(READ "${PROJECT_SOURCE_DIR}/sourceport/game/shim/mu_20xx_ai.c" ai_source)
string(REPLACE "\r\n" "\n" ai_source "${ai_source}")
string(FIND "${ai_source}" "int mu_20xx_ai_enabled(void)" ai_start)
string(FIND "${ai_source}" "unsigned int mu_20xx_ai(void*" ai_end)
math(EXPR ai_length "${ai_end} - ${ai_start}")
string(SUBSTRING "${ai_source}" ${ai_start} ${ai_length} ai_policy)
file(READ "${PROJECT_SOURCE_DIR}/sourceport/game/shim/mu_entry.c" entry_source)
string(REPLACE "\r\n" "\n" entry_source "${entry_source}")
string(FIND "${entry_source}" "const char* mu_test_getenv(const char* name)" entry_start)
string(FIND "${entry_source}" "unsigned int mu_game_options(void)" entry_end)
math(EXPR entry_length "${entry_end} - ${entry_start}")
string(SUBSTRING "${entry_source}" ${entry_start} ${entry_length} entry_policy)
file(WRITE "${CMAKE_CURRENT_BINARY_DIR}/mu_native_test_policy.inc" "${entry_policy}\n${ai_policy}")
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS
  "${PROJECT_SOURCE_DIR}/sourceport/game/shim/mu_entry.c"
  "${PROJECT_SOURCE_DIR}/sourceport/game/shim/mu_20xx_ai.c")
target_include_directories(port_production_integrity_test PRIVATE
  "${CMAKE_CURRENT_BINARY_DIR}" "${PROJECT_SOURCE_DIR}/port/runtime/abi")
foreach(mode plain yougame vanilla)
  add_test(NAME port_native_ai_env_${mode} COMMAND port_production_integrity_test --native-ai-${mode})
endforeach()
find_package(Python3 COMPONENTS Interpreter REQUIRED)
add_test(NAME port_production_env_wiring
  COMMAND "${Python3_EXECUTABLE}" "${PROJECT_SOURCE_DIR}/port/tests/production_env_wiring_test.py")
