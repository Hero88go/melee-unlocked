# The whole Source Port on Linux: the same host sources as the Windows melee_source (Slippi online,
# replays, spectating, mods, cosmetics, the settings panel, Gecko codes, the GameCube adapter), with
# Linux implementations where Windows does the work: the SDL2 window and input, OpenGL rendering,
# SDL2 audio, hidraw controllers, FFmpeg media, and port/linux/compat for the small Win32 API surface
# the portable files use. The game comes from the same melee_game.so the Linux build makes.
cmake_minimum_required(VERSION 3.25)
set(PORT "${PROJECT_SOURCE_DIR}/port")
set(MELEE_PORT_GEN "" CACHE PATH "The guest translation's generated headers (hle_decls.h, guest_symbols.h)")
if(NOT EXISTS "${MELEE_PORT_GEN}/hle_decls.h" OR NOT EXISTS "${MELEE_PORT_GEN}/guest_symbols.h")
  message(FATAL_ERROR "MELEE_PORT_GEN must name a folder with hle_decls.h and guest_symbols.h (python port/recomp/recomp.py)")
endif()
find_package(SDL2 REQUIRED)
find_package(X11 REQUIRED COMPONENTS Xext Xshape)
find_package(OpenGL REQUIRED)
find_package(GLEW REQUIRED)
find_package(CURL REQUIRED)
find_package(OpenSSL REQUIRED)
pkg_check_modules(FFMPEG REQUIRED IMPORTED_TARGET libavformat libavcodec libswscale libswresample libavutil)

file(STRINGS "${PROJECT_SOURCE_DIR}/VERSION" MELEE_PORT_VERSION LIMIT_COUNT 1)
string(REPLACE "." ";" MELEE_VERSION_PARTS "${MELEE_PORT_VERSION}")
list(LENGTH MELEE_VERSION_PARTS MELEE_VERSION_PART_COUNT)
while(MELEE_VERSION_PART_COUNT LESS 4)
  list(APPEND MELEE_VERSION_PARTS "0")
  list(LENGTH MELEE_VERSION_PARTS MELEE_VERSION_PART_COUNT)
endwhile()
list(SUBLIST MELEE_VERSION_PARTS 0 4 MELEE_VERSION_PARTS)
list(JOIN MELEE_VERSION_PARTS "," MELEE_VERSION_COMMAS)
# The compatibility build (any other baseline) sits beside the AVX2 one under its own names, and the
# launcher picks between them by asking the processor.
if(MELEE_CPU_BASELINE STREQUAL "AVX2")
  set(MELEE_GAME_FILE melee_game.so)
  set(MELEE_SOURCE_NAME melee_source)
else()
  set(MELEE_GAME_FILE melee_game_compat.so)
  set(MELEE_SOURCE_NAME melee_source_compat)
endif()
if(MELEE_CPU_BASELINE STREQUAL "AVX2")
  set(MELEE_HOST_ARCH -mavx2 -mfma)
elseif(MELEE_CPU_BASELINE STREQUAL "AVX")
  set(MELEE_HOST_ARCH -mavx)
else()
  set(MELEE_HOST_ARCH "")
endif()
set(COMPAT "${CMAKE_CURRENT_SOURCE_DIR}/../compat")
set(MELEE_LINUX_COMMON_FLAGS -include "${COMPAT}/windows.h" ${MELEE_HOST_ARCH} -ffp-contract=off -fno-strict-aliasing
    -Wno-unknown-pragmas -Wno-multichar -pthread)

# Generated sources, as the Windows build makes them.
set(FULL_FOBJ "${CMAKE_CURRENT_BINARY_DIR}/generated/FObjHost.cpp")
add_custom_command(OUTPUT "${FULL_FOBJ}"
  COMMAND "${Python3_EXECUTABLE}" "${PROJECT_SOURCE_DIR}/tools/generate_fobj_host.py" "${FULL_FOBJ}"
  DEPENDS "${PROJECT_SOURCE_DIR}/tools/generate_fobj_host.py" VERBATIM)
