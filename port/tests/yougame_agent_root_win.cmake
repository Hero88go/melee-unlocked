# Shared by the default Windows test configuration and the full port build.
if(TARGET port_yougame_agent_root_win)
  return()
endif()
add_executable(port_yougame_agent_root_win "${CMAKE_CURRENT_LIST_DIR}/yougame_agent_root_win_test.cpp")
target_include_directories(port_yougame_agent_root_win PRIVATE "${CMAKE_CURRENT_LIST_DIR}/../runtime/hle")
target_compile_features(port_yougame_agent_root_win PRIVATE cxx_std_17)
target_compile_definitions(port_yougame_agent_root_win PRIVATE
  YOUGAME_AGENT_TEST_HOOK YOUGAME_AGENT_ROOT_WIN_TEST NOMINMAX _WIN32_WINNT=0x0602)
if(MSVC)
  target_compile_options(port_yougame_agent_root_win PRIVATE /EHsc /W4)
endif()
target_link_libraries(port_yougame_agent_root_win PRIVATE advapi32)
add_test(NAME port_yougame_agent_root_win COMMAND port_yougame_agent_root_win)
