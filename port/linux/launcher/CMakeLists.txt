# Melee Unlocked Launcher for Linux (output MeleeUnlocked): Dear ImGui on SDL2/OpenGL over the
# Windows launcher's logic files. Built beside the Source Port by port/linux/full.
set(PORT "${PROJECT_SOURCE_DIR}/port")
set(COMPAT "${CMAKE_CURRENT_SOURCE_DIR}/../compat")
include("${PORT}/app/disc_rvz.cmake")
add_executable(melee_launcher launcher_linux.cpp
  "${PORT}/app/launcher_replay_data.cpp" "${PORT}/app/launcher_lobby_p2p.cpp"
  "${PORT}/runtime/host/updater.cpp" "${COMPAT}/compat.cpp" "${COMPAT}/winhttp_ws.cpp"
  "${PORT}/app/launcher_yougame.cpp" "${PORT}/third_party/dht/dht.c")
set_target_properties(melee_launcher PROPERTIES OUTPUT_NAME MeleeUnlocked)
add_test(NAME linux_launcher_quick_play
  COMMAND "${Python3_EXECUTABLE}" "${PORT}/tests/linux_launcher_quick_play_test.py" $<TARGET_FILE:melee_launcher>)
target_include_directories(melee_launcher PRIVATE "${COMPAT}" "${PORT}/app" "${PORT}/runtime/host" "${PORT}/runtime/hle"
  "${PORT}/runtime/ppc" "${PORT}/runtime/gx" "${PORT}/third_party" "${PORT}/third_party/dht" "${PORT}/third_party/monocypher")
target_compile_features(melee_launcher PRIVATE cxx_std_17)
file(STRINGS "${PROJECT_SOURCE_DIR}/VERSION" MELEE_LAUNCHER_VERSION LIMIT_COUNT 1)
target_compile_definitions(melee_launcher PRIVATE MELEE_LINUX MELEE_PORT_VERSION="${MELEE_LAUNCHER_VERSION}")
set_source_files_properties(launcher_linux.cpp "${PORT}/app/launcher_replay_data.cpp" "${PORT}/app/launcher_lobby_p2p.cpp" "${PORT}/app/launcher_yougame.cpp"
  "${PORT}/runtime/host/updater.cpp" "${COMPAT}/compat.cpp" "${COMPAT}/winhttp_ws.cpp"
  PROPERTIES COMPILE_OPTIONS "-include;${COMPAT}/windows.h;-Wno-unknown-pragmas")
target_link_libraries(melee_launcher PRIVATE port_disc_rvz full_imgui full_monocypher SDL2::SDL2 OpenGL::GL GLEW::GLEW CURL::libcurl
  OpenSSL::Crypto Threads::Threads ${CMAKE_DL_LIBS})
target_link_options(melee_launcher PRIVATE "-Wl,-rpath,\$ORIGIN/lib" -Wl,--enable-new-dtags)