add_library(full_native_animation STATIC "${FULL_FOBJ}")
target_include_directories(full_native_animation PUBLIC "${PROJECT_SOURCE_DIR}/native")
target_compile_features(full_native_animation PUBLIC cxx_std_14)
target_compile_options(full_native_animation PRIVATE ${MELEE_HOST_ARCH})
set(NATIVE_RECORDING_CODES "${CMAKE_CURRENT_BINARY_DIR}/native_recording_codes.h")
add_custom_command(OUTPUT "${NATIVE_RECORDING_CODES}"
  COMMAND "${Python3_EXECUTABLE}" "${PROJECT_SOURCE_DIR}/tools/native_recording_codes.py" "${NATIVE_RECORDING_CODES}"
  DEPENDS "${PROJECT_SOURCE_DIR}/tools/native_recording_codes.py" "${PORT}/recomp/gecko.py"
          "${PORT}/slippi_sys_general/GameSettings/GALE01r2.ini" "${PORT}/slippi_sys/GameSettings/GALE01r2.ini"
  VERBATIM)

enable_language(C)
file(GLOB FULL_ENET_SOURCES "${PORT}/third_party/enet/*.c")
add_library(full_enet STATIC ${FULL_ENET_SOURCES})
target_include_directories(full_enet PUBLIC "${PORT}/third_party/enet/include")
target_compile_definitions(full_enet PRIVATE HAS_SOCKLEN_T=1 HAS_POLL=1 HAS_FCNTL=1 HAS_MSGHDR_FLAGS=1 HAS_GETADDRINFO=1 HAS_GETNAMEINFO=1 HAS_INET_PTON=1 HAS_INET_NTOP=1)
target_link_libraries(full_enet PUBLIC Threads::Threads)
add_executable(yg_enet_posix_ring_test "${PORT}/tests/yg_enet_posix_ring_test.c")
target_link_libraries(yg_enet_posix_ring_test PRIVATE full_enet)
add_test(NAME yg_enet_posix_ring COMMAND yg_enet_posix_ring_test)
set_tests_properties(yg_enet_posix_ring PROPERTIES TIMEOUT 30)
set(FAKE_RING_ENET_SOURCES ${FULL_ENET_SOURCES})
list(REMOVE_ITEM FAKE_RING_ENET_SOURCES "${PORT}/third_party/enet/unix.c" "${PORT}/third_party/enet/win32.c")
add_executable(yg_enet_socket_test "${PORT}/tests/yg_enet_socket_test.c" ${FAKE_RING_ENET_SOURCES})
target_include_directories(yg_enet_socket_test PRIVATE "${PORT}/third_party/enet/include")
target_link_libraries(yg_enet_socket_test PRIVATE Threads::Threads)
add_test(NAME yg_enet_socket COMMAND yg_enet_socket_test)
add_executable(yougame_transport_test "${PORT}/tests/yougame_transport_test.cpp")
target_include_directories(yougame_transport_test PRIVATE "${PORT}/runtime/hle" "${PORT}/third_party")
target_compile_features(yougame_transport_test PRIVATE cxx_std_17)
target_link_libraries(yougame_transport_test PRIVATE Threads::Threads)
add_test(NAME yougame_transport COMMAND yougame_transport_test)
add_library(full_monocypher STATIC "${PORT}/third_party/monocypher/monocypher.c" "${PORT}/third_party/monocypher/monocypher-ed25519.c")
target_include_directories(full_monocypher PUBLIC "${PORT}/third_party/monocypher")

add_library(full_imgui STATIC
  "${PORT}/third_party/imgui/imgui.cpp" "${PORT}/third_party/imgui/imgui_draw.cpp"
  "${PORT}/third_party/imgui/imgui_tables.cpp" "${PORT}/third_party/imgui/imgui_widgets.cpp"
  "${PORT}/third_party/imgui/backends/imgui_impl_sdl2.cpp" "${PORT}/third_party/imgui/backends/imgui_impl_opengl3.cpp"
  "${PORT}/third_party/imgui/backends/imgui_impl_vulkan.cpp")
# The Vulkan renderer (runtime/gx/gx_vulkan.cpp) opens libvulkan at run time: headers only.
target_include_directories(full_imgui PUBLIC "${PORT}/third_party/imgui" "${PORT}/third_party/imgui/backends" "${PORT}/third_party/vulkan-headers/include")
target_compile_definitions(full_imgui PUBLIC IMGUI_IMPL_VULKAN_NO_PROTOTYPES)
target_compile_features(full_imgui PUBLIC cxx_std_17)
target_link_libraries(full_imgui PUBLIC SDL2::SDL2 ${CMAKE_DL_LIBS})

