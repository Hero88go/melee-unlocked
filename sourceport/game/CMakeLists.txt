# The game, built from source as a library the host runtime loads.
#
# A separate project because it needs a different compiler from everything else in this repository
# (see ../cmake/mingw-w64-x86_64.cmake). The host talks to it through one exported entry point and
# two tables of function pointers, so nothing here links against the host and the host links against
# nothing here.
cmake_minimum_required(VERSION 3.25)
project(melee_game C CXX)

enable_testing()

if(NOT CMAKE_C_COMPILER_ID STREQUAL "GNU" OR CMAKE_C_COMPILER_VERSION VERSION_LESS 14)
  message(FATAL_ERROR "melee_game needs GCC 14 or newer: scalar_storage_order, and -ffp-contract=on meaning what the standard says")
endif()

get_filename_component(REPO "${CMAKE_CURRENT_SOURCE_DIR}/../.." ABSOLUTE)
# Assertions and the debug file name their source file. Without this they carry the full path of
# the folder the game was built in, which then shows up in every player's crash report.
add_compile_options("-ffile-prefix-map=${REPO}=.")
get_filename_component(MU_TOOLCHAIN_ROOT "${CMAKE_C_COMPILER}" DIRECTORY)
get_filename_component(MU_TOOLCHAIN_ROOT "${MU_TOOLCHAIN_ROOT}/.." ABSOLUTE)
add_compile_options("-ffile-prefix-map=${MU_TOOLCHAIN_ROOT}=toolchain")
set(MELEE_DECOMP_DIR "${CMAKE_CURRENT_SOURCE_DIR}/../extern/melee" CACHE PATH "checkout of the game sources")
set(D "${MELEE_DECOMP_DIR}")
set(SDK "${D}/libs/dolphin/src/dolphin")
if(NOT EXISTS "${D}/src/melee/gm/gmmain.c")
  message(FATAL_ERROR "melee_game: no game sources at ${D}")
endif()
# ---- how game code is compiled: one definition, shared by the game and by the parity library ----
add_library(game_flags INTERFACE)
target_include_directories(game_flags INTERFACE
  "${CMAKE_CURRENT_SOURCE_DIR}/include" "${D}/src" "${D}/src/MSL" "${D}/libs/dolphin/include" "${SDK}"
  "${CMAKE_CURRENT_SOURCE_DIR}/include_stub" "${REPO}/port/runtime/abi")
target_compile_definitions(game_flags INTERFACE MU_NATIVE VERSION_GALE01 BUILD_VERSION=0)
target_compile_options(game_flags INTERFACE
  -O2 -g
  -include "${CMAKE_CURRENT_SOURCE_DIR}/include/mu_native.h"   # only C targets link game_flags
  -mno-ms-bitfields          # the console compiler packs a u8 and a u16 bitfield into one run; the MS layout does not
  -fno-strict-aliasing       # the sources read floats through integer pointers throughout
  -fwrapv
  -fno-aggressive-loop-optimizations   # the sources read one entry past a table on a loop's last pass; GCC took that as proof the pass never runs and dropped it (Peach's third down special item)
  -fno-builtin               # no folding sin() at compile time or sqrt() into an instruction: the game's own maths decides
  -ffp-contract=on           # fuse a*b+c only inside one expression, as the console compiler did
  -fexec-charset=CP932       # the game's string literals are Shift-JIS at run time
  -w)
# M0 evidence build only: counts non-finite inputs to the maths routines (include/mu_math_audit.h).
option(MU_MATH_AUDIT "Count non-finite inputs to audited maths routines" OFF)
if(MU_MATH_AUDIT)
  target_compile_definitions(game_flags INTERFACE MU_MATH_AUDIT)
endif()
set(MELEE_CPU_BASELINE "AVX2" CACHE STRING "Instruction set baseline, matching the host: AVX2, AVX or SSE2")
if(MELEE_CPU_BASELINE STREQUAL "AVX2")
  target_compile_options(game_flags INTERFACE -mavx2 -mfma)
elseif(MELEE_CPU_BASELINE STREQUAL "AVX")
  target_compile_options(game_flags INTERFACE -mavx)
