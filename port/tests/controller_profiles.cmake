# Controller profiles and overlay binding access: no window, device or game process.
add_executable(port_controller_profiles_test
  "${PROJECT_SOURCE_DIR}/port/tests/controller_profiles_test.cpp"
  "${PROJECT_SOURCE_DIR}/port/runtime/host/controller_profiles.cpp")
target_include_directories(port_controller_profiles_test PRIVATE
  "${PROJECT_SOURCE_DIR}/port/runtime/host")
target_compile_features(port_controller_profiles_test PRIVATE cxx_std_17)
add_test(NAME port_controller_profiles COMMAND port_controller_profiles_test)

add_executable(port_settings_profile_bindings_test
  "${PROJECT_SOURCE_DIR}/port/tests/settings_profile_bindings_test.cpp")
target_include_directories(port_settings_profile_bindings_test PRIVATE
  "${PROJECT_SOURCE_DIR}/port/runtime/gx")
target_compile_features(port_settings_profile_bindings_test PRIVATE cxx_std_17)
add_test(NAME port_settings_profile_bindings COMMAND port_settings_profile_bindings_test)

add_executable(port_dolphin_profile_test
  "${PROJECT_SOURCE_DIR}/port/tests/dolphin_profile_test.cpp"
  "${PROJECT_SOURCE_DIR}/port/runtime/host/dolphin_profile.cpp")
target_include_directories(port_dolphin_profile_test PRIVATE
  "${PROJECT_SOURCE_DIR}/port/runtime/host")
target_compile_features(port_dolphin_profile_test PRIVATE cxx_std_17)
if(WIN32)
  target_compile_definitions(port_dolphin_profile_test PRIVATE NOMINMAX)
  target_link_libraries(port_dolphin_profile_test PRIVATE comdlg32 user32)
endif()
add_test(NAME port_dolphin_profile COMMAND port_dolphin_profile_test)