# The host runtime: every portable file of runtime/, the Windows renderers and Windows-only GPU
# libraries left out, the Linux implementations added.
file(GLOB FULL_RUNTIME_SOURCES "${PORT}/runtime/ppc/*.cpp" "${PORT}/runtime/hle/*.cpp" "${PORT}/runtime/host/*.cpp" "${PORT}/runtime/gx/*.cpp")
foreach(removed
    runtime/ppc/leaf_stencils.cpp runtime/ppc/leaf_translator.cpp runtime/ppc/ram_translator.cpp
    runtime/gx/gx_d3d11.cpp runtime/gx/gx_d3d12.cpp runtime/gx/gx_dlss5.cpp runtime/gx/gx_dlss5_scaling.cpp
    runtime/gx/gx_dxr_gpu_scene.cpp runtime/gx/gx_dxr_path_tracer.cpp runtime/gx/gx_streamline.cpp runtime/gx/gx_xess.cpp
    runtime/gx/rtx_scene.cpp runtime/gx/rtx_tracer.cpp runtime/gx/rtx_melee_integrator.cpp
    runtime/gx/gx_backend.cpp runtime/gx/pc_settings_dx11.cpp runtime/host/audio.cpp)
  list(REMOVE_ITEM FULL_RUNTIME_SOURCES "${PORT}/${removed}")
endforeach()
add_library(full_runtime STATIC ${FULL_RUNTIME_SOURCES} "${PORT}/third_party/slippilib/SlippiGame.cpp"
  "${COMPAT}/compat.cpp" "${COMPAT}/winhttp_ws.cpp" ../xinput_sdl.cpp ../hid_linux.cpp ../renderer_gl.cpp ../renderer_presenter.cpp
  gx_backend_linux.cpp pc_settings_gl.cpp audio_sdl.cpp linux_media.cpp gpu_features_linux.cpp)
target_compile_definitions(full_runtime PUBLIC MELEE_LINUX GX_PC_SETTINGS MELEE_LINUX_X11_EMBED
  PRIVATE MELEE_PORT_VERSION="${MELEE_PORT_VERSION}" MELEE_PLAYBACK_GCT_BASE=0u MELEE_PLAYBACK_GCT_SIZE=0u
  GX_SHADER_CACHE_VERSION="linux-gl-1")
target_include_directories(full_runtime PUBLIC "${COMPAT}" "${PORT}/runtime/ppc" "${PORT}/runtime/host" "${PORT}/runtime/hle"
  "${PORT}/runtime/gx" "${PORT}/runtime/mu_net" "${PORT}/runtime/abi" "${MELEE_PORT_GEN}" "${PROJECT_SOURCE_DIR}/native"
  "${PORT}/third_party" "${PORT}/third_party/onnxruntime/include" "${CMAKE_CURRENT_SOURCE_DIR}/.." ${LIBUSB_INCLUDE_DIRS})
target_compile_features(full_runtime PUBLIC cxx_std_17)
target_compile_options(full_runtime PRIVATE ${MELEE_LINUX_COMMON_FLAGS})
target_link_libraries(full_runtime PUBLIC full_native_animation full_enet full_imgui PkgConfig::LIBUSB SDL2::SDL2 OpenGL::GL
  GLEW::GLEW CURL::libcurl OpenSSL::Crypto PkgConfig::FFMPEG Threads::Threads X11::X11 X11::Xext ${CMAKE_DL_LIBS} m)

add_executable(linux_ws_frame_test "${COMPAT}/tests/ws_frame_test.cpp")
target_compile_features(linux_ws_frame_test PRIVATE cxx_std_17)
target_link_libraries(linux_ws_frame_test PRIVATE OpenSSL::Crypto)
add_test(NAME linux_ws_frames COMMAND linux_ws_frame_test)

add_library(full_mu_net STATIC "${PORT}/runtime/mu_net/session_types.cpp" "${PORT}/runtime/mu_net/peer_identity.cpp"
  "${PORT}/runtime/mu_net/session_security.cpp" "${PORT}/runtime/mu_net/input_transport.cpp" "${PORT}/runtime/mu_net/peer_connector.cpp"
  "${PORT}/runtime/mu_net/rollback_session.cpp" "${PORT}/runtime/mu_net/result_outbox.cpp" "${PORT}/runtime/mu_net/nat_discovery.cpp")
