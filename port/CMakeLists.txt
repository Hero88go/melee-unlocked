# Native Melee port: recompiled guest code + host runtime.
cmake_minimum_required(VERSION 3.15)

# A Source Port with no Slippi code compiled or linked: -DMELEE_NO_SLIPPI=ON in a build directory of
# its own. Only melee_source is defined there, from the same runtime sources minus the Slippi layer
# (runtime_ns), so the translated game, the playback build, the launcher and the tests, which all
# need that layer or the generated translation, are simply not part of that build directory.
# Everything after this block is the normal build; the option being OFF changes nothing in it.
# When a setting of the runtime or of melee_source changes below, change it here as well.
option(MELEE_NO_SLIPPI "Build only melee_source, without the Slippi layer" OFF)
if(MELEE_NO_SLIPPI)
  message(STATUS "melee: CPU baseline ${MELEE_CPU_BASELINE}, Source Port without the Slippi layer")
  # Two generated headers are all this build reads from the translation folder (the HLE prototypes
  # and the game's symbol addresses); none of the translated code is compiled or linked.
  set(PORT_GEN "${CMAKE_CURRENT_SOURCE_DIR}/generated" CACHE PATH "Guest translation source directory")
  if(NOT EXISTS "${PORT_GEN}/hle_decls.h" OR NOT EXISTS "${PORT_GEN}/guest_symbols.h")
    message(FATAL_ERROR "port: ${PORT_GEN} has no hle_decls.h and guest_symbols.h; run python port/recomp/recomp.py --no-slippi")
  endif()
  file(GLOB RUNTIME_NS_SOURCES runtime/ppc/*.cpp runtime/hle/*.cpp runtime/host/*.cpp runtime/gx/*.cpp
       runtime/mu_net/*.cpp)   # mu_net: the project's own peer-to-peer session, when present
  list(REMOVE_ITEM RUNTIME_NS_SOURCES "${CMAKE_CURRENT_SOURCE_DIR}/runtime/ppc/leaf_stencils.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/runtime/ppc/leaf_translator.cpp" "${CMAKE_CURRENT_SOURCE_DIR}/runtime/ppc/ram_translator.cpp")
  foreach(ns_removed
      runtime/hle/exi_slippi.cpp runtime/hle/slippi_net.cpp runtime/hle/slippi_online.cpp
      runtime/hle/slippi_report.cpp runtime/hle/slippi_playback.cpp runtime/hle/native_practice.cpp
      runtime/hle/native_practice_model.cpp runtime/hle/native_replay_stream.cpp runtime/hle/jukebox.cpp
      runtime/host/hackpack_ai.cpp runtime/host/static_gecko.cpp)   # the Static Recomp's loader for the pack's AI block
    list(REMOVE_ITEM RUNTIME_NS_SOURCES "${CMAKE_CURRENT_SOURCE_DIR}/${ns_removed}")
  endforeach()
  option(MELEE_ENABLE_DLSS5 "Build experimental DLSS 5 neural rendering" OFF)
  if(NOT MELEE_ENABLE_DLSS5)
    list(REMOVE_ITEM RUNTIME_NS_SOURCES "${CMAKE_CURRENT_SOURCE_DIR}/runtime/gx/gx_dlss5.cpp")
    list(REMOVE_ITEM RUNTIME_NS_SOURCES "${CMAKE_CURRENT_SOURCE_DIR}/runtime/gx/gx_dlss5_scaling.cpp")
  endif()
  enable_language(C)
  file(GLOB ENET_SOURCES third_party/enet/*.c)
  add_library(port_enet STATIC ${ENET_SOURCES})
  target_include_directories(port_enet PUBLIC third_party/enet/include)
  target_compile_definitions(port_enet PRIVATE _CRT_SECURE_NO_WARNINGS _WINSOCK_DEPRECATED_NO_WARNINGS)
  target_link_libraries(port_enet PUBLIC ws2_32 winmm)
  add_library(port_libusb STATIC
    third_party/libusb/libusb/core.c
    third_party/libusb/libusb/descriptor.c
    third_party/libusb/libusb/hotplug.c
    third_party/libusb/libusb/io.c
    third_party/libusb/libusb/strerror.c
    third_party/libusb/libusb/sync.c
    third_party/libusb/libusb/os/events_windows.c
    third_party/libusb/libusb/os/threads_windows.c
    third_party/libusb/libusb/os/windows_common.c
    third_party/libusb/libusb/os/windows_usbdk.c
    third_party/libusb/libusb/os/windows_winusb.c)
  target_include_directories(port_libusb PUBLIC third_party/libusb/libusb PRIVATE third_party/libusb/msvc)
  target_compile_definitions(port_libusb PRIVATE _CRT_SECURE_NO_WARNINGS)
  target_link_libraries(port_libusb PUBLIC advapi32 ole32 setupapi)
  file(STRINGS "${PROJECT_SOURCE_DIR}/VERSION" MELEE_PORT_VERSION LIMIT_COUNT 1)
  set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${PROJECT_SOURCE_DIR}/VERSION")
  string(REPLACE "." ";" MELEE_VERSION_PARTS "${MELEE_PORT_VERSION}")
  list(LENGTH MELEE_VERSION_PARTS MELEE_VERSION_PART_COUNT)
  while(MELEE_VERSION_PART_COUNT LESS 4)
    list(APPEND MELEE_VERSION_PARTS "0")
    list(LENGTH MELEE_VERSION_PARTS MELEE_VERSION_PART_COUNT)
  endwhile()
  list(SUBLIST MELEE_VERSION_PARTS 0 4 MELEE_VERSION_PARTS)
  list(JOIN MELEE_VERSION_PARTS "," MELEE_VERSION_COMMAS)

  add_library(runtime_ns STATIC ${RUNTIME_NS_SOURCES} app/source_noslippi.cpp
    third_party/monocypher/monocypher.c third_party/monocypher/monocypher-ed25519.c)   # mu_net: keys, signatures, packet sealing
  target_include_directories(runtime_ns PUBLIC third_party/monocypher)
  target_compile_definitions(runtime_ns PRIVATE MELEE_NO_SLIPPI MELEE_PORT_VERSION="${MELEE_PORT_VERSION}"
    MELEE_PLAYBACK_GCT_BASE=0u MELEE_PLAYBACK_GCT_SIZE=0u)
  if(WIN32)
    set(MELEE_ASIO_SOURCES third_party/asio/asio.cpp third_party/asio/asiodrivers.cpp third_party/asio/asiolist.cpp)
    target_sources(runtime_ns PRIVATE ${MELEE_ASIO_SOURCES})
    set_source_files_properties(${MELEE_ASIO_SOURCES} PROPERTIES COMPILE_OPTIONS "/UUNICODE;/U_UNICODE;/W0")
    target_link_libraries(runtime_ns PUBLIC advapi32)
  endif()
  target_include_directories(runtime_ns PUBLIC runtime/ppc runtime/host runtime/hle runtime/gx runtime/mu_net
    "${PORT_GEN}" "${PROJECT_SOURCE_DIR}/native" third_party)
  target_compile_features(runtime_ns PUBLIC cxx_std_17)
  target_compile_options(runtime_ns PRIVATE /EHsc ${MELEE_ARCH_FLAG} /fp:precise /W3)
  target_compile_definitions(runtime_ns PRIVATE NOMINMAX _CRT_SECURE_NO_WARNINGS _SILENCE_CXX17_CODECVT_HEADER_DEPRECATION_WARNING)
  target_link_libraries(runtime_ns PUBLIC native_animation port_enet port_libusb bcrypt comdlg32 d3d12 d3d11 dxgi d3dcompiler xinput9_1_0 winmm user32 gdi32 setupapi winusb hid mfplat mfreadwrite mfuuid ole32 shell32)

  add_executable(melee_source WIN32 app/main.cpp app/settings_window.cpp app/source_host.cpp app/source_guest_boundary.cpp
    app/source_p2p.cpp   # the --p2p-* command line: one mu_net session on the game's command channel
    app/melee_unlocked.rc app/utf8.manifest)
  target_include_directories(melee_source PRIVATE runtime/abi app)
  target_link_libraries(melee_source PRIVATE runtime_ns dbghelp)
  target_link_options(melee_source PRIVATE /MAP:$<TARGET_FILE_DIR:melee_source>/melee_source.map)
  target_compile_options(melee_source PRIVATE /EHsc)
  target_compile_definitions(melee_source PRIVATE MELEE_SOURCE_PORT MELEE_NO_SLIPPI NOMINMAX _CRT_SECURE_NO_WARNINGS
    MELEE_PORT_VERSION="${MELEE_PORT_VERSION}" MELEE_CPU_BASELINE_NAME="${MELEE_CPU_BASELINE}"
    MELEE_VERSION_COMMAS=${MELEE_VERSION_COMMAS})
  add_test(NAME source_card_roundtrip COMMAND melee_source --hidden --volume 0
    --log-file "${CMAKE_CURRENT_BINARY_DIR}/source-card-roundtrip.log"
    --card-self-test "${CMAKE_CURRENT_BINARY_DIR}/source-card-roundtrip")

  # The D3D12 ray-tracing library, beside the executable (the normal build copies it with melee_port).
  set(MELEE_DXC_HINT "C:/Program Files (x86)/Windows Kits/10/bin/${CMAKE_VS_WINDOWS_TARGET_PLATFORM_VERSION}/x64")
  find_program(MELEE_DXC_EXECUTABLE NAMES dxc.exe HINTS "${MELEE_DXC_HINT}")
  if(NOT MELEE_DXC_EXECUTABLE)
    message(FATAL_ERROR "The Windows SDK DXC compiler (dxc.exe) is required to build the DXR path shader")
  endif()
  set(MELEE_DXR_DXIL "${CMAKE_CURRENT_BINARY_DIR}/dxr_pathtrace.dxil")
  add_custom_command(OUTPUT "${MELEE_DXR_DXIL}"
    COMMAND "${MELEE_DXC_EXECUTABLE}" -T lib_6_3 -HV 2021 -O3
            -Fo "${MELEE_DXR_DXIL}" "${CMAKE_CURRENT_SOURCE_DIR}/runtime/gx/shaders/dxr_pathtrace.hlsl"
    DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/runtime/gx/shaders/dxr_pathtrace.hlsl"
    VERBATIM)
  add_custom_target(melee_dxr_shader DEPENDS "${MELEE_DXR_DXIL}")
  add_dependencies(melee_source melee_dxr_shader)
  add_custom_command(TARGET melee_source POST_BUILD
    COMMAND "${CMAKE_COMMAND}" -E copy_if_different "${MELEE_DXR_DXIL}"
            "$<TARGET_FILE_DIR:melee_source>/dxr_pathtrace.dxil"
    VERBATIM)

  # Cache identity follows shader generation, constant layout and pipeline setup.
  set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS
    runtime/gx/gx_shader.cpp runtime/gx/gx_shader.h runtime/gx/gx_d3d12.cpp)
  file(SHA256 "${CMAKE_CURRENT_SOURCE_DIR}/runtime/gx/gx_shader.cpp" GX_SHADER_HASH)
  file(SHA256 "${CMAKE_CURRENT_SOURCE_DIR}/runtime/gx/gx_shader.h" GX_LAYOUT_HASH)
  file(SHA256 "${CMAKE_CURRENT_SOURCE_DIR}/runtime/gx/gx_d3d12.cpp" GX_PIPELINE_HASH)
  string(SHA256 GX_CACHE_HASH "${GX_SHADER_HASH}${GX_LAYOUT_HASH}${GX_PIPELINE_HASH}")
  target_compile_definitions(runtime_ns PRIVATE GX_SHADER_CACHE_VERSION="${GX_CACHE_HASH}")

  add_library(port_imgui STATIC
    third_party/imgui/imgui.cpp third_party/imgui/imgui_draw.cpp
    third_party/imgui/imgui_tables.cpp third_party/imgui/imgui_widgets.cpp
    third_party/imgui/backends/imgui_impl_dx12.cpp third_party/imgui/backends/imgui_impl_win32.cpp)
  target_include_directories(port_imgui PUBLIC third_party/imgui)
  target_compile_features(port_imgui PUBLIC cxx_std_17)
  target_compile_options(port_imgui PRIVATE ${MELEE_ARCH_FLAG})
  target_link_libraries(port_imgui PUBLIC d3d12 dxgi d3dcompiler dwmapi)
  target_compile_definitions(runtime_ns PRIVATE GX_PC_SETTINGS)
  target_link_libraries(runtime_ns PUBLIC port_imgui)

  # NVIDIA Streamline, Intel XeSS and the experimental DLSS 5 helper: the same optional pieces as
  # the normal build, enabled only when their headers, import library and runtime DLLs are present.
  set(MELEE_STREAMLINE_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/third_party/streamline" CACHE PATH
      "NVIDIA Streamline SDK root (headers, import library, and x64 runtime plugins)")
  if(EXISTS "${MELEE_STREAMLINE_ROOT}/include/sl.h" AND
     EXISTS "${MELEE_STREAMLINE_ROOT}/lib/x64/sl.interposer.lib" AND
     EXISTS "${MELEE_STREAMLINE_ROOT}/bin/x64/sl.interposer.dll" AND
     EXISTS "${MELEE_STREAMLINE_ROOT}/bin/x64/sl.common.dll")
    target_compile_definitions(runtime_ns PRIVATE GX_STREAMLINE)
    target_include_directories(runtime_ns PRIVATE "${MELEE_STREAMLINE_ROOT}/include")
    target_link_libraries(runtime_ns PUBLIC "${MELEE_STREAMLINE_ROOT}/lib/x64/sl.interposer.lib" wintrust crypt32 delayimp)
    target_link_options(melee_source PRIVATE /DELAYLOAD:sl.interposer.dll)
    foreach(sl_dll sl.interposer.dll sl.common.dll sl.dlss.dll nvngx_dlss.dll sl.dlss_d.dll nvngx_dlssd.dll sl.dlss_g.dll nvngx_dlssg.dll sl.reflex.dll sl.pcl.dll)
      if(EXISTS "${MELEE_STREAMLINE_ROOT}/bin/x64/${sl_dll}")
        add_custom_command(TARGET melee_source POST_BUILD COMMAND ${CMAKE_COMMAND} -E copy_if_different
          "${MELEE_STREAMLINE_ROOT}/bin/x64/${sl_dll}" "$<TARGET_FILE_DIR:melee_source>/${sl_dll}")
      endif()
    endforeach()
    if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/third_party/xess/bin/libxess.dll")
      add_custom_command(TARGET melee_source POST_BUILD COMMAND ${CMAKE_COMMAND} -E copy_if_different
        "${CMAKE_CURRENT_SOURCE_DIR}/third_party/xess/bin/libxess.dll" "$<TARGET_FILE_DIR:melee_source>/libxess.dll")
    endif()
    if(MELEE_ENABLE_DLSS5)
      if(NOT EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/third_party/ngx/include/nvsdk_ngx.h")
        message(FATAL_ERROR "DLSS 5 requires the NGX SDK headers")
      endif()
      target_compile_definitions(runtime_ns PUBLIC GX_DLSS5)
      target_include_directories(runtime_ns PRIVATE third_party/ngx/include)
      add_library(dlss5_forwarder SHARED dlss5_forwarder/forwarder.cpp)
      target_compile_features(dlss5_forwarder PRIVATE cxx_std_17)
      target_compile_definitions(dlss5_forwarder PRIVATE NOMINMAX)
      target_include_directories(dlss5_forwarder PRIVATE third_party/ngx/include)
      target_link_libraries(dlss5_forwarder PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/third_party/ngx/lib/nvsdk_ngx_d.lib" advapi32 user32)
      target_link_options(dlss5_forwarder PRIVATE /MAP:$<TARGET_FILE_DIR:dlss5_forwarder>/dlss5_forwarder.map)
      set_target_properties(dlss5_forwarder PROPERTIES OUTPUT_NAME "nvngx.dll_meleedlss5" PREFIX "" SUFFIX ".dll")
      add_dependencies(melee_source dlss5_forwarder)
      add_custom_command(TARGET melee_source POST_BUILD COMMAND ${CMAKE_COMMAND} -E copy_if_different
        "$<TARGET_FILE:dlss5_forwarder>" "$<TARGET_FILE_DIR:melee_source>/nvngx.dll_meleedlss5.dll")
    endif()
  elseif(EXISTS "${MELEE_STREAMLINE_ROOT}/include/sl.h")
    message(STATUS "NVIDIA Streamline runtime is incomplete; building without Streamline/DLSS")
  endif()
  if(MELEE_ENABLE_DLSS5 AND NOT TARGET dlss5_forwarder)
    message(FATAL_ERROR "DLSS 5 requires Streamline and NGX")
  endif()

  # The launcher for this build: the same sources and settings as the normal melee_unlocked target
  # further down, plus MELEE_NO_SLIPPI. With it the lobby needs no Slippi account, an accepted match
  # starts melee_source with --p2p-* (a peer-to-peer session), and automatic pairing is offered.
  # When the normal target's sources or settings change below, change them here as well.
  add_executable(melee_unlocked WIN32 app/launcher_mod_catalog.cpp runtime/host/mod_scan.cpp runtime/host/mod_profile.cpp app/launcher.cpp app/launcher_replay_data.cpp app/launcher_lobby.cpp app/launcher_lobby_p2p.cpp
    third_party/dht/dht.c third_party/monocypher/monocypher.c third_party/monocypher/monocypher-ed25519.c
    runtime/host/updater.cpp runtime/host/net_trace_read.cpp app/launcher_trace_view.cpp app/launcher.rc)
  set_target_properties(melee_unlocked PROPERTIES OUTPUT_NAME MeleeUnlockedLauncher)
  target_include_directories(melee_unlocked PRIVATE runtime/host runtime/ppc runtime/hle runtime/gx "${PORT_GEN}" third_party third_party/dht third_party/monocypher)
  target_compile_features(melee_unlocked PRIVATE cxx_std_17)
  target_compile_options(melee_unlocked PRIVATE /EHsc)
  target_compile_definitions(melee_unlocked PRIVATE MELEE_NO_SLIPPI NOMINMAX UNICODE _UNICODE _CRT_SECURE_NO_WARNINGS MELEE_PORT_VERSION="${MELEE_PORT_VERSION}"
    MELEE_VERSION_COMMAS=${MELEE_VERSION_COMMAS})
  target_link_libraries(melee_unlocked PRIVATE comctl32 winhttp ws2_32 bcrypt user32 gdi32 shell32 comdlg32 msimg32 uxtheme winmm)

  # The lobby's peer-to-peer match flow, as two (and three) peer lobbies in one process over
  # loopback: the match setup both launchers agree on, automatic pairing, the block list, and the
  # refusal an older launcher gets. The same test file as the normal build's port_launcher_lobby_p2p,
  # which with this define runs only those cases.
  add_executable(port_launcher_lobby_p2p_ns_test tests/launcher_lobby_p2p_test.cpp app/launcher_lobby_p2p.cpp
    third_party/dht/dht.c third_party/monocypher/monocypher.c third_party/monocypher/monocypher-ed25519.c)
  target_include_directories(port_launcher_lobby_p2p_ns_test PRIVATE app third_party third_party/dht third_party/monocypher)
  target_compile_features(port_launcher_lobby_p2p_ns_test PRIVATE cxx_std_17)
  target_compile_definitions(port_launcher_lobby_p2p_ns_test PRIVATE MELEE_NO_SLIPPI NOMINMAX WIN32_LEAN_AND_MEAN)
  target_compile_options(port_launcher_lobby_p2p_ns_test PRIVATE /EHsc)
  target_link_libraries(port_launcher_lobby_p2p_ns_test PRIVATE ws2_32 bcrypt user32 gdi32)
  # One ctest entry per case so they run side by side (each case waits on real timeouts); the
  # .list entry fails when this list and the test's own table drift apart.
  set(MELEE_P2P_NS_CASES p2p_setup p2p_setup_loss p2p_auto_two p2p_auto_three p2p_block p2p_old_protocol p2p_invite)
  foreach(p2p_case IN LISTS MELEE_P2P_NS_CASES)
    add_test(NAME port_launcher_lobby_p2p_ns.${p2p_case} COMMAND port_launcher_lobby_p2p_ns_test ${p2p_case})
  endforeach()
  add_test(NAME port_launcher_lobby_p2p_ns.list COMMAND port_launcher_lobby_p2p_ns_test --expect ${MELEE_P2P_NS_CASES})

  # Shared regression checks run against the standalone build as well.
  add_executable(port_offline_input_delay_ns_test tests/offline_input_delay_test.cpp)
  target_include_directories(port_offline_input_delay_ns_test PRIVATE runtime/host runtime/ppc third_party)
  target_compile_features(port_offline_input_delay_ns_test PRIVATE cxx_std_17)
  target_compile_options(port_offline_input_delay_ns_test PRIVATE /EHsc)
  target_compile_definitions(port_offline_input_delay_ns_test PRIVATE MELEE_NO_SLIPPI NOMINMAX _CRT_SECURE_NO_WARNINGS)
  add_test(NAME port_offline_input_delay_ns COMMAND port_offline_input_delay_ns_test)
  add_executable(port_stage_dat_safety_ns_test tests/stage_dat_safety_test.cpp)
  target_include_directories(port_stage_dat_safety_ns_test PRIVATE runtime/host runtime/ppc third_party)
  target_compile_features(port_stage_dat_safety_ns_test PRIVATE cxx_std_17)
  target_compile_options(port_stage_dat_safety_ns_test PRIVATE /EHsc)
  target_compile_definitions(port_stage_dat_safety_ns_test PRIVATE MELEE_NO_SLIPPI NOMINMAX _CRT_SECURE_NO_WARNINGS)
  add_test(NAME port_stage_dat_safety_ns COMMAND port_stage_dat_safety_ns_test)
  add_executable(port_user_gecko_ns_test tests/user_gecko_test.cpp)
  target_include_directories(port_user_gecko_ns_test PRIVATE runtime/host runtime/ppc third_party)
  target_compile_features(port_user_gecko_ns_test PRIVATE cxx_std_17)
  target_compile_options(port_user_gecko_ns_test PRIVATE /EHsc)
  target_compile_definitions(port_user_gecko_ns_test PRIVATE MELEE_NO_SLIPPI NOMINMAX _CRT_SECURE_NO_WARNINGS)
  add_test(NAME port_user_gecko_ns COMMAND port_user_gecko_ns_test)
  add_executable(port_disc_archive_ns_test tests/disc_archive_test.cpp)
  target_include_directories(port_disc_archive_ns_test PRIVATE runtime/host runtime/ppc third_party)
  target_compile_features(port_disc_archive_ns_test PRIVATE cxx_std_17)
  target_compile_options(port_disc_archive_ns_test PRIVATE /EHsc)
  target_compile_definitions(port_disc_archive_ns_test PRIVATE MELEE_NO_SLIPPI NOMINMAX _CRT_SECURE_NO_WARNINGS)
  add_test(NAME port_disc_archive_ns COMMAND port_disc_archive_ns_test)
  add_executable(port_disc_fst_paths_ns_test tests/disc_fst_paths_test.cpp runtime/host/disc_fst_paths.cpp)
  target_include_directories(port_disc_fst_paths_ns_test PRIVATE runtime/host runtime/ppc third_party)
  target_compile_features(port_disc_fst_paths_ns_test PRIVATE cxx_std_17)
  target_compile_options(port_disc_fst_paths_ns_test PRIVATE /EHsc)
  target_compile_definitions(port_disc_fst_paths_ns_test PRIVATE MELEE_NO_SLIPPI NOMINMAX _CRT_SECURE_NO_WARNINGS)
  add_test(NAME port_disc_fst_paths_ns COMMAND port_disc_fst_paths_ns_test)
  add_executable(port_cosmetic_mods_ns_test tests/cosmetic_mods_test.cpp runtime/host/cosmetic_mods.cpp)
  target_include_directories(port_cosmetic_mods_ns_test PRIVATE runtime/host runtime/ppc third_party)
  target_compile_features(port_cosmetic_mods_ns_test PRIVATE cxx_std_17)
  target_compile_options(port_cosmetic_mods_ns_test PRIVATE /EHsc)
  target_compile_definitions(port_cosmetic_mods_ns_test PRIVATE MELEE_NO_SLIPPI NOMINMAX _CRT_SECURE_NO_WARNINGS)
  add_test(NAME port_cosmetic_mods_ns COMMAND port_cosmetic_mods_ns_test)
  target_link_libraries(port_cosmetic_mods_ns_test PRIVATE bcrypt comdlg32 user32)
  add_executable(port_mu_net_ns_test tests/mu_net_test.cpp)
  target_include_directories(port_mu_net_ns_test PRIVATE runtime/mu_net)
  target_compile_features(port_mu_net_ns_test PRIVATE cxx_std_17)
  target_compile_options(port_mu_net_ns_test PRIVATE /EHsc /utf-8)
  target_compile_definitions(port_mu_net_ns_test PRIVATE NOMINMAX WIN32_LEAN_AND_MEAN)
  target_link_libraries(port_mu_net_ns_test PRIVATE runtime_ns)
  add_test(NAME port_mu_net_ns COMMAND port_mu_net_ns_test)
  return()
endif()
add_executable(port_launch_process_test tests/launch_process_test.cpp)
target_compile_features(port_launch_process_test PRIVATE cxx_std_17)
add_test(NAME port_launch_process COMMAND port_launch_process_test)
add_executable(port_native_replay_stream_test tests/native_replay_stream_test.cpp runtime/hle/native_replay_stream.cpp)
target_include_directories(port_native_replay_stream_test PRIVATE runtime/hle)
target_compile_features(port_native_replay_stream_test PRIVATE cxx_std_17)
add_test(NAME port_native_replay_stream COMMAND port_native_replay_stream_test)
add_executable(port_source_mod_overlay_test tests/source_mod_overlay_test.cpp runtime/host/source_mod_overlay.cpp)
target_include_directories(port_source_mod_overlay_test PRIVATE runtime/host)
target_compile_features(port_source_mod_overlay_test PRIVATE cxx_std_17)
target_compile_options(port_source_mod_overlay_test PRIVATE /EHsc)
add_test(NAME port_source_mod_overlay COMMAND port_source_mod_overlay_test)
add_executable(port_disc_archive_test tests/disc_archive_test.cpp)
target_include_directories(port_disc_archive_test PRIVATE runtime/host)
target_compile_features(port_disc_archive_test PRIVATE cxx_std_17)
add_test(NAME port_disc_archive COMMAND port_disc_archive_test)
add_executable(port_mod_profile_test tests/mod_profile_test.cpp runtime/host/mod_profile.cpp)
target_include_directories(port_mod_profile_test PRIVATE runtime/host)
target_compile_features(port_mod_profile_test PRIVATE cxx_std_17)
target_compile_options(port_mod_profile_test PRIVATE /EHsc)
add_test(NAME port_mod_profile COMMAND port_mod_profile_test)
add_executable(port_mod_scan_test tests/mod_scan_test.cpp runtime/host/mod_scan.cpp runtime/host/mod_profile.cpp)
target_include_directories(port_mod_scan_test PRIVATE runtime/host)
target_compile_features(port_mod_scan_test PRIVATE cxx_std_17)
target_compile_options(port_mod_scan_test PRIVATE /EHsc /utf-8)
target_compile_definitions(port_mod_scan_test PRIVATE MU_REPO_ROOT="${CMAKE_SOURCE_DIR}" NOMINMAX)
target_link_libraries(port_mod_scan_test PRIVATE bcrypt shell32)
add_test(NAME port_mod_scan COMMAND port_mod_scan_test)
add_executable(port_lcancel_flash_policy_test tests/lcancel_flash_policy_test.cpp)
target_include_directories(port_lcancel_flash_policy_test PRIVATE runtime/abi)
target_compile_features(port_lcancel_flash_policy_test PRIVATE cxx_std_17)
add_test(NAME port_lcancel_flash_policy COMMAND port_lcancel_flash_policy_test)
add_executable(port_mod_catalog_test tests/mod_catalog_test.cpp app/launcher_mod_catalog.cpp)
target_include_directories(port_mod_catalog_test PRIVATE app third_party)
target_compile_features(port_mod_catalog_test PRIVATE cxx_std_17)
target_compile_options(port_mod_catalog_test PRIVATE /EHsc /utf-8)
target_compile_definitions(port_mod_catalog_test PRIVATE NOMINMAX)
target_link_libraries(port_mod_catalog_test PRIVATE bcrypt)
add_test(NAME port_mod_catalog COMMAND port_mod_catalog_test)

add_executable(port_native_online_policy_test tests/native_online_policy_test.cpp)
target_include_directories(port_native_online_policy_test PRIVATE runtime/hle)
target_compile_features(port_native_online_policy_test PRIVATE cxx_std_17)
add_test(NAME port_native_online_policy COMMAND port_native_online_policy_test)
add_executable(port_static_mod_online_test tests/static_mod_online_test.cpp runtime/host/mod_profile.cpp)
target_include_directories(port_static_mod_online_test PRIVATE runtime/hle runtime/host)
target_compile_features(port_static_mod_online_test PRIVATE cxx_std_17)
target_compile_options(port_static_mod_online_test PRIVATE /EHsc)
target_link_libraries(port_static_mod_online_test PRIVATE bcrypt)
add_test(NAME port_static_mod_online COMMAND port_static_mod_online_test)
add_executable(port_native_slippi_bridge_test tests/native_slippi_bridge_test.cpp)
target_include_directories(port_native_slippi_bridge_test PRIVATE runtime/hle)
target_compile_features(port_native_slippi_bridge_test PRIVATE cxx_std_17)
add_test(NAME port_native_slippi_bridge COMMAND port_native_slippi_bridge_test)
add_executable(port_native_slippi_menu_payload_test tests/native_slippi_menu_payload_test.cpp)
target_include_directories(port_native_slippi_menu_payload_test PRIVATE runtime/hle)
target_compile_features(port_native_slippi_menu_payload_test PRIVATE cxx_std_17)
add_test(NAME port_native_slippi_menu_payload COMMAND port_native_slippi_menu_payload_test)
add_executable(port_native_state_layout_test tests/native_state_layout_test.cpp)
target_include_directories(port_native_state_layout_test PRIVATE runtime/hle runtime/abi)
target_compile_features(port_native_state_layout_test PRIVATE cxx_std_17)
add_test(NAME port_native_state_layout COMMAND port_native_state_layout_test)
add_executable(port_native_savestate_test tests/native_savestate_test.cpp runtime/hle/native_savestate.cpp)
target_include_directories(port_native_savestate_test PRIVATE runtime/hle runtime/abi)
target_compile_features(port_native_savestate_test PRIVATE cxx_std_17)
add_test(NAME port_native_savestate COMMAND port_native_savestate_test)
add_test(NAME port_slp_diff COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/../tools/test_slp_diff.py")
add_executable(port_launcher_engine_test tests/launcher_engine_test.cpp)
target_include_directories(port_launcher_engine_test PRIVATE app)
target_compile_features(port_launcher_engine_test PRIVATE cxx_std_17)
add_test(NAME port_launcher_engine COMMAND port_launcher_engine_test)
# The crash report zip: file list, per-file caps, the relay's 8 MB cap (synthetic crash folder in %TEMP%).
add_executable(port_launcher_crash_zip_test tests/launcher_crash_zip_test.cpp)
target_include_directories(port_launcher_crash_zip_test PRIVATE app)
target_compile_features(port_launcher_crash_zip_test PRIVATE cxx_std_17)
target_compile_options(port_launcher_crash_zip_test PRIVATE /EHsc)
target_compile_definitions(port_launcher_crash_zip_test PRIVATE PRIVACY_VECTORS="${CMAKE_CURRENT_SOURCE_DIR}/../tools/crash_relay/test/privacy_vectors.json")
add_test(NAME port_launcher_crash_zip COMMAND port_launcher_crash_zip_test)
add_executable(port_frame_queue_test tests/frame_queue_test.cpp)
target_include_directories(port_frame_queue_test PRIVATE runtime/gx)
target_compile_features(port_frame_queue_test PRIVATE cxx_std_17)
add_test(NAME port_frame_queue COMMAND port_frame_queue_test)
set_tests_properties(port_frame_queue PROPERTIES TIMEOUT 10)
add_executable(port_video_background_learning_test tests/video_background_learning_test.cpp)
target_include_directories(port_video_background_learning_test PRIVATE runtime/gx)
target_compile_features(port_video_background_learning_test PRIVATE cxx_std_17)
add_test(NAME port_video_background_learning COMMAND port_video_background_learning_test)

add_executable(port_settings_chord_test tests/settings_chord_test.cpp)
target_include_directories(port_settings_chord_test PRIVATE runtime/host)
target_compile_features(port_settings_chord_test PRIVATE cxx_std_17)
add_test(NAME port_settings_chord COMMAND port_settings_chord_test)

add_executable(port_settings_nav_test tests/settings_nav_test.cpp)
target_include_directories(port_settings_nav_test PRIVATE runtime/host)
target_compile_features(port_settings_nav_test PRIVATE cxx_std_17)
add_test(NAME port_settings_nav COMMAND port_settings_nav_test)

add_executable(port_net_trace_test tests/net_trace_test.cpp runtime/host/net_trace_file.cpp runtime/host/net_trace_read.cpp)
target_include_directories(port_net_trace_test PRIVATE runtime/host)
target_compile_features(port_net_trace_test PRIVATE cxx_std_17)
target_compile_options(port_net_trace_test PRIVATE /EHsc)
add_test(NAME port_net_trace COMMAND port_net_trace_test)
add_executable(port_replay_revisions_test tests/replay_revisions_test.cpp runtime/host/replay_revisions.cpp)
target_include_directories(port_replay_revisions_test PRIVATE runtime/host)
target_compile_features(port_replay_revisions_test PRIVATE cxx_std_17)
target_compile_options(port_replay_revisions_test PRIVATE /EHsc)
add_test(NAME port_replay_revisions COMMAND port_replay_revisions_test)
add_executable(port_user_gecko_test tests/user_gecko_test.cpp)
target_include_directories(port_user_gecko_test PRIVATE runtime/host runtime/ppc)
target_compile_features(port_user_gecko_test PRIVATE cxx_std_17)
target_compile_options(port_user_gecko_test PRIVATE /EHsc)
target_compile_definitions(port_user_gecko_test PRIVATE NOMINMAX _CRT_SECURE_NO_WARNINGS)
add_test(NAME port_user_gecko COMMAND port_user_gecko_test)
add_executable(port_unlock_code_policy_test tests/unlock_code_policy_test.cpp)
target_include_directories(port_unlock_code_policy_test PRIVATE runtime/hle)
target_compile_features(port_unlock_code_policy_test PRIVATE cxx_std_17)
add_test(NAME port_unlock_code_policy COMMAND port_unlock_code_policy_test)
add_executable(port_lcancel_indicator_test tests/lcancel_indicator_test.cpp runtime/host/lcancel.cpp)
target_include_directories(port_lcancel_indicator_test PRIVATE runtime/host runtime/ppc runtime/hle runtime/gx runtime/abi)
target_compile_features(port_lcancel_indicator_test PRIVATE cxx_std_17)
target_compile_options(port_lcancel_indicator_test PRIVATE /EHsc)
target_compile_definitions(port_lcancel_indicator_test PRIVATE NOMINMAX _CRT_SECURE_NO_WARNINGS)
add_test(NAME port_lcancel_indicator COMMAND port_lcancel_indicator_test)
add_executable(port_offline_input_delay_test tests/offline_input_delay_test.cpp)
target_include_directories(port_offline_input_delay_test PRIVATE runtime/host runtime/ppc)
target_compile_features(port_offline_input_delay_test PRIVATE cxx_std_17)
add_test(NAME port_offline_input_delay COMMAND port_offline_input_delay_test)

add_executable(port_pack_skin_rule_test tests/pack_skin_rule_test.cpp runtime/host/pack_skin_rule.cpp)
target_include_directories(port_pack_skin_rule_test PRIVATE runtime/host)
target_compile_features(port_pack_skin_rule_test PRIVATE cxx_std_17)
target_compile_options(port_pack_skin_rule_test PRIVATE /EHsc)
add_test(NAME port_pack_skin_rule COMMAND port_pack_skin_rule_test)

add_executable(port_hackpack_ai_test tests/hackpack_ai_test.cpp)
target_include_directories(port_hackpack_ai_test PRIVATE runtime/host)
target_compile_features(port_hackpack_ai_test PRIVATE cxx_std_17)
target_compile_options(port_hackpack_ai_test PRIVATE /EHsc)
add_test(NAME port_hackpack_ai COMMAND port_hackpack_ai_test)

add_executable(port_gd_melee_ui_motion_test tests/gd_melee_ui_motion_test.cpp)
target_include_directories(port_gd_melee_ui_motion_test PRIVATE runtime/gx)
target_compile_features(port_gd_melee_ui_motion_test PRIVATE cxx_std_17)
add_test(NAME port_gd_melee_ui_motion COMMAND port_gd_melee_ui_motion_test)
add_executable(port_radial_navigation_test tests/radial_navigation_test.cpp)
target_include_directories(port_radial_navigation_test PRIVATE runtime/gx)
target_compile_features(port_radial_navigation_test PRIVATE cxx_std_17)
add_test(NAME port_radial_navigation COMMAND port_radial_navigation_test)
add_executable(port_memory_range_test tests/memory_range_test.cpp)
target_include_directories(port_memory_range_test PRIVATE runtime/host)
target_compile_features(port_memory_range_test PRIVATE cxx_std_17)
add_test(NAME port_memory_ranges COMMAND port_memory_range_test)
add_test(NAME port_dol_validation COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/tests/dol_validation_test.py")
add_test(NAME port_release_integration COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/../tools/release_integration_test.py")
add_test(NAME port_gecko_layout COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/tests/gecko_layout_test.py")
add_test(NAME port_computed_return COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/tests/computed_return_test.py")
add_test(NAME port_cosmetic_import_diagnostic COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/tests/cosmetic_import_diagnostic_test.py")
add_test(NAME port_nucleus_vault_diagnostic COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/tests/nucleus_vault_diagnostic_test.py")
add_test(NAME port_stage_cosmetic_diagnostic COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/tests/stage_cosmetic_diagnostic_test.py")
add_test(NAME port_effect_cosmetic_diagnostic COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/tests/effect_cosmetic_diagnostic_test.py")
add_test(NAME port_companion_texture_diagnostic COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/tests/companion_texture_diagnostic_test.py")
add_executable(port_texture_test tests/texture_snapshot_test.cpp runtime/gx/gx_texture.cpp)
target_include_directories(port_texture_test PRIVATE runtime/gx)
target_compile_features(port_texture_test PRIVATE cxx_std_17)
add_test(NAME port_texture_snapshots COMMAND port_texture_test)
add_executable(port_ram_watch_test tests/ram_watch_test.cpp)
target_include_directories(port_ram_watch_test PRIVATE runtime/ppc)
target_compile_features(port_ram_watch_test PRIVATE cxx_std_17)
add_test(NAME port_ram_watch COMMAND port_ram_watch_test)
add_executable(port_draw_batch_test tests/draw_batch_test.cpp)
target_include_directories(port_draw_batch_test PRIVATE runtime/gx)
target_compile_features(port_draw_batch_test PRIVATE cxx_std_17)
add_test(NAME port_draw_batch COMMAND port_draw_batch_test)
add_executable(port_dxr_scene_test tests/dxr_scene_test.cpp runtime/gx/gx_dxr_scene.cpp)
target_include_directories(port_dxr_scene_test PRIVATE runtime/gx)
target_compile_features(port_dxr_scene_test PRIVATE cxx_std_17)
add_test(NAME port_dxr_scene COMMAND port_dxr_scene_test)
add_executable(port_ax_ucode_test tests/ax_ucode_test.cpp runtime/hle/ax_ucode.cpp)
target_include_directories(port_ax_ucode_test PRIVATE runtime/hle)
target_compile_features(port_ax_ucode_test PRIVATE cxx_std_17)
add_test(NAME port_ax_ucode COMMAND port_ax_ucode_test)
add_executable(port_hle_card_test tests/hle_card_lifecycle_test.cpp runtime/hle/hle_card.cpp)
target_include_directories(port_hle_card_test PRIVATE runtime/hle runtime/host runtime/ppc generated)
target_compile_features(port_hle_card_test PRIVATE cxx_std_17)
target_compile_definitions(port_hle_card_test PRIVATE NOMINMAX _CRT_SECURE_NO_WARNINGS)
add_test(NAME port_hle_card_lifecycle COMMAND port_hle_card_test)
add_executable(port_pad_report_test tests/pad_report_test.cpp runtime/host/playstation_pad.cpp runtime/host/hid_pad.cpp)
target_include_directories(port_pad_report_test PRIVATE runtime/host runtime/ppc)
target_compile_features(port_pad_report_test PRIVATE cxx_std_17)
target_compile_definitions(port_pad_report_test PRIVATE NOMINMAX _CRT_SECURE_NO_WARNINGS)
target_link_libraries(port_pad_report_test PRIVATE hid user32)
add_test(NAME port_pad_reports COMMAND port_pad_report_test)
add_executable(port_gc_adapter_reset_test tests/gc_adapter_reset_test.cpp)
target_include_directories(port_gc_adapter_reset_test PRIVATE runtime/host runtime/ppc third_party/libusb/libusb)
target_compile_features(port_gc_adapter_reset_test PRIVATE cxx_std_17)
target_compile_options(port_gc_adapter_reset_test PRIVATE /EHsc)
target_compile_definitions(port_gc_adapter_reset_test PRIVATE NOMINMAX _CRT_SECURE_NO_WARNINGS)
target_link_libraries(port_gc_adapter_reset_test PRIVATE port_libusb setupapi hid)
add_test(NAME port_gc_adapter_reset COMMAND port_gc_adapter_reset_test)
set_tests_properties(port_gc_adapter_reset PROPERTIES TIMEOUT 10)
add_executable(port_controller_profiles_test tests/controller_profiles_test.cpp runtime/host/controller_profiles.cpp)
target_include_directories(port_controller_profiles_test PRIVATE runtime/host)
target_compile_features(port_controller_profiles_test PRIVATE cxx_std_17)
add_test(NAME port_controller_profiles COMMAND port_controller_profiles_test)
add_executable(port_dolphin_profile_test tests/dolphin_profile_test.cpp runtime/host/dolphin_profile.cpp)
target_include_directories(port_dolphin_profile_test PRIVATE runtime/host)
target_compile_features(port_dolphin_profile_test PRIVATE cxx_std_17)
target_compile_definitions(port_dolphin_profile_test PRIVATE NOMINMAX)
target_link_libraries(port_dolphin_profile_test PRIVATE comdlg32 user32)
add_test(NAME port_dolphin_profile COMMAND port_dolphin_profile_test)
# The disc is the player's own: with MELEE_TEST_ISO set the test also draws four fighters from it.
add_executable(port_skin_thumbnail_test tests/skin_thumbnail_test.cpp tests/skin_thumbnail_stubs.cpp runtime/host/skin_thumbnail.cpp runtime/gx/gx_texture.cpp)
target_include_directories(port_skin_thumbnail_test PRIVATE runtime/host runtime/gx third_party)
target_compile_features(port_skin_thumbnail_test PRIVATE cxx_std_17)
target_compile_definitions(port_skin_thumbnail_test PRIVATE NOMINMAX _CRT_SECURE_NO_WARNINGS)
add_test(NAME port_skin_thumbnail COMMAND port_skin_thumbnail_test $ENV{MELEE_TEST_ISO})
add_executable(port_cosmetic_mods_test tests/cosmetic_mods_test.cpp runtime/host/cosmetic_mods.cpp)
target_include_directories(port_cosmetic_mods_test PRIVATE runtime/host runtime/ppc third_party)
target_compile_features(port_cosmetic_mods_test PRIVATE cxx_std_17)
target_compile_options(port_cosmetic_mods_test PRIVATE /EHsc)
target_compile_definitions(port_cosmetic_mods_test PRIVATE NOMINMAX _CRT_SECURE_NO_WARNINGS)
target_link_libraries(port_cosmetic_mods_test PRIVATE bcrypt comdlg32 user32)
add_test(NAME port_cosmetic_mods COMMAND port_cosmetic_mods_test)
add_executable(port_stage_dat_safety_test tests/stage_dat_safety_test.cpp)
target_include_directories(port_stage_dat_safety_test PRIVATE runtime/host)
target_compile_features(port_stage_dat_safety_test PRIVATE cxx_std_17)
target_compile_definitions(port_stage_dat_safety_test PRIVATE _CRT_SECURE_NO_WARNINGS)
add_test(NAME port_stage_dat_safety COMMAND port_stage_dat_safety_test)
add_executable(port_vcdiff_test tests/vcdiff_test.cpp runtime/host/vcdiff.cpp)
target_include_directories(port_vcdiff_test PRIVATE runtime/host)
target_compile_features(port_vcdiff_test PRIVATE cxx_std_17)
add_test(NAME port_vcdiff COMMAND port_vcdiff_test)
add_executable(port_subframe_test tests/subframe_test.cpp runtime/gx/subframe.cpp runtime/gx/gx_texture.cpp runtime/gx/gx_shader.cpp runtime/gx/authored_pose.cpp)
target_include_directories(port_subframe_test PRIVATE runtime/gx "${PROJECT_SOURCE_DIR}/native")
target_link_libraries(port_subframe_test PRIVATE native_animation)
target_compile_features(port_subframe_test PRIVATE cxx_std_17)
target_compile_definitions(port_subframe_test PRIVATE NOMINMAX)
add_test(NAME port_subframe COMMAND port_subframe_test)
set(PORT_LOAD_TEST "${CMAKE_CURRENT_BINARY_DIR}/generated/load_test.cpp")
add_custom_command(OUTPUT "${PORT_LOAD_TEST}"
  COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/tests/generate_load_test.py" "${PORT_LOAD_TEST}"
  DEPENDS tests/generate_load_test.py recomp/emit.py recomp/gekko.py)
add_executable(port_load_test "${PORT_LOAD_TEST}")
target_include_directories(port_load_test PRIVATE runtime/ppc)
target_compile_features(port_load_test PRIVATE cxx_std_17)
add_test(NAME port_signed_halfword_loads COMMAND port_load_test)

# The instruction set the translated game and the runtime are built for. AVX2 is the default and
# what every release so far has used; it needs Intel Haswell (2013) or AMD Ryzen. "AVX" reaches
# back to 2011, and "SSE2" is every x64 processor ever made. A player on a processor without AVX2
# cannot start the game at all, so this exists to build them something that runs.
# MELEE_CPU_BASELINE and MELEE_ARCH_FLAG are declared at the root CMakeLists.txt, before this
# subdirectory is added, so native_animation (built there) also gets the right /arch flag.
message(STATUS "melee: CPU baseline ${MELEE_CPU_BASELINE}")

set(PORT_GEN "${CMAKE_CURRENT_SOURCE_DIR}/generated" CACHE PATH "Guest translation source directory")
if(NOT EXISTS "${PORT_GEN}/guest_sources.cmake")
  message(FATAL_ERROR "port: generated code missing; run python port/recomp/recomp.py")
endif()
include("${PORT_GEN}/guest_sources.cmake")
# The memory card test above includes hle.h, which needs the generated hle_decls.h: follow
# PORT_GEN when the translation is somewhere other than port/generated.
target_include_directories(port_hle_card_test PRIVATE "${PORT_GEN}")

# Guest library: large generated translation units. A second checkout of the same generated code can
# link an existing guest.lib instead of compiling it again (several GB of objects).
set(MELEE_PREBUILT_GUEST "" CACHE FILEPATH "Link this guest.lib (built from identical port/generated) instead of compiling it")
if(MELEE_PREBUILT_GUEST)
  add_library(guest STATIC IMPORTED GLOBAL)
  set_target_properties(guest PROPERTIES IMPORTED_LOCATION "${MELEE_PREBUILT_GUEST}")
  target_include_directories(guest INTERFACE "${PORT_GEN}" "${CMAKE_CURRENT_SOURCE_DIR}/runtime/ppc")
  target_compile_features(guest INTERFACE cxx_std_17)
else()
add_library(guest STATIC ${GUEST_SOURCES} "${PORT_GEN}/function_names.cpp" "${PORT_GEN}/gecko_data.cpp")
target_include_directories(guest PUBLIC "${PORT_GEN}" runtime/ppc)
target_include_directories(guest PRIVATE runtime/hle)   # gecko_data.h: run-time optional code flags
target_compile_features(guest PUBLIC cxx_std_17)
target_compile_options(guest PRIVATE /bigobj /EHsc ${MELEE_ARCH_FLAG} /fp:precise /wd4102 /wd4101 /wd4189 /W1)
target_compile_definitions(guest PRIVATE NOMINMAX _CRT_SECURE_NO_WARNINGS)
endif()

# Host runtime: CPU helpers, memory, HLE, host services.
file(GLOB RUNTIME_SOURCES runtime/ppc/*.cpp runtime/hle/*.cpp runtime/host/*.cpp runtime/gx/*.cpp)
list(REMOVE_ITEM RUNTIME_SOURCES "${CMAKE_CURRENT_SOURCE_DIR}/runtime/ppc/leaf_stencils.cpp"
  "${CMAKE_CURRENT_SOURCE_DIR}/runtime/ppc/leaf_translator.cpp" "${CMAKE_CURRENT_SOURCE_DIR}/runtime/ppc/ram_translator.cpp")
option(MELEE_ENABLE_DLSS5 "Build experimental DLSS 5 neural rendering" OFF)
if(NOT MELEE_ENABLE_DLSS5)
  list(REMOVE_ITEM RUNTIME_SOURCES "${CMAKE_CURRENT_SOURCE_DIR}/runtime/gx/gx_dlss5.cpp")
  list(REMOVE_ITEM RUNTIME_SOURCES "${CMAKE_CURRENT_SOURCE_DIR}/runtime/gx/gx_dlss5_scaling.cpp")
else()
  add_executable(port_dlss5_scaling_test tests/dlss5_scaling_test.cpp runtime/gx/gx_dlss5_scaling.cpp)
  target_include_directories(port_dlss5_scaling_test PRIVATE runtime/gx)
  target_compile_features(port_dlss5_scaling_test PRIVATE cxx_std_17)
  target_compile_definitions(port_dlss5_scaling_test PRIVATE NOMINMAX)
  target_link_libraries(port_dlss5_scaling_test PRIVATE d3d12 dxgi d3dcompiler)
  add_test(NAME port_dlss5_scaling COMMAND port_dlss5_scaling_test)
  set_tests_properties(port_dlss5_scaling PROPERTIES TIMEOUT 60)
  add_executable(port_dlss5_pipeline_test tests/dlss5_pipeline_test.cpp runtime/gx/gx_dlss5_scaling.cpp)
  target_include_directories(port_dlss5_pipeline_test PRIVATE runtime/gx runtime/host runtime/ppc third_party/ngx/include)
  target_compile_features(port_dlss5_pipeline_test PRIVATE cxx_std_17)
  target_compile_definitions(port_dlss5_pipeline_test PRIVATE GX_DLSS5 NOMINMAX _CRT_SECURE_NO_WARNINGS)
  target_link_libraries(port_dlss5_pipeline_test PRIVATE d3d12 dxgi d3dcompiler)
  add_test(NAME port_dlss5_pipeline COMMAND port_dlss5_pipeline_test)
  set_tests_properties(port_dlss5_pipeline PROPERTIES TIMEOUT 60)
endif()
enable_language(C)
file(GLOB ENET_SOURCES third_party/enet/*.c)
add_library(port_enet STATIC ${ENET_SOURCES})
target_include_directories(port_enet PUBLIC third_party/enet/include)
target_compile_definitions(port_enet PRIVATE _CRT_SECURE_NO_WARNINGS _WINSOCK_DEPRECATED_NO_WARNINGS)
target_link_libraries(port_enet PUBLIC ws2_32 winmm)
# libusb, as Dolphin uses for the GameCube adapter. Talking to WinUSB directly meant an adapter on
# libusbK or libusb-win32 was invisible to us while Dolphin reported it detected, and players were
# being asked to swap drivers for hardware that already worked. libusb's Windows backend covers all
# three, so an adapter that works in Slippi works here.
add_library(port_libusb STATIC
  third_party/libusb/libusb/core.c
  third_party/libusb/libusb/descriptor.c
  third_party/libusb/libusb/hotplug.c
  third_party/libusb/libusb/io.c
  third_party/libusb/libusb/strerror.c
  third_party/libusb/libusb/sync.c
  third_party/libusb/libusb/os/events_windows.c
  third_party/libusb/libusb/os/threads_windows.c
  third_party/libusb/libusb/os/windows_common.c
  third_party/libusb/libusb/os/windows_usbdk.c
  third_party/libusb/libusb/os/windows_winusb.c)
target_include_directories(port_libusb PUBLIC third_party/libusb/libusb PRIVATE third_party/libusb/msvc)
target_compile_definitions(port_libusb PRIVATE _CRT_SECURE_NO_WARNINGS)
target_link_libraries(port_libusb PUBLIC advapi32 ole32 setupapi)
file(STRINGS "${PROJECT_SOURCE_DIR}/VERSION" MELEE_PORT_VERSION LIMIT_COUNT 1)
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${PROJECT_SOURCE_DIR}/VERSION")
# VERSIONINFO wants the version as four comma separated numbers, not the "0.6.2" string. An
# executable with no version resource at all has no company, product or description for Windows to
# show, which is part of what makes an unsigned binary look like a dropper to SmartScreen and
# Defender: players have had the launcher quarantined on download. This does not replace signing,
# but it is free and it is a prerequisite for it.
string(REPLACE "." ";" MELEE_VERSION_PARTS "${MELEE_PORT_VERSION}")
list(LENGTH MELEE_VERSION_PARTS MELEE_VERSION_PART_COUNT)
while(MELEE_VERSION_PART_COUNT LESS 4)
  list(APPEND MELEE_VERSION_PARTS "0")
  list(LENGTH MELEE_VERSION_PARTS MELEE_VERSION_PART_COUNT)
endwhile()
list(SUBLIST MELEE_VERSION_PARTS 0 4 MELEE_VERSION_PARTS)
list(JOIN MELEE_VERSION_PARTS "," MELEE_VERSION_COMMAS)
add_library(runtime STATIC ${RUNTIME_SOURCES} third_party/slippilib/SlippiGame.cpp)
target_compile_definitions(runtime PRIVATE MELEE_PORT_VERSION="${MELEE_PORT_VERSION}")
# ASIO output (audio mode 3): the Steinberg ASIO SDK host files only, under the SDK's GPL Version 3
# option (asio.cpp, asiodrivers.cpp, asiolist.cpp; see third_party/asio/README.txt). Windows only. The
# SDK's registry helpers use the ANSI API, so these three build without UNICODE.
if(WIN32)
  set(MELEE_ASIO_SOURCES third_party/asio/asio.cpp third_party/asio/asiodrivers.cpp third_party/asio/asiolist.cpp)
  target_sources(runtime PRIVATE ${MELEE_ASIO_SOURCES})
  set_source_files_properties(${MELEE_ASIO_SOURCES} PROPERTIES COMPILE_OPTIONS "/UUNICODE;/U_UNICODE;/W0")
  target_link_libraries(runtime PUBLIC advapi32)
endif()
# The replay code list a playback build was translated against, so it can tell a player when a
# replay comes from a different Slippi version instead of quietly diverging. Set these to the
# --extra-gct-base and the list's size when generating port/generated_playback; left at zero, the
# playback build says it has none, which is the honest answer for a build made without them.
set(MELEE_PLAYBACK_GCT_BASE "0" CACHE STRING "Guest address of the replay code list this playback build was translated for")
set(MELEE_PLAYBACK_GCT_SIZE "0" CACHE STRING "Size in bytes of that replay code list")
target_compile_definitions(runtime PRIVATE
  MELEE_PLAYBACK_GCT_BASE=${MELEE_PLAYBACK_GCT_BASE}u MELEE_PLAYBACK_GCT_SIZE=${MELEE_PLAYBACK_GCT_SIZE}u)
target_include_directories(runtime PUBLIC runtime/ppc runtime/host runtime/hle runtime/gx "${PORT_GEN}" "${PROJECT_SOURCE_DIR}/native" third_party)
target_compile_features(runtime PUBLIC cxx_std_17)
target_compile_options(runtime PRIVATE /EHsc ${MELEE_ARCH_FLAG} /fp:precise /W3)
target_compile_definitions(runtime PRIVATE NOMINMAX _CRT_SECURE_NO_WARNINGS _SILENCE_CXX17_CODECVT_HEADER_DEPRECATION_WARNING)
include(runtime/ppc/ram_translator.cmake)
target_link_libraries(runtime PUBLIC native_animation port_enet port_libusb bcrypt comdlg32 d3d12 d3d11 dxgi d3dcompiler xinput9_1_0 winmm user32 gdi32 setupapi winusb hid mfplat mfreadwrite mfuuid ole32 shell32)

# mu_net: the project's own peer-to-peer session (no Slippi code). Keys, signatures and packet
# sealing come from Monocypher, the transport is ENet. Built where its sources are present.
if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/runtime/mu_net/session_types.cpp" AND EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/third_party/monocypher/monocypher.c")
add_library(port_monocypher STATIC third_party/monocypher/monocypher.c third_party/monocypher/monocypher-ed25519.c)
target_include_directories(port_monocypher PUBLIC third_party/monocypher)
add_library(mu_net STATIC runtime/mu_net/session_types.cpp runtime/mu_net/peer_identity.cpp
  runtime/mu_net/session_security.cpp runtime/mu_net/input_transport.cpp runtime/mu_net/peer_connector.cpp
  runtime/mu_net/rollback_session.cpp runtime/mu_net/result_outbox.cpp runtime/mu_net/nat_discovery.cpp)
target_include_directories(mu_net PUBLIC runtime/mu_net)
target_compile_features(mu_net PUBLIC cxx_std_17)
target_compile_definitions(mu_net PRIVATE NOMINMAX WIN32_LEAN_AND_MEAN _CRT_SECURE_NO_WARNINGS)
target_compile_options(mu_net PRIVATE /EHsc /utf-8)
target_link_libraries(mu_net PUBLIC port_enet PRIVATE port_monocypher bcrypt)
add_executable(port_mu_net_test tests/mu_net_test.cpp)
target_compile_definitions(port_mu_net_test PRIVATE NOMINMAX WIN32_LEAN_AND_MEAN)
target_compile_options(port_mu_net_test PRIVATE /EHsc /utf-8)
target_link_libraries(port_mu_net_test PRIVATE mu_net)
add_test(NAME port_mu_net COMMAND port_mu_net_test)
endif()

# WIN32: no console window when the game is double-clicked or started from the launcher. Command-line
# use still works, because main() reattaches to a parent console when there is one (see app/main.cpp).
add_executable(melee_port WIN32 app/main.cpp app/settings_window.cpp app/melee_unlocked.rc app/utf8.manifest)
# The source port: the whole application (renderers, DLSS, sub-frame, input, panel) with the game
# coming from melee_game.dll, built from the decompiled sources by sourceport/game with GCC.
# melee_source_probe is the bare first stage (no window, no runtime), for chasing boot crashes.
set(NATIVE_RECORDING_CODES "${CMAKE_CURRENT_BINARY_DIR}/native_recording_codes.h")
add_custom_command(OUTPUT "${NATIVE_RECORDING_CODES}"
  COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/../tools/native_recording_codes.py" "${NATIVE_RECORDING_CODES}"
  DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/../tools/native_recording_codes.py"
          "${CMAKE_CURRENT_SOURCE_DIR}/recomp/gecko.py"
          "${CMAKE_CURRENT_SOURCE_DIR}/slippi_sys_general/GameSettings/GALE01r2.ini"
          "${CMAKE_CURRENT_SOURCE_DIR}/slippi_sys/GameSettings/GALE01r2.ini"
  VERBATIM)
add_executable(melee_source WIN32 app/main.cpp app/settings_window.cpp app/source_host.cpp app/source_guest_boundary.cpp app/melee_unlocked.rc app/utf8.manifest)
target_sources(melee_source PRIVATE "${NATIVE_RECORDING_CODES}")
target_include_directories(melee_source PRIVATE "${CMAKE_CURRENT_BINARY_DIR}")
target_include_directories(melee_source PRIVATE runtime/abi app)
target_link_libraries(melee_source PRIVATE runtime dbghelp)
# The --p2p-* command line: one mu_net session on the game's command channel, chosen at run time when
# those options are given. Without them nothing of it runs and Slippi play is as before.
target_sources(melee_source PRIVATE app/source_p2p.cpp)
target_link_libraries(melee_source PRIVATE mu_net)
# A link map lets the package audit verify no translated guest/interpreter objects enter Source.
target_link_options(melee_source PRIVATE /MAP:$<TARGET_FILE_DIR:melee_source>/melee_source.map)
target_compile_options(melee_source PRIVATE /EHsc)
target_compile_definitions(melee_source PRIVATE MELEE_SOURCE_PORT NOMINMAX _CRT_SECURE_NO_WARNINGS
  MELEE_PORT_VERSION="${MELEE_PORT_VERSION}" MELEE_CPU_BASELINE_NAME="${MELEE_CPU_BASELINE}"
  MELEE_VERSION_COMMAS=${MELEE_VERSION_COMMAS})
add_test(NAME source_card_roundtrip COMMAND melee_source --hidden --volume 0
  --log-file "${CMAKE_CURRENT_BINARY_DIR}/source-card-roundtrip.log"
  --card-self-test "${CMAKE_CURRENT_BINARY_DIR}/source-card-roundtrip")
add_executable(melee_source_probe app/main_source.cpp)
target_include_directories(melee_source_probe PRIVATE runtime/abi)
add_executable(port_mm_probe tests/mm_probe.cpp)
target_link_libraries(port_mm_probe PRIVATE runtime guest)
target_compile_options(port_mm_probe PRIVATE /EHsc)
target_compile_definitions(port_mm_probe PRIVATE NOMINMAX _CRT_SECURE_NO_WARNINGS)
add_executable(port_slippi_ticket_test tests/slippi_ticket_test.cpp)
target_link_libraries(port_slippi_ticket_test PRIVATE runtime guest)
target_compile_options(port_slippi_ticket_test PRIVATE /EHsc)
target_compile_definitions(port_slippi_ticket_test PRIVATE NOMINMAX _CRT_SECURE_NO_WARNINGS)
add_test(NAME port_slippi_ticket COMMAND port_slippi_ticket_test)
add_executable(port_slippi_login_test tests/slippi_login_test.cpp)
target_link_libraries(port_slippi_login_test PRIVATE runtime guest)
target_compile_options(port_slippi_login_test PRIVATE /EHsc)
target_compile_definitions(port_slippi_login_test PRIVATE NOMINMAX _CRT_SECURE_NO_WARNINGS)
add_test(NAME port_slippi_login COMMAND port_slippi_login_test)
add_executable(port_settings_load_test tests/settings_load_test.cpp)
target_link_libraries(port_settings_load_test PRIVATE runtime guest)
target_include_directories(port_settings_load_test PRIVATE runtime)
target_compile_options(port_settings_load_test PRIVATE /EHsc)
target_compile_definitions(port_settings_load_test PRIVATE NOMINMAX _CRT_SECURE_NO_WARNINGS)
add_test(NAME port_settings_load COMMAND port_settings_load_test)
target_link_libraries(melee_port PRIVATE runtime guest dbghelp)   # dbghelp: crash minidump
add_executable(port_static_gecko_test tests/static_gecko_test.cpp)
target_link_libraries(port_static_gecko_test PRIVATE runtime guest)
target_compile_options(port_static_gecko_test PRIVATE /EHsc)
target_compile_definitions(port_static_gecko_test PRIVATE NOMINMAX _CRT_SECURE_NO_WARNINGS)
add_test(NAME port_static_gecko COMMAND port_static_gecko_test)
set_tests_properties(port_static_gecko PROPERTIES TIMEOUT 30)
# Holds native routines (a GCC-built parity library, see sourceport/game) to the translated game bit
# for bit, routine by routine. Needs the disc.
add_executable(port_native_parity tests/native_parity.cpp)
target_link_libraries(port_native_parity PRIVATE runtime guest)
target_include_directories(port_native_parity PRIVATE runtime)
target_compile_options(port_native_parity PRIVATE /EHsc)
target_compile_definitions(port_native_parity PRIVATE NOMINMAX _CRT_SECURE_NO_WARNINGS)
target_compile_options(melee_port PRIVATE /EHsc)
target_compile_definitions(melee_port PRIVATE NOMINMAX _CRT_SECURE_NO_WARNINGS MELEE_PORT_VERSION="${MELEE_PORT_VERSION}"
  MELEE_CPU_BASELINE_NAME="${MELEE_CPU_BASELINE}"
  MELEE_VERSION_COMMAS=${MELEE_VERSION_COMMAS})

# Compile the D3D12 ray-tracing library with the matching Windows SDK DXC. Keeping DXIL beside the
# executable makes the D3D12 renderer independent of the user's developer tools; the package tool
# includes this binary with the game files. Shader Model 6.3 keeps the pass usable on DXR 1.0 RTX
# adapters as well as newer cards.
set(MELEE_DXC_HINT "C:/Program Files (x86)/Windows Kits/10/bin/${CMAKE_VS_WINDOWS_TARGET_PLATFORM_VERSION}/x64")
find_program(MELEE_DXC_EXECUTABLE NAMES dxc.exe HINTS "${MELEE_DXC_HINT}")
if(NOT MELEE_DXC_EXECUTABLE)
  message(FATAL_ERROR "The Windows SDK DXC compiler (dxc.exe) is required to build the DXR path shader")
endif()
set(MELEE_DXR_DXIL "${CMAKE_CURRENT_BINARY_DIR}/dxr_pathtrace.dxil")
add_custom_command(OUTPUT "${MELEE_DXR_DXIL}"
  COMMAND "${MELEE_DXC_EXECUTABLE}" -T lib_6_3 -HV 2021 -O3
          -Fo "${MELEE_DXR_DXIL}" "${CMAKE_CURRENT_SOURCE_DIR}/runtime/gx/shaders/dxr_pathtrace.hlsl"
  DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/runtime/gx/shaders/dxr_pathtrace.hlsl"
  VERBATIM)
add_custom_target(melee_dxr_shader DEPENDS "${MELEE_DXR_DXIL}")
add_dependencies(melee_port melee_dxr_shader)
add_custom_command(TARGET melee_port POST_BUILD
  COMMAND "${CMAKE_COMMAND}" -E copy_if_different "${MELEE_DXR_DXIL}"
          "$<TARGET_FILE_DIR:melee_port>/dxr_pathtrace.dxil"
  VERBATIM)
if(MELEE_CPU_BASELINE STREQUAL "AVX2")
  target_compile_definitions(melee_port PRIVATE MELEE_NEEDS_AVX2)
endif()

# Melee Unlocked client: Win32 launcher (Play page, Build tab, self-update). No runtime dependency.
add_executable(melee_unlocked WIN32 app/launcher_mod_catalog.cpp runtime/host/mod_scan.cpp runtime/host/mod_profile.cpp app/launcher.cpp app/launcher_replay_data.cpp app/launcher_lobby.cpp app/launcher_lobby_p2p.cpp
  third_party/dht/dht.c third_party/monocypher/monocypher.c third_party/monocypher/monocypher-ed25519.c
  runtime/host/updater.cpp runtime/host/net_trace_read.cpp app/launcher_trace_view.cpp app/launcher.rc)
set_target_properties(melee_unlocked PROPERTIES OUTPUT_NAME MeleeUnlockedLauncher)
add_executable(port_launcher_replay_data_test tests/launcher_replay_data_test.cpp app/launcher_replay_data.cpp)
target_include_directories(port_launcher_replay_data_test PRIVATE app third_party)
target_compile_features(port_launcher_replay_data_test PRIVATE cxx_std_17)
target_compile_options(port_launcher_replay_data_test PRIVATE /EHsc)
add_test(NAME port_launcher_replay_data COMMAND port_launcher_replay_data_test)
add_executable(port_launcher_trace_view_test tests/launcher_trace_view_test.cpp
  app/launcher_trace_view.cpp runtime/host/net_trace_read.cpp)
target_include_directories(port_launcher_trace_view_test PRIVATE app runtime/host)
target_compile_features(port_launcher_trace_view_test PRIVATE cxx_std_17)
target_compile_options(port_launcher_trace_view_test PRIVATE /EHsc)
add_test(NAME port_launcher_trace_view COMMAND port_launcher_trace_view_test)
add_executable(port_hps_bound_test tests/hps_bound_test.cpp)
target_include_directories(port_hps_bound_test PRIVATE runtime/hle runtime/host runtime/ppc)
target_compile_features(port_hps_bound_test PRIVATE cxx_std_17)
target_compile_options(port_hps_bound_test PRIVATE /EHsc)
target_compile_definitions(port_hps_bound_test PRIVATE NOMINMAX _CRT_SECURE_NO_WARNINGS)
target_link_libraries(port_hps_bound_test PRIVATE mfplat mfreadwrite mfuuid ole32 shell32)
add_test(NAME port_hps_bound COMMAND port_hps_bound_test)
add_executable(port_disc_fst_paths_test tests/disc_fst_paths_test.cpp runtime/host/disc_fst_paths.cpp)
target_include_directories(port_disc_fst_paths_test PRIVATE runtime/host)
target_compile_features(port_disc_fst_paths_test PRIVATE cxx_std_17)
target_compile_options(port_disc_fst_paths_test PRIVATE /EHsc)
add_test(NAME port_disc_fst_paths COMMAND port_disc_fst_paths_test)
add_executable(port_jukebox_custom_music_test tests/jukebox_custom_music_test.cpp runtime/hle/jukebox.cpp)
target_include_directories(port_jukebox_custom_music_test PRIVATE runtime/hle runtime/host runtime/ppc)
target_compile_features(port_jukebox_custom_music_test PRIVATE cxx_std_17)
target_compile_options(port_jukebox_custom_music_test PRIVATE /EHsc)
target_compile_definitions(port_jukebox_custom_music_test PRIVATE NOMINMAX _CRT_SECURE_NO_WARNINGS)
target_link_libraries(port_jukebox_custom_music_test PRIVATE mfplat mfreadwrite mfuuid ole32 shell32)
add_test(NAME port_jukebox_custom_music COMMAND port_jukebox_custom_music_test)
add_executable(port_slippilib_bounds_test tests/slippilib_bounds_test.cpp third_party/slippilib/SlippiGame.cpp)
target_include_directories(port_slippilib_bounds_test PRIVATE third_party)
target_compile_features(port_slippilib_bounds_test PRIVATE cxx_std_17)
target_compile_options(port_slippilib_bounds_test PRIVATE /EHsc)
target_compile_definitions(port_slippilib_bounds_test PRIVATE NOMINMAX _CRT_SECURE_NO_WARNINGS _SILENCE_CXX17_CODECVT_HEADER_DEPRECATION_WARNING)
add_test(NAME port_slippilib_bounds COMMAND port_slippilib_bounds_test)
add_executable(port_updater_test tests/updater_test.cpp runtime/host/updater.cpp)
target_include_directories(port_updater_test PRIVATE runtime/host runtime/ppc runtime/hle runtime/gx "${PORT_GEN}" third_party)
target_compile_features(port_updater_test PRIVATE cxx_std_17)
target_compile_options(port_updater_test PRIVATE /EHsc)
target_compile_definitions(port_updater_test PRIVATE NOMINMAX UNICODE _UNICODE)
add_test(NAME port_updater COMMAND port_updater_test)
target_include_directories(melee_unlocked PRIVATE runtime/host runtime/ppc runtime/hle runtime/gx "${PORT_GEN}" third_party third_party/dht third_party/monocypher)
target_compile_features(melee_unlocked PRIVATE cxx_std_17)
target_compile_options(melee_unlocked PRIVATE /EHsc)
target_compile_definitions(melee_unlocked PRIVATE NOMINMAX UNICODE _UNICODE _CRT_SECURE_NO_WARNINGS MELEE_PORT_VERSION="${MELEE_PORT_VERSION}"
  MELEE_VERSION_COMMAS=${MELEE_VERSION_COMMAS})
target_link_libraries(melee_unlocked PRIVATE comctl32 winhttp ws2_32 bcrypt user32 gdi32 shell32 comdlg32 msimg32 uxtheme winmm)

# Run via lobby/test_client.py, which owns a temporary HTTP/UDP service and database.
add_executable(port_launcher_lobby_client_test tests/launcher_lobby_client_test.cpp app/launcher_lobby_p2p.cpp
  third_party/dht/dht.c third_party/monocypher/monocypher.c third_party/monocypher/monocypher-ed25519.c)
target_include_directories(port_launcher_lobby_client_test PRIVATE third_party third_party/dht third_party/monocypher)
target_compile_features(port_launcher_lobby_client_test PRIVATE cxx_std_17)
target_compile_definitions(port_launcher_lobby_client_test PRIVATE NOMINMAX UNICODE _UNICODE)
target_link_libraries(port_launcher_lobby_client_test PRIVATE winhttp ws2_32 bcrypt user32 gdi32 comctl32 winmm)

# Two-process, no-service lobby smoke; driven by lobby/test_peer_client.py.
add_executable(port_launcher_lobby_peer_test tests/launcher_lobby_peer_test.cpp app/launcher_lobby_p2p.cpp
  third_party/dht/dht.c third_party/monocypher/monocypher.c third_party/monocypher/monocypher-ed25519.c)
target_include_directories(port_launcher_lobby_peer_test PRIVATE third_party third_party/dht third_party/monocypher)
target_compile_features(port_launcher_lobby_peer_test PRIVATE cxx_std_17)
target_compile_definitions(port_launcher_lobby_peer_test PRIVATE NOMINMAX WIN32_LEAN_AND_MEAN)
target_link_libraries(port_launcher_lobby_peer_test PRIVATE ws2_32 bcrypt)

add_executable(port_launcher_lobby_peer_worker_test tests/launcher_lobby_peer_worker_test.cpp app/launcher_lobby_p2p.cpp
  third_party/dht/dht.c third_party/monocypher/monocypher.c third_party/monocypher/monocypher-ed25519.c)
target_include_directories(port_launcher_lobby_peer_worker_test PRIVATE third_party third_party/dht third_party/monocypher)
target_compile_features(port_launcher_lobby_peer_worker_test PRIVATE cxx_std_17)
target_compile_definitions(port_launcher_lobby_peer_worker_test PRIVATE NOMINMAX UNICODE _UNICODE)
target_link_libraries(port_launcher_lobby_peer_worker_test PRIVATE winhttp ws2_32 bcrypt user32 gdi32 comctl32 winmm)

# Two peer lobbies in one process over loopback (no DHT, no window): requests, refusals, loss, friends, mods.
add_executable(port_launcher_lobby_p2p_test tests/launcher_lobby_p2p_test.cpp app/launcher_lobby_p2p.cpp
  third_party/dht/dht.c third_party/monocypher/monocypher.c third_party/monocypher/monocypher-ed25519.c)
target_include_directories(port_launcher_lobby_p2p_test PRIVATE app third_party third_party/dht third_party/monocypher)
target_compile_features(port_launcher_lobby_p2p_test PRIVATE cxx_std_17)
target_compile_definitions(port_launcher_lobby_p2p_test PRIVATE NOMINMAX WIN32_LEAN_AND_MEAN)
target_compile_options(port_launcher_lobby_p2p_test PRIVATE /EHsc)
target_link_libraries(port_launcher_lobby_p2p_test PRIVATE ws2_32 bcrypt user32 gdi32)
# One ctest entry per case so they run side by side (each case waits on real timeouts); the .list
# entry fails when this list and the test's own table drift apart.
set(MELEE_P2P_CASES accept_flow cross_build_flow refusals crossed_requests one_way_loss_requests
    one_way_loss_answers clock_skew friends_by_code mods mod_match_disc private_accept_flow
    private_decline_and_block private_timeout private_old_versions private_routing private_crossed
    private_limits transport_side_by_side transport_old_peer transport_other_kind transport_rules)
foreach(p2p_case IN LISTS MELEE_P2P_CASES)
  add_test(NAME port_launcher_lobby_p2p.${p2p_case} COMMAND port_launcher_lobby_p2p_test ${p2p_case})
endforeach()
add_test(NAME port_launcher_lobby_p2p.list COMMAND port_launcher_lobby_p2p_test --expect ${MELEE_P2P_CASES})

# Diagnostic for a future serverless rendezvous transport; intentionally separate from the launcher.
# jech/dht is MIT licensed (third_party/dht/LICENCE) and only supplies BEP 5 endpoint discovery.
add_executable(port_lobby_dht_probe ../lobby/dht_probe.cpp third_party/dht/dht.c)
target_include_directories(port_lobby_dht_probe PRIVATE third_party/dht)
target_compile_features(port_lobby_dht_probe PRIVATE cxx_std_17)
target_compile_definitions(port_lobby_dht_probe PRIVATE WIN32_LEAN_AND_MEAN)
target_link_libraries(port_lobby_dht_probe PRIVATE ws2_32 bcrypt)

# Replay playback build: the same runtime with the game translated against the Slippi Playback
# code set (python port/recomp/recomp.py --sys-dir port/slippi_sys_playback --out port/generated_playback).
# A build dir may point it at another translation (a replay's own code list) with -DPORT_GEN_PLAYBACK.
if(NOT PORT_GEN_PLAYBACK)
  set(PORT_GEN_PLAYBACK "${CMAKE_CURRENT_SOURCE_DIR}/generated_playback")
endif()
if(EXISTS "${PORT_GEN_PLAYBACK}/guest_sources.cmake")
  include("${PORT_GEN_PLAYBACK}/guest_sources.cmake")
  set(MELEE_PREBUILT_GUEST_PLAYBACK "" CACHE FILEPATH "Link this guest_playback.lib (built from identical port/generated_playback) instead of compiling it")
  if(MELEE_PREBUILT_GUEST_PLAYBACK)
    add_library(guest_playback STATIC IMPORTED GLOBAL)
    set_target_properties(guest_playback PROPERTIES IMPORTED_LOCATION "${MELEE_PREBUILT_GUEST_PLAYBACK}")
    target_include_directories(guest_playback BEFORE INTERFACE "${PORT_GEN_PLAYBACK}")
    target_include_directories(guest_playback INTERFACE "${CMAKE_CURRENT_SOURCE_DIR}/runtime/ppc")
    target_compile_features(guest_playback INTERFACE cxx_std_17)
  else()
  add_library(guest_playback STATIC ${GUEST_SOURCES} "${PORT_GEN_PLAYBACK}/function_names.cpp" "${PORT_GEN_PLAYBACK}/gecko_data.cpp")
  target_include_directories(guest_playback BEFORE PUBLIC "${PORT_GEN_PLAYBACK}")
  target_include_directories(guest_playback PUBLIC runtime/ppc)
  target_include_directories(guest_playback PRIVATE runtime/hle)
  target_compile_features(guest_playback PUBLIC cxx_std_17)
  target_compile_options(guest_playback PRIVATE /bigobj /EHsc ${MELEE_ARCH_FLAG} /fp:precise /wd4102 /wd4101 /wd4189 /W1)
  target_compile_definitions(guest_playback PRIVATE NOMINMAX _CRT_SECURE_NO_WARNINGS)
  endif()
  # The .rc carries the icon and the version resource. This target never had it, so the playback
  # build shipped with a blank Explorer icon and no company or product name at all, which is the
  # profile an unsigned executable is judged on.
  add_executable(melee_port_playback app/main.cpp app/settings_window.cpp app/melee_unlocked.rc app/utf8.manifest)
  target_link_libraries(melee_port_playback PRIVATE runtime guest_playback dbghelp)
  target_compile_options(melee_port_playback PRIVATE /EHsc)
  # It ships in the release now, and the packager refuses an executable that does not report the
  # release's own version, so it needs the same definition the other builds get.
  target_compile_definitions(melee_port_playback PRIVATE NOMINMAX _CRT_SECURE_NO_WARNINGS
    MELEE_PORT_VERSION="${MELEE_PORT_VERSION}"
    MELEE_VERSION_COMMAS=${MELEE_VERSION_COMMAS}
    MELEE_RC_DESCRIPTION="Melee Unlocked Replay Playback"
    MELEE_RC_INTERNAL_NAME="melee_port_playback"
    MELEE_RC_ORIGINAL_FILENAME="melee_port_playback.exe")
endif()
if(EXISTS "${PROJECT_SOURCE_DIR}/melee/orig/GALE01/sys/main.dol")
  add_test(NAME port_image_identity COMMAND "${Python3_EXECUTABLE}"
    "${CMAKE_CURRENT_SOURCE_DIR}/tests/image_identity_test.py" "$<TARGET_FILE:melee_port>"
    "${PROJECT_SOURCE_DIR}/melee/orig/GALE01/sys/main.dol")
endif()

add_executable(port_gpu_resource_test tests/gpu_resource_test.cpp runtime/gx/gx_d3d12.cpp runtime/gx/gx_shader.cpp runtime/gx/gx_texture.cpp runtime/gx/gx_streamline.cpp runtime/gx/gx_xess.cpp runtime/gx/flicker_scan.cpp runtime/gx/texture_pack.cpp runtime/gx/gx_dxr_scene.cpp runtime/gx/gx_dxr_gpu_scene.cpp runtime/gx/gx_dxr_path_tracer.cpp)
target_include_directories(port_gpu_resource_test PRIVATE runtime/gx runtime/host runtime/ppc runtime/hle third_party)
target_compile_features(port_gpu_resource_test PRIVATE cxx_std_17)
target_compile_definitions(port_gpu_resource_test PRIVATE NOMINMAX _CRT_SECURE_NO_WARNINGS GX_SRV_HEAP_SIZE=32 GX_SAMPLER_HEAP_SIZE=16 GX_CONSTANT_PAGE_SIZE=4096)
target_link_libraries(port_gpu_resource_test PRIVATE d3d12 dxgi d3dcompiler user32)
add_test(NAME port_gpu_resources COMMAND port_gpu_resource_test)
set_tests_properties(port_gpu_resources PROPERTIES TIMEOUT 60)

add_executable(port_render_observer_test tests/render_observer_test.cpp runtime/gx/render_observer.cpp runtime/gx/gx_texture.cpp runtime/gx/authored_pose.cpp runtime/gx/subframe.cpp runtime/gx/gx_shader.cpp)
target_include_directories(port_render_observer_test PRIVATE runtime/ppc runtime/gx "${PROJECT_SOURCE_DIR}/native")
target_compile_features(port_render_observer_test PRIVATE cxx_std_17)
target_compile_definitions(port_render_observer_test PRIVATE NOMINMAX)
target_link_libraries(port_render_observer_test PRIVATE native_animation)
add_test(NAME port_render_observer COMMAND port_render_observer_test)

add_executable(port_source_boundary_test tests/source_boundary_test.cpp app/source_guest_boundary.cpp)
target_include_directories(port_source_boundary_test PRIVATE runtime/ppc runtime/hle runtime/host)
target_compile_features(port_source_boundary_test PRIVATE cxx_std_17)
target_compile_options(port_source_boundary_test PRIVATE /EHsc)
add_test(NAME port_source_boundary COMMAND port_source_boundary_test)

add_executable(port_backend_dispatch_test tests/backend_dispatch_test.cpp runtime/gx/gx_backend_dispatch.cpp)
target_include_directories(port_backend_dispatch_test PRIVATE runtime/gx)
target_compile_features(port_backend_dispatch_test PRIVATE cxx_std_17)
add_test(NAME port_backend_dispatch COMMAND port_backend_dispatch_test)

add_executable(port_native_draw_audit_test tests/native_draw_audit_test.cpp runtime/gx/native_draw_audit.cpp)
target_include_directories(port_native_draw_audit_test PRIVATE runtime/gx)
target_compile_features(port_native_draw_audit_test PRIVATE cxx_std_17)
add_test(NAME port_native_draw_audit COMMAND port_native_draw_audit_test)

add_executable(port_native_pose_bridge_test tests/native_pose_bridge_test.cpp runtime/gx/native_pose_bridge.cpp)
target_include_directories(port_native_pose_bridge_test PRIVATE runtime/gx runtime/abi "${PROJECT_SOURCE_DIR}/native")
target_compile_features(port_native_pose_bridge_test PRIVATE cxx_std_17)
add_test(NAME port_native_pose_bridge COMMAND port_native_pose_bridge_test)

# Cache identity follows shader generation, constant layout and pipeline setup.
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS
  runtime/gx/gx_shader.cpp runtime/gx/gx_shader.h runtime/gx/gx_d3d12.cpp)
file(SHA256 "${CMAKE_CURRENT_SOURCE_DIR}/runtime/gx/gx_shader.cpp" GX_SHADER_HASH)
file(SHA256 "${CMAKE_CURRENT_SOURCE_DIR}/runtime/gx/gx_shader.h" GX_LAYOUT_HASH)
file(SHA256 "${CMAKE_CURRENT_SOURCE_DIR}/runtime/gx/gx_d3d12.cpp" GX_PIPELINE_HASH)
string(SHA256 GX_CACHE_HASH "${GX_SHADER_HASH}${GX_LAYOUT_HASH}${GX_PIPELINE_HASH}")
foreach(cache_target runtime port_gpu_resource_test)
  target_compile_definitions(${cache_target} PRIVATE GX_SHADER_CACHE_VERSION="${GX_CACHE_HASH}")
endforeach()

add_library(port_imgui STATIC
  third_party/imgui/imgui.cpp third_party/imgui/imgui_draw.cpp
  third_party/imgui/imgui_tables.cpp third_party/imgui/imgui_widgets.cpp
  third_party/imgui/backends/imgui_impl_dx12.cpp third_party/imgui/backends/imgui_impl_win32.cpp)
target_include_directories(port_imgui PUBLIC third_party/imgui)
target_compile_features(port_imgui PUBLIC cxx_std_17)
target_compile_options(port_imgui PRIVATE ${MELEE_ARCH_FLAG})
target_link_libraries(port_imgui PUBLIC d3d12 dxgi d3dcompiler dwmapi)
target_compile_definitions(runtime PRIVATE GX_PC_SETTINGS)
target_link_libraries(runtime PUBLIC port_imgui)
# Lab view: feeds Slippi events through lab::feed and checks where the silhouettes land. Links the
# ImGui core only (no renderer), so it runs headless. With a .slp and a Lab folder as arguments it
# dumps draw data for tools/lab_render.py instead (see the test's header).
add_executable(port_lab_view_test tests/lab_view_test.cpp runtime/gx/lab_view.cpp
  third_party/imgui/imgui.cpp third_party/imgui/imgui_draw.cpp
  third_party/imgui/imgui_tables.cpp third_party/imgui/imgui_widgets.cpp)
target_include_directories(port_lab_view_test PRIVATE runtime/gx runtime/host runtime/ppc third_party third_party/imgui)
target_compile_features(port_lab_view_test PRIVATE cxx_std_17)
target_compile_definitions(port_lab_view_test PRIVATE NOMINMAX _CRT_SECURE_NO_WARNINGS)
add_test(NAME port_lab_view COMMAND port_lab_view_test)
set_tests_properties(port_lab_view PROPERTIES TIMEOUT 30)

# NVIDIA Streamline (DLSS): optional, enabled only when the headers, import library, and core
# redistributable DLLs are all present. A source archive may intentionally omit proprietary runtime
# binaries; that must produce a normal non-Streamline build rather than a post-link copy failure.
set(MELEE_STREAMLINE_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/third_party/streamline" CACHE PATH
    "NVIDIA Streamline SDK root (headers, import library, and x64 runtime plugins)")
if(EXISTS "${MELEE_STREAMLINE_ROOT}/include/sl.h" AND
   EXISTS "${MELEE_STREAMLINE_ROOT}/lib/x64/sl.interposer.lib" AND
   EXISTS "${MELEE_STREAMLINE_ROOT}/bin/x64/sl.interposer.dll" AND
   EXISTS "${MELEE_STREAMLINE_ROOT}/bin/x64/sl.common.dll")
  target_compile_definitions(runtime PRIVATE GX_STREAMLINE)
  target_include_directories(runtime PRIVATE "${MELEE_STREAMLINE_ROOT}/include")
  target_link_libraries(runtime PUBLIC "${MELEE_STREAMLINE_ROOT}/lib/x64/sl.interposer.lib" wintrust crypt32 delayimp)
  target_link_options(melee_port PRIVATE /DELAYLOAD:sl.interposer.dll)
  target_link_options(melee_source PRIVATE /DELAYLOAD:sl.interposer.dll)   # same folder, same DLLs
  foreach(sl_dll sl.interposer.dll sl.common.dll sl.dlss.dll nvngx_dlss.dll sl.dlss_d.dll nvngx_dlssd.dll sl.dlss_g.dll nvngx_dlssg.dll sl.reflex.dll sl.pcl.dll)
    if(EXISTS "${MELEE_STREAMLINE_ROOT}/bin/x64/${sl_dll}")
      add_custom_command(TARGET melee_port POST_BUILD COMMAND ${CMAKE_COMMAND} -E copy_if_different
        "${MELEE_STREAMLINE_ROOT}/bin/x64/${sl_dll}" "$<TARGET_FILE_DIR:melee_port>/${sl_dll}")
      add_custom_command(TARGET melee_source POST_BUILD COMMAND ${CMAKE_COMMAND} -E copy_if_different
        "${MELEE_STREAMLINE_ROOT}/bin/x64/${sl_dll}" "$<TARGET_FILE_DIR:melee_source>/${sl_dll}")
    endif()
  endforeach()
  # Intel XeSS (loaded at run time; missing file = no XeSS modes)
  add_custom_command(TARGET melee_port POST_BUILD COMMAND ${CMAKE_COMMAND} -E copy_if_different
    "${CMAKE_CURRENT_SOURCE_DIR}/third_party/xess/bin/libxess.dll" "$<TARGET_FILE_DIR:melee_port>/libxess.dll")
  add_custom_command(TARGET melee_source POST_BUILD COMMAND ${CMAKE_COMMAND} -E copy_if_different
    "${CMAKE_CURRENT_SOURCE_DIR}/third_party/xess/bin/libxess.dll" "$<TARGET_FILE_DIR:melee_source>/libxess.dll")
  # EXPERIMENTAL DLSS 5 Neural Rendering (gx_dlss5.h): NGX core from NVIDIA's DLSS SDK, plus the
  # forwarder DLL the model accepts as a caller. The model itself is never shipped.
  if(MELEE_ENABLE_DLSS5)
    if(NOT EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/third_party/ngx/include/nvsdk_ngx.h")
      message(FATAL_ERROR "DLSS 5 requires the NGX SDK headers")
    endif()
    target_compile_definitions(runtime PUBLIC GX_DLSS5)
    target_include_directories(runtime PRIVATE third_party/ngx/include)
    # The optional helper is the sole static NGX consumer. Host executables use its C ABI.
    add_library(dlss5_forwarder SHARED dlss5_forwarder/forwarder.cpp)
    target_compile_features(dlss5_forwarder PRIVATE cxx_std_17)
    target_compile_definitions(dlss5_forwarder PRIVATE NOMINMAX)
    target_include_directories(dlss5_forwarder PRIVATE third_party/ngx/include)
    target_link_libraries(dlss5_forwarder PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/third_party/ngx/lib/nvsdk_ngx_d.lib" advapi32 user32)
    target_link_options(dlss5_forwarder PRIVATE /MAP:$<TARGET_FILE_DIR:dlss5_forwarder>/dlss5_forwarder.map)
    set_target_properties(dlss5_forwarder PROPERTIES OUTPUT_NAME "nvngx.dll_meleedlss5" PREFIX "" SUFFIX ".dll")
    add_dependencies(melee_port dlss5_forwarder)
    add_dependencies(melee_source dlss5_forwarder)
    if(TARGET melee_port_playback)
      add_dependencies(melee_port_playback dlss5_forwarder)
    endif()
    add_dependencies(port_dlss5_pipeline_test dlss5_forwarder)
    add_custom_command(TARGET port_dlss5_pipeline_test POST_BUILD COMMAND ${CMAKE_COMMAND} -E copy_if_different
      "$<TARGET_FILE:dlss5_forwarder>" "$<TARGET_FILE_DIR:port_dlss5_pipeline_test>/nvngx.dll_meleedlss5.dll")
    foreach(ngx_host_target melee_port melee_port_playback port_dlss5_pipeline_test)
      if(TARGET ${ngx_host_target})
        target_link_options(${ngx_host_target} PRIVATE /MAP:$<TARGET_FILE_DIR:${ngx_host_target}>/$<TARGET_FILE_BASE_NAME:${ngx_host_target}>.map)
      endif()
    endforeach()
    add_custom_command(TARGET melee_port POST_BUILD COMMAND ${CMAKE_COMMAND} -E copy_if_different
      "$<TARGET_FILE:dlss5_forwarder>" "$<TARGET_FILE_DIR:melee_port>/nvngx.dll_meleedlss5.dll")
    add_custom_command(TARGET melee_source POST_BUILD COMMAND ${CMAKE_COMMAND} -E copy_if_different
      "$<TARGET_FILE:dlss5_forwarder>" "$<TARGET_FILE_DIR:melee_source>/nvngx.dll_meleedlss5.dll")
  endif()
elseif(EXISTS "${MELEE_STREAMLINE_ROOT}/include/sl.h")
  message(STATUS "NVIDIA Streamline runtime is incomplete; building without Streamline/DLSS")
endif()
if(MELEE_ENABLE_DLSS5 AND NOT TARGET dlss5_forwarder)
  message(FATAL_ERROR "DLSS 5 requires Streamline and NGX")
endif()
