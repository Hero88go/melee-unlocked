# Native Linux Source Port. No Windows PE loader or compatibility runtime.
cmake_minimum_required(VERSION 3.25)
find_package(Python3 COMPONENTS Interpreter REQUIRED)
find_package(Threads REQUIRED)
find_package(PkgConfig REQUIRED)
pkg_check_modules(LIBUSB REQUIRED IMPORTED_TARGET libusb-1.0)
option(MELEE_LINUX_GRAPHICS "Build SDL2/OpenGL graphics and ordinary gamepad support" ON)
set(MELEE_LINUX_GAME_LIBRARY "" CACHE FILEPATH "Use an already built native melee_game.so")
# The game library's instruction set (the same cache entry sourceport/game reads). The host is built
# for plain x86-64 so it can say a CPU lacks it instead of the game dying on its first AVX2 op.
set(MELEE_CPU_BASELINE "AVX2" CACHE STRING "Instruction set baseline of melee_game.so: AVX2, AVX or SSE2")

if(NOT MELEE_LINUX_GAME_LIBRARY)
  enable_language(C)
  add_subdirectory("${PROJECT_SOURCE_DIR}/sourceport/game" "${CMAKE_CURRENT_BINARY_DIR}/game")
endif()

file(STRINGS "${PROJECT_SOURCE_DIR}/VERSION" MELEE_LINUX_VERSION LIMIT_COUNT 1)
add_executable(melee_linux main.cpp host_services.cpp input_script.cpp gc_adapter.cpp "${PROJECT_SOURCE_DIR}/port/runtime/hle/ax_ucode.cpp")
target_compile_features(melee_linux PRIVATE cxx_std_17)
target_compile_definitions(melee_linux PRIVATE MELEE_PORT_VERSION="${MELEE_LINUX_VERSION}" MELEE_LINUX_USB MELEE_NO_SLIPPI
  MELEE_LINUX_CPU_BASELINE="${MELEE_CPU_BASELINE}")
target_compile_options(melee_linux PRIVATE -Wall -Wextra -fno-strict-aliasing -ffp-contract=on)
target_include_directories(melee_linux PRIVATE . ../runtime/abi ../runtime/hle ../runtime/host ../runtime/ppc ../runtime/gx "${PROJECT_SOURCE_DIR}/native")
target_link_libraries(melee_linux PRIVATE Threads::Threads ${CMAKE_DL_LIBS} m PkgConfig::LIBUSB)
# Keep the host PIE well away from the fixed console RAM and game image.
set_target_properties(melee_linux PROPERTIES POSITION_INDEPENDENT_CODE ON)
target_link_options(melee_linux PRIVATE -pie)

if(MELEE_LINUX_GRAPHICS)
  find_package(SDL2 REQUIRED)
  find_package(OpenGL REQUIRED)
  find_package(GLEW REQUIRED)
  find_program(MELEE_GLSLANG glslangValidator REQUIRED)
  find_program(MELEE_SPIRV_CROSS spirv-cross REQUIRED)
  target_compile_definitions(melee_linux PRIVATE MELEE_LINUX_SDL MELEE_LINUX_GRAPHICS)
  target_compile_definitions(melee_linux PRIVATE MELEE_LINUX_UNLOCKED)
  set(LINUX_HOST_FOBJ "${CMAKE_CURRENT_BINARY_DIR}/generated/FObjHost.cpp")
  add_custom_command(OUTPUT "${LINUX_HOST_FOBJ}"
    COMMAND "${Python3_EXECUTABLE}" "${PROJECT_SOURCE_DIR}/tools/generate_fobj_host.py" "${LINUX_HOST_FOBJ}"
    DEPENDS "${PROJECT_SOURCE_DIR}/tools/generate_fobj_host.py"
      "${PROJECT_SOURCE_DIR}/sourceport/extern/melee/src/sysdolphin/baselib/fobj.c"
    VERBATIM)
  target_sources(melee_linux PRIVATE renderer_gl.cpp gx_host_compat.cpp
    renderer_presenter.cpp "${LINUX_HOST_FOBJ}"
    ../runtime/gx/gx_core.cpp ../runtime/gx/gx_shader.cpp ../runtime/gx/gx_texture.cpp
    ../runtime/gx/native_draw_audit.cpp ../runtime/gx/native_pose_bridge.cpp
    ../runtime/gx/authored_pose.cpp ../runtime/gx/subframe.cpp)
  target_link_libraries(melee_linux PRIVATE SDL2::SDL2 OpenGL::GL GLEW::GLEW)
  find_program(MELEE_XVFB_RUN xvfb-run)
  if(MELEE_XVFB_RUN)
    add_test(NAME linux_gx_render
      COMMAND "${MELEE_XVFB_RUN}" -a $<TARGET_FILE:melee_linux> --graphics-self-test "${CMAKE_CURRENT_BINARY_DIR}/gx-test.ppm")
    add_test(NAME linux_unlocked_presenter
      COMMAND "${MELEE_XVFB_RUN}" -a $<TARGET_FILE:melee_linux> --presenter-self-test)
    set_tests_properties(linux_gx_render linux_unlocked_presenter PROPERTIES TIMEOUT 30 ENVIRONMENT "LIBGL_ALWAYS_SOFTWARE=1")
  endif()