target_include_directories(full_mu_net PUBLIC "${PORT}/runtime/mu_net" "${COMPAT}")
target_compile_features(full_mu_net PUBLIC cxx_std_17)
target_compile_definitions(full_mu_net PRIVATE MELEE_LINUX)
target_compile_options(full_mu_net PRIVATE ${MELEE_LINUX_COMMON_FLAGS})
target_link_libraries(full_mu_net PUBLIC full_enet PRIVATE full_monocypher OpenSSL::Crypto)

add_executable(melee_source ../embed/yougame_embed_x11.c "${PORT}/app/main.cpp" settings_window_linux.cpp "${PORT}/app/source_host.cpp"
  "${PORT}/app/source_guest_boundary.cpp" "${PORT}/app/source_p2p.cpp" "${NATIVE_RECORDING_CODES}")
target_include_directories(melee_source PRIVATE "${CMAKE_CURRENT_BINARY_DIR}" "${PORT}/runtime/abi" "${PORT}/app")
target_compile_definitions(melee_source PRIVATE MELEE_SOURCE_PORT MELEE_PORT_VERSION="${MELEE_PORT_VERSION}"
  MELEE_CPU_BASELINE_NAME="${MELEE_CPU_BASELINE}" MELEE_VERSION_COMMAS=${MELEE_VERSION_COMMAS}
  MELEE_GAME_LIBRARY_NAME="${MELEE_GAME_FILE}")
set_target_properties(melee_source PROPERTIES OUTPUT_NAME ${MELEE_SOURCE_NAME})
# The shared Win32 compatibility preinclude is C++; never force it into the C99 helper.
target_compile_options(melee_source PRIVATE "$<$<COMPILE_LANGUAGE:CXX>:${MELEE_LINUX_COMMON_FLAGS}>")
target_compile_features(melee_source PRIVATE c_std_99)
target_link_libraries(melee_source PRIVATE full_runtime full_mu_net)
option(YOUGAME_AGENT_TEST_HOOK "Staging test builds: read agent-test.json in app-launched YouGame mode" OFF)
if(YOUGAME_AGENT_TEST_HOOK)
  # Windows defines this privately on runtime and both game hosts. Linux has one full host;
  # melee_linux is the separate minimal host and does not contain these hook sources.
  target_compile_definitions(melee_source PRIVATE YOUGAME_AGENT_TEST_HOOK)
  target_compile_definitions(full_runtime PRIVATE YOUGAME_AGENT_TEST_HOOK)
  message(STATUS "YOUGAME_AGENT_TEST_HOOK=ON: staging agent-test build")
else()
  add_test(NAME linux_agent_test_hook_absent COMMAND sh
    "${PORT}/tests/agent_test_hook_absent.sh" $<TARGET_FILE:melee_source> $<TARGET_FILE:melee_linux>)
endif()
set_target_properties(melee_source PROPERTIES POSITION_INDEPENDENT_CODE ON)
# The release carries its own copies of the libraries that differ between distributions, in lib/
# beside the program (tools/package_linux_release.py); without lib/ the system's are used.
target_link_options(melee_source PRIVATE -pie -rdynamic "-Wl,-rpath,\$ORIGIN/lib" -Wl,--enable-new-dtags)
add_custom_command(TARGET melee_source POST_BUILD
  COMMAND "${CMAKE_COMMAND}" -E copy_directory "${PORT}/slippi_sys" "$<TARGET_FILE_DIR:melee_source>/Sys"
  VERBATIM)
if(TARGET melee_game)
  add_dependencies(melee_source melee_game)
  add_custom_command(TARGET melee_source POST_BUILD
    COMMAND "${CMAKE_COMMAND}" -E copy_if_different "$<TARGET_FILE:melee_game>" "$<TARGET_FILE_DIR:melee_source>/${MELEE_GAME_FILE}"
    VERBATIM)
elseif(MELEE_LINUX_GAME_LIBRARY AND EXISTS "${MELEE_LINUX_GAME_LIBRARY}")
  get_filename_component(MELEE_LINUX_GAME_DIR "${MELEE_LINUX_GAME_LIBRARY}" DIRECTORY)
  add_custom_command(TARGET melee_source POST_BUILD
    COMMAND "${CMAKE_COMMAND}" -E copy_if_different "${MELEE_LINUX_GAME_LIBRARY}" "$<TARGET_FILE_DIR:melee_source>/${MELEE_GAME_FILE}"
    VERBATIM)
endif()

# The launcher (MeleeUnlocked).
add_subdirectory(../launcher "${CMAKE_CURRENT_BINARY_DIR}/launcher")
