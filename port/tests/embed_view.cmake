# Window-free geometry and wiring checks, including the native Linux root path.
enable_testing()
find_package(Python3 COMPONENTS Interpreter REQUIRED)
add_executable(port_yougame_embed_view_test "${PROJECT_SOURCE_DIR}/port/tests/yougame_embed_view_test.cpp")
target_include_directories(port_yougame_embed_view_test PRIVATE "${PROJECT_SOURCE_DIR}/port/runtime/hle")
target_compile_features(port_yougame_embed_view_test PRIVATE cxx_std_17)
add_test(NAME port_yougame_embed_view COMMAND port_yougame_embed_view_test)
add_test(NAME port_embed_view_wiring COMMAND "${Python3_EXECUTABLE}" -B
  "${PROJECT_SOURCE_DIR}/port/tests/embed_view_wiring_test.py")
add_executable(port_yougame_embed_volume_test "${PROJECT_SOURCE_DIR}/port/tests/yougame_embed_volume_test.cpp")
target_include_directories(port_yougame_embed_volume_test PRIVATE "${PROJECT_SOURCE_DIR}/port/runtime/hle")
target_compile_features(port_yougame_embed_volume_test PRIVATE cxx_std_17)
add_test(NAME port_yougame_embed_volume COMMAND port_yougame_embed_volume_test)
