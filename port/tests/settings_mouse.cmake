# Exercise the shipped ImGui core without a window, renderer, adapter or game process.
add_executable(port_settings_mouse_test
  "${PROJECT_SOURCE_DIR}/port/tests/settings_mouse_test.cpp"
  "${PROJECT_SOURCE_DIR}/port/third_party/imgui/imgui.cpp"
  "${PROJECT_SOURCE_DIR}/port/third_party/imgui/imgui_draw.cpp"
  "${PROJECT_SOURCE_DIR}/port/third_party/imgui/imgui_tables.cpp"
  "${PROJECT_SOURCE_DIR}/port/third_party/imgui/imgui_widgets.cpp")
target_include_directories(port_settings_mouse_test PRIVATE
  "${PROJECT_SOURCE_DIR}/port/runtime/gx" "${PROJECT_SOURCE_DIR}/port/third_party/imgui")
target_compile_features(port_settings_mouse_test PRIVATE cxx_std_17)
add_test(NAME port_settings_mouse COMMAND port_settings_mouse_test)
