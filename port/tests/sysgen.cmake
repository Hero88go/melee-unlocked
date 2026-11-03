# Disc-derived Sys assets and VCDIFF tests: no game process or external encoder.
add_executable(port_yougame_sysgen_test
  "${PROJECT_SOURCE_DIR}/port/tests/yougame_sysgen_test.cpp"
  "${PROJECT_SOURCE_DIR}/port/runtime/host/sysgen.cpp"
  "${PROJECT_SOURCE_DIR}/port/runtime/host/vcdiff.cpp")
target_include_directories(port_yougame_sysgen_test PRIVATE
  "${PROJECT_SOURCE_DIR}/port/runtime/host" "${PROJECT_SOURCE_DIR}/port/third_party")
target_compile_features(port_yougame_sysgen_test PRIVATE cxx_std_17)
target_compile_definitions(port_yougame_sysgen_test PRIVATE
  SYSGEN_MANIFEST="${CMAKE_SOURCE_DIR}/port/sys_deltas/manifest.json")
if(WIN32)
  target_compile_definitions(port_yougame_sysgen_test PRIVATE NOMINMAX)
endif()
add_test(NAME port_yougame_sysgen COMMAND port_yougame_sysgen_test)