endif()
# Disc pointers are 32-bit slots holding real addresses, so everything the game can point at has to
# sit below 4 GB: the image at a fixed base with relocation off, MEM1 placed by the host.
set(LOW_IMAGE_LINK_OPTIONS -static-libgcc -static-libstdc++ -Wl,--disable-dynamicbase -Wl,--disable-high-entropy-va)

add_executable(mu_disc_layout_test EXCLUDE_FROM_ALL tests/disc_layout.c shim/mu_disc_layout.c)
set_target_properties(mu_disc_layout_test PROPERTIES C_STANDARD 11 C_EXTENSIONS ON)
target_link_libraries(mu_disc_layout_test PRIVATE game_flags)
add_test(NAME native_disc_layout COMMAND mu_disc_layout_test)

add_executable(mu_command_test EXCLUDE_FROM_ALL tests/command.c "${D}/src/melee/lb/lbcommand.c")
set_target_properties(mu_command_test PROPERTIES C_STANDARD 11 C_EXTENSIONS ON)
target_link_libraries(mu_command_test PRIVATE game_flags)
add_test(NAME native_command_loop COMMAND mu_command_test)

add_executable(mu_match_test EXCLUDE_FROM_ALL tests/match_override.c shim/mu_match.c)
set_target_properties(mu_match_test PROPERTIES C_STANDARD 11 C_EXTENSIONS ON)
target_link_libraries(mu_match_test PRIVATE game_flags)
target_include_directories(mu_match_test PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/include" "${REPO}/port/runtime/abi")
add_test(NAME native_match_override COMMAND mu_match_test)

# Run the exact production major runner with scene stubs, plus the content hook's real cache calls.
set(CONTENT_MAJOR_RUNNER "${CMAKE_CURRENT_BINARY_DIR}/content_major_runner.h")
if(MELEE_PYTHON)
  set(CONTENT_TEST_PYTHON "${MELEE_PYTHON}")
else()
  find_package(Python3 COMPONENTS Interpreter REQUIRED)
  set(CONTENT_TEST_PYTHON "${Python3_EXECUTABLE}")
endif()
add_custom_command(OUTPUT "${CONTENT_MAJOR_RUNNER}"
  COMMAND "${CONTENT_TEST_PYTHON}" "${REPO}/tools/native_major_fixture.py"
    "${D}/src/melee/gm/gm_1A3F.c" "${CONTENT_MAJOR_RUNNER}"
  DEPENDS "${REPO}/tools/native_major_fixture.py" "${D}/src/melee/gm/gm_1A3F.c"
  VERBATIM)
add_executable(mu_content_transition_test EXCLUDE_FROM_ALL
  tests/content_transition.c shim/mu_content.c "${CONTENT_MAJOR_RUNNER}")
set_target_properties(mu_content_transition_test PROPERTIES C_STANDARD 11 C_EXTENSIONS ON)
target_link_libraries(mu_content_transition_test PRIVATE game_flags)
target_include_directories(mu_content_transition_test PRIVATE "${CMAKE_CURRENT_BINARY_DIR}")
add_test(NAME native_content_transition COMMAND mu_content_transition_test)

if(NOT MU_NO_SLIPPI)   # tests the replay recording, which a MU_NO_SLIPPI tree does not have (option: see below)
add_executable(mu_record_start_test tests/record_start.c)
set_target_properties(mu_record_start_test PROPERTIES C_STANDARD 11 C_EXTENSIONS ON)
target_link_libraries(mu_record_start_test PRIVATE game_flags)
target_compile_options(mu_record_start_test PRIVATE -ffunction-sections -fdata-sections -fno-asynchronous-unwind-tables)
target_link_options(mu_record_start_test PRIVATE -Wl,--gc-sections)
add_test(NAME native_record_start COMMAND mu_record_start_test)
endif()

add_executable(mu_setjmp_test EXCLUDE_FROM_ALL tests/setjmp.c shim/mu_setjmp.c)
set_target_properties(mu_setjmp_test PROPERTIES C_STANDARD 11 C_EXTENSIONS ON)
target_link_libraries(mu_setjmp_test PRIVATE game_flags)
add_test(NAME native_setjmp COMMAND mu_setjmp_test)

add_executable(mu_alarm_test EXCLUDE_FROM_ALL tests/alarm.c shim/mu_os.c)
set_target_properties(mu_alarm_test PROPERTIES C_STANDARD 11 C_EXTENSIONS ON)
target_link_libraries(mu_alarm_test PRIVATE game_flags)
add_test(NAME native_alarm COMMAND mu_alarm_test)

# Exercise the actual private sound-bank bookkeeping without disc/renderer dependencies.
add_executable(mu_synth_bank_test EXCLUDE_FROM_ALL tests/synth_bank.c)
set_target_properties(mu_synth_bank_test PROPERTIES C_STANDARD 11 C_EXTENSIONS ON)
target_link_libraries(mu_synth_bank_test PRIVATE game_flags)
target_compile_options(mu_synth_bank_test PRIVATE -ffunction-sections -fdata-sections -fno-asynchronous-unwind-tables)
target_link_options(mu_synth_bank_test PRIVATE -Wl,--gc-sections)
add_test(NAME native_synth_bank COMMAND mu_synth_bank_test)

# Exercise the production AX voice service path when full and partial address
# updates are coalesced before the DSP copy runs.
add_executable(mu_ax_addr_sync_test tests/ax_addr_sync.c)
set_target_properties(mu_ax_addr_sync_test PROPERTIES C_STANDARD 11 C_EXTENSIONS ON)
target_link_libraries(mu_ax_addr_sync_test PRIVATE game_flags)
target_compile_options(mu_ax_addr_sync_test PRIVATE -ffunction-sections -fdata-sections -fno-asynchronous-unwind-tables)
target_link_options(mu_ax_addr_sync_test PRIVATE -Wl,--gc-sections)
add_test(NAME native_ax_addr_sync COMMAND mu_ax_addr_sync_test)

add_executable(mu_ax_commands_test tests/ax_commands.c)
set_target_properties(mu_ax_commands_test PROPERTIES C_STANDARD 11 C_EXTENSIONS ON)
target_link_libraries(mu_ax_commands_test PRIVATE game_flags)
target_compile_options(mu_ax_commands_test PRIVATE -ffunction-sections -fdata-sections -fno-asynchronous-unwind-tables)
target_link_options(mu_ax_commands_test PRIVATE -Wl,--gc-sections)
add_test(NAME native_ax_commands COMMAND mu_ax_commands_test)

add_executable(mu_synth_ratio_test tests/synth_ratio.c)
set_target_properties(mu_synth_ratio_test PROPERTIES C_STANDARD 11 C_EXTENSIONS ON)
target_link_libraries(mu_synth_ratio_test PRIVATE game_flags)
target_compile_options(mu_synth_ratio_test PRIVATE -ffunction-sections -fdata-sections -fno-asynchronous-unwind-tables)
target_link_options(mu_synth_ratio_test PRIVATE -Wl,--gc-sections)
add_test(NAME native_synth_ratio COMMAND mu_synth_ratio_test)

# Verify field-aware card payload byte-order conversion for save and name-tag data.
add_executable(mu_card_endian_test tests/card_endian.c "${D}/src/melee/lb/lbcardserial.c")
set_target_properties(mu_card_endian_test PROPERTIES C_STANDARD 11 C_EXTENSIONS ON)
target_link_libraries(mu_card_endian_test PRIVATE game_flags)
add_test(NAME native_card_endian COMMAND mu_card_endian_test)

# Every annotated DISC_STRUCT field sits at its console offset in the host layout (tools/disc_offset_audit.py).
# An 8-byte pointer field in a disc struct shifted Link's attributes from +0x94 and went unnoticed.
add_test(NAME native_disc_offsets COMMAND "${MELEE_PYTHON}" "${REPO}/tools/disc_offset_audit.py")

# ---- source implementations of assembly-only SDK maths ----
# Native game builds never consume generated PPC translations or a retail DOL.
# The retail comparison belongs exclusively to the host's parity test oracle.
add_library(sdk_math OBJECT math/sdk_math.c math/estimates.c)
target_link_libraries(sdk_math PRIVATE game_flags)
target_compile_options(sdk_math PRIVATE -ffp-contract=off)
set_target_properties(sdk_math PROPERTIES C_STANDARD 11 C_EXTENSIONS ON)

# ---- what is compiled ----
file(GLOB_RECURSE GAME_SOURCES CONFIGURE_DEPENDS "${D}/src/melee/*.c" "${D}/src/sysdolphin/*.c")
# The console's C library is kept only where its results reach the simulation: the maths, and rand.
set(LIBC_SOURCES "${D}/src/MSL/math.c" "${D}/src/MSL/math_1.c" "${D}/src/MSL/math_data.c" "${D}/src/MSL/trigf.c" "${D}/src/MSL/float.c" "${D}/src/MSL/rand.c")
# The halves of the SDK that sit above the hardware. What sat below it is the host's job.
file(GLOB SDK_SOURCES CONFIGURE_DEPENDS "${SDK}/gx/*.c" "${SDK}/ax/*.c" "${SDK}/axfx/*.c" "${SDK}/mtx/*.c"
  "${SDK}/thp/*.c")   # THP is a video decoder: computation, no hardware. MCC/FIO: see shim/mu_devkit.c
list(APPEND SDK_SOURCES "${SDK}/os/OSAlloc.c" "${SDK}/os/OSArena.c" "${SDK}/pad/PadClamp.c")
# Written for the hardware itself, or in its assembly; the native build has its own versions of these.
list(REMOVE_ITEM SDK_SOURCES "${SDK}/gx/GXFifo.c" "${SDK}/mtx/psmtx.c")
file(GLOB_RECURSE SHIM_SOURCES CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/shim/*.c")
# A game library with no Slippi code in it (-DMU_NO_SLIPPI=ON, its own build directory): the online
# menus, replay playback and recording, practice matchmaking and the online rule codes are left out.
# Three small files stand in for them: the entry points the game sources call (shim/mu_noslippi.c),
# the match block copy a network match starts from (shim/mu_match_info.c) and the menu-less entry
# into such a match (shim/mu_p2p_entry.c). The rollback engine itself (shim/mu_online*.c) stays.
# The library keeps its names: melee_game.dll, .dbg and .snapexcl.
option(MU_NO_SLIPPI "Build the game without the Slippi menus, replays and online codes" OFF)
set(MU_NO_SLIPPI_ONLY mu_noslippi.c mu_match_info.c mu_p2p_entry.c)
if(MU_NO_SLIPPI)
  foreach(removed mu_slippi_menu.c mu_slippi_css.c mu_slippi_chat.c mu_slippi_splash.c mu_slippi_sss.c
                  mu_replay.c mu_replay_abi.c mu_practice.c mu_online_rules.c)
    list(REMOVE_ITEM SHIM_SOURCES "${CMAKE_CURRENT_SOURCE_DIR}/shim/${removed}")
  endforeach()
  target_compile_definitions(game_flags INTERFACE MU_NO_SLIPPI)
else()
  foreach(removed ${MU_NO_SLIPPI_ONLY})
    list(REMOVE_ITEM SHIM_SOURCES "${CMAKE_CURRENT_SOURCE_DIR}/shim/${removed}")
  endforeach()
endif()
# Replacements for SDK files written against the hardware itself (see the REMOVE_ITEM above).
file(GLOB GXNATIVE_SOURCES CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/gxnative/*.c")
list(APPEND SHIM_SOURCES ${GXNATIVE_SOURCES})
# Akaneia's added fighters, native (akaneia/INTEGRATION.md): the registry and each fighter's own C.
# The fighters themselves are work in progress: they build only with -DMU_AKANEIA_FIGHTERS=ON, so a
# release never carries an unfinished fighter (the registry alone keeps every added fighter locked).
option(MU_AKANEIA_FIGHTERS "Build the work-in-progress native Akaneia fighters" OFF)
list(APPEND SHIM_SOURCES "${CMAKE_CURRENT_SOURCE_DIR}/akaneia/mu_ak_fighters.c")
if(MU_AKANEIA_FIGHTERS)
  target_compile_definitions(game_flags INTERFACE MU_AKANEIA_FIGHTERS=1)
  add_executable(mu_ak_character_kind_test EXCLUDE_FROM_ALL tests/ak_character_kind.c)
  set_target_properties(mu_ak_character_kind_test PROPERTIES C_STANDARD 11 C_EXTENSIONS ON)
  target_link_libraries(mu_ak_character_kind_test PRIVATE game_flags)
  target_compile_options(mu_ak_character_kind_test PRIVATE -ffunction-sections -fdata-sections -fno-asynchronous-unwind-tables)
  target_link_options(mu_ak_character_kind_test PRIVATE -Wl,--gc-sections)
  add_test(NAME native_ak_character_kind COMMAND mu_ak_character_kind_test)
  file(GLOB_RECURSE AKANEIA_SOURCES CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/akaneia/*/*.c")
  list(APPEND SHIM_SOURCES ${AKANEIA_SOURCES})
endif()
# A fighter joins the registry once akaneia/<fighter>/<fighter>.c exists (that file defines
# mu_ak_<fighter>); until then the registry leaves it out and the fighter stays locked.
foreach(ak_fighter wolf diddy charizard lucas sonic dedede tails)
  if(MU_AKANEIA_FIGHTERS AND (EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/akaneia/${ak_fighter}/${ak_fighter}.c"
      OR EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/akaneia/${ak_fighter}/ft${ak_fighter}.c"))
    string(TOUPPER "${ak_fighter}" ak_upper)
    set_property(SOURCE "${CMAKE_CURRENT_SOURCE_DIR}/akaneia/mu_ak_fighters.c" APPEND PROPERTY
      COMPILE_DEFINITIONS "MU_AK_HAVE_${ak_upper}")
  endif()
endforeach()

if(NOT Python3_EXECUTABLE)
  find_package(Python3 COMPONENTS Interpreter REQUIRED)
endif()

# ---- Training Mode CE, compiled natively (tmce/) ----
# TM-CE's own C, against MexTK headers regenerated for the native layouts (tools/tmce/gen_mextk.py).
# Each module is linked on its own as on the console, where each is a separate .dat file: its
# globals stay private, its exports get the module's prefix, and its MexTK calls are renamed to the
# decomp functions at the same console addresses (tools/tmce/link_module.py).
set(TMCE "${CMAKE_CURRENT_SOURCE_DIR}/tmce")
get_filename_component(TMCE_TOOLCHAIN_BIN "${CMAKE_C_COMPILER}" DIRECTORY)
# No debug info: TM-CE declares types with the decomp's names (ColorOverlay, CollData...), and the
# layout tools (tools/tmce/gdb_decomp_info.py) must find only the decomp's in melee_game.dbg.
set(TMCE_FLAGS -O2 -g0 -funsigned-char -fno-strict-aliasing -fwrapv -mno-ms-bitfields -mcmodel=small
  -fno-builtin -ffp-contract=on -w)
if(MELEE_CPU_BASELINE STREQUAL "AVX2")
  list(APPEND TMCE_FLAGS -mavx2 -mfma)
elseif(MELEE_CPU_BASELINE STREQUAL "AVX")
  list(APPEND TMCE_FLAGS -mavx)
endif()
set(TMCE_OBJECTS)
function(tmce_module name exports)
  cmake_parse_arguments(M "" "" "SOURCES;KEEP" ${ARGN})
  add_library(tmce_${name}_objs OBJECT ${M_SOURCES})
  target_compile_definitions(tmce_${name}_objs PRIVATE MU_NATIVE)
  target_include_directories(tmce_${name}_objs PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/include")  # mu_gxpipe.h
  target_compile_options(tmce_${name}_objs PRIVATE ${TMCE_FLAGS})
  set_target_properties(tmce_${name}_objs PROPERTIES C_STANDARD 11 C_EXTENSIONS ON)
  set(keep)
  foreach(k ${M_KEEP})
    list(APPEND keep --keep "${k}")
  endforeach()
  set(out "${CMAKE_CURRENT_BINARY_DIR}/tmce_${name}.o")
  add_custom_command(OUTPUT "${out}"
    COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/../../tools/tmce/link_module.py"
            --ld "${TMCE_TOOLCHAIN_BIN}/ld.exe" --objcopy "${CMAKE_OBJCOPY}" --name ${name} --exports "${exports}"
            ${keep} --redefine "${TMCE}/MexTK/mextk_redefine.txt" --out "${out}" $<TARGET_OBJECTS:tmce_${name}_objs>
    DEPENDS tmce_${name}_objs $<TARGET_OBJECTS:tmce_${name}_objs> "${TMCE}/MexTK/mextk_redefine.txt"
            "${CMAKE_CURRENT_SOURCE_DIR}/../../tools/tmce/link_module.py"
    COMMAND_EXPAND_LISTS VERBATIM)
  set(TMCE_OBJECTS ${TMCE_OBJECTS} "${out}" PARENT_SCOPE)
endfunction()
# The runtime: the loader, the C library shims, and the native parts written against TM-CE's own
# types (the original UnclePunch events in C, the on-screen displays).
file(GLOB TMCE_NATIVE_SOURCES CONFIGURE_DEPENDS "${TMCE}/native/*.c")
tmce_module(runtime - SOURCES ${TMCE_NATIVE_SOURCES} KEEP "mu_tmce_*" "mex_*" "MEX_*")
tmce_module(eventMenu "${TMCE}/MexTK/tmFunction.txt" SOURCES
  "${TMCE}/src/events.c" "${TMCE}/src/menu.c" "${TMCE}/src/osds.c" "${TMCE}/src/savestate_v1.c")
tmce_module(labCSS "${TMCE}/MexTK/cssFunction.txt" SOURCES "${TMCE}/src/lab_css.c")
foreach(ev lab lcancel ledgedash wavedash powershield dthrowknee edgeguard fc sweetspot laserland eggs techchase slalom)
  tmce_module(${ev} "${TMCE}/MexTK/evFunction.txt" SOURCES "${TMCE}/src/${ev}.c")
endforeach()
set_source_files_properties(${TMCE_OBJECTS} PROPERTIES EXTERNAL_OBJECT TRUE GENERATED TRUE)

add_library(melee_game SHARED ${GAME_SOURCES} ${LIBC_SOURCES} ${SDK_SOURCES} ${SHIM_SOURCES} $<TARGET_OBJECTS:sdk_math>
  ${TMCE_OBJECTS})
set_target_properties(melee_game PROPERTIES PREFIX "" C_STANDARD 11 C_EXTENSIONS ON EXCLUDE_FROM_ALL ON)
target_link_libraries(melee_game PRIVATE game_flags)
# Right after MEM1 (see port/app/source_host.cpp): the image's statics then have physical addresses
# GX can hold. The debug information goes to melee_game.dbg so the loaded image stays small enough
# to fit below 64 MB physical; addr2line reads the .dbg.
target_link_options(melee_game PRIVATE ${LOW_IMAGE_LINK_OPTIONS} -Wl,--image-base,0x82800000
  "-Wl,-Map,${CMAKE_CURRENT_BINARY_DIR}/melee_game.map")
add_custom_command(TARGET melee_game POST_BUILD
  COMMAND "${CMAKE_OBJCOPY}" --only-keep-debug $<TARGET_FILE:melee_game> $<TARGET_FILE_DIR:melee_game>/melee_game.dbg
  COMMAND "${CMAKE_OBJCOPY}" --strip-debug --add-gnu-debuglink=$<TARGET_FILE_DIR:melee_game>/melee_game.dbg $<TARGET_FILE:melee_game>
  # The sound driver's whole state, left out of rollback snapshots as a console savestate does.
  COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/../../tools/snapshot_audio_ranges.py"
          "${CMAKE_CURRENT_BINARY_DIR}/melee_game.map" 82800000 $<TARGET_FILE_DIR:melee_game>/melee_game.snapexcl
          $<TARGET_FILE:melee_game>
  VERBATIM)

# ---- parity library ----
# Native routines that are meant to agree bit for bit with the translated game, offered to the MSVC
# harness (port/tests/native_parity.cpp), which runs each against its twin with the same inputs.
# Pure collision routines copied out of lbcollision.c (which needs the whole game to link).
if(NOT Python3_EXECUTABLE)
  find_package(Python3 COMPONENTS Interpreter REQUIRED)
endif()
set(MU_TOOLS "${CMAKE_CURRENT_SOURCE_DIR}/../../tools")
add_custom_command(OUTPUT "${CMAKE_CURRENT_BINARY_DIR}/extracted_lbcoll.c"
  COMMAND "${Python3_EXECUTABLE}" "${MU_TOOLS}/fma_exact/extract_funcs.py"
          "${D}/src/melee/lb/lbcollision.c" "${CMAKE_CURRENT_BINARY_DIR}/extracted_lbcoll.c" lbColl_80005EBC lbColl_80006094 lbColl_80006E58
  DEPENDS "${D}/src/melee/lb/lbcollision.c" "${MU_TOOLS}/fma_exact/extract_funcs.py" VERBATIM)
add_custom_command(OUTPUT "${CMAKE_CURRENT_BINARY_DIR}/extracted_mplib.c"
  COMMAND "${Python3_EXECUTABLE}" "${MU_TOOLS}/fma_exact/extract_funcs.py"
          "${D}/src/melee/mp/mplib.c" "${CMAKE_CURRENT_BINARY_DIR}/extracted_mplib.c" mpLineIntersection mpLineIntersectionH
  DEPENDS "${D}/src/melee/mp/mplib.c" "${MU_TOOLS}/fma_exact/extract_funcs.py" VERBATIM)
add_custom_command(OUTPUT "${CMAKE_CURRENT_BINARY_DIR}/extracted_spline.c"
  COMMAND "${Python3_EXECUTABLE}" "${MU_TOOLS}/fma_exact/extract_funcs.py"
          "${D}/src/sysdolphin/baselib/spline.c" "${CMAKE_CURRENT_BINARY_DIR}/extracted_spline.c" splGetHelmite
  DEPENDS "${D}/src/sysdolphin/baselib/spline.c" "${MU_TOOLS}/fma_exact/extract_funcs.py" VERBATIM)
add_library(mu_parity SHARED parity/parity_table.c parity/parity_stubs.c shim/mu_math.c $<TARGET_OBJECTS:sdk_math>
  "${CMAKE_CURRENT_BINARY_DIR}/extracted_lbcoll.c" "${CMAKE_CURRENT_BINARY_DIR}/extracted_mplib.c"
  "${CMAKE_CURRENT_BINARY_DIR}/extracted_spline.c"
  "${D}/src/MSL/trigf.c" "${D}/src/MSL/math_data.c" "${D}/src/MSL/math_1.c" "${D}/src/MSL/math.c" "${D}/src/MSL/float.c"
  "${D}/src/melee/lb/lbvector.c" "${D}/src/melee/lb/lbtrigf.c" "${D}/src/melee/lb/lb_00CE.c"
  "${D}/src/sysdolphin/baselib/mtx.c" "${D}/src/sysdolphin/baselib/quatlib.c")
set_target_properties(mu_parity PROPERTIES PREFIX "" C_STANDARD 11 C_EXTENSIONS ON)
target_link_libraries(mu_parity PRIVATE game_flags)
target_link_options(mu_parity PRIVATE ${LOW_IMAGE_LINK_OPTIONS} -Wl,--image-base,0x58000000)

# Record the actual configured game inputs for the source-only package audit.
file(GENERATE OUTPUT "${CMAKE_CURRENT_BINARY_DIR}/native-game-sources.txt"
  CONTENT "$<JOIN:$<TARGET_PROPERTY:melee_game,SOURCES>,\n>\n$<JOIN:$<TARGET_PROPERTY:sdk_math,SOURCES>,\n>\n")