endif()
# The whole Source Port (melee_source): every Windows feature on the shared host code.
set(MELEE_PORT_GEN "" CACHE PATH "The guest translation's generated headers (hle_decls.h, guest_symbols.h)")
if(MELEE_LINUX_GRAPHICS AND MELEE_PORT_GEN)
  add_subdirectory(full)
endif()
add_custom_command(TARGET melee_linux POST_BUILD
  COMMAND "${CMAKE_COMMAND}" -E make_directory "$<TARGET_FILE_DIR:melee_linux>/Sys/GC"
  COMMAND "${CMAKE_COMMAND}" -E copy_if_different
    "${PROJECT_SOURCE_DIR}/port/slippi_sys/GC/dsp_coef.bin" "$<TARGET_FILE_DIR:melee_linux>/Sys/GC/dsp_coef.bin"
  VERBATIM)

if(TARGET melee_game)
  add_dependencies(melee_linux melee_game)
  add_custom_command(TARGET melee_linux POST_BUILD
    COMMAND "${CMAKE_COMMAND}" -E copy_if_different "$<TARGET_FILE:melee_game>" "$<TARGET_FILE_DIR:melee_linux>/melee_game.so"
    VERBATIM)
elseif(MELEE_LINUX_GAME_LIBRARY AND EXISTS "${MELEE_LINUX_GAME_LIBRARY}")
  add_custom_command(TARGET melee_linux POST_BUILD
    COMMAND "${CMAKE_COMMAND}" -E copy_if_different "${MELEE_LINUX_GAME_LIBRARY}" "$<TARGET_FILE_DIR:melee_linux>/melee_game.so"
    VERBATIM)
endif()
add_test(NAME linux_host_version COMMAND melee_linux --version)
add_test(NAME linux_host_memory COMMAND melee_linux --memory-self-test)
add_test(NAME linux_host_services COMMAND melee_linux --host-self-test "${CMAKE_CURRENT_BINARY_DIR}/service-check")
add_executable(linux_gc_adapter_test tests/gc_adapter_test.cpp)
target_compile_features(linux_gc_adapter_test PRIVATE cxx_std_17)
target_include_directories(linux_gc_adapter_test PRIVATE ../runtime/abi)
target_link_libraries(linux_gc_adapter_test PRIVATE PkgConfig::LIBUSB Threads::Threads)
add_test(NAME linux_gc_adapter_decode COMMAND linux_gc_adapter_test)
add_executable(linux_input_script_test tests/input_script_test.cpp input_script.cpp)
target_compile_features(linux_input_script_test PRIVATE cxx_std_17)
target_include_directories(linux_input_script_test PRIVATE . ../runtime/abi)
add_test(NAME linux_input_scripts COMMAND linux_input_script_test
  "${PROJECT_SOURCE_DIR}/port/scripts/native_vs.txt" "${CMAKE_CURRENT_BINARY_DIR}/input-script-check")
install(TARGETS melee_linux RUNTIME DESTINATION .)
if(TARGET melee_game)
  install(TARGETS melee_game LIBRARY DESTINATION .)
endif()
