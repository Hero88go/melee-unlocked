# The game, built from source as a library the host runtime loads.
#
# A separate project because it needs a different compiler from everything else in this repository
# (see ../cmake/mingw-w64-x86_64.cmake). The host talks to it through one exported entry point and
# two tables of function pointers, so nothing here links against the host and the host links against
# nothing here.
cmake_minimum_required(VERSION 3.25)
project(melee_game C CXX)

if(NOT CMAKE_C_COMPILER_ID STREQUAL "GNU" OR CMAKE_C_COMPILER_VERSION VERSION_LESS 14)
  message(FATAL_ERROR "melee_game needs GCC 14 or newer: scalar_storage_order, and -ffp-contract=on meaning what the standard says")
endif()

get_filename_component(REPO "${CMAKE_CURRENT_SOURCE_DIR}/../.." ABSOLUTE)
set(MELEE_DECOMP_DIR "${CMAKE_CURRENT_SOURCE_DIR}/../extern/melee" CACHE PATH "checkout of the game sources")
set(MELEE_DOL "${REPO}/melee/orig/GALE01/sys/main.dol" CACHE FILEPATH "the player's own main.dol (NTSC 1.02)")
set(MELEE_TRANSLATION "${REPO}/port/generated_vanilla" CACHE PATH "the recompiler's translation of the game without code tables (recomp.py --no-slippi)")
set(MELEE_PYTHON "python" CACHE FILEPATH "Python 3 interpreter for the generators")
set(D "${MELEE_DECOMP_DIR}")
set(SDK "${D}/extern/dolphin/src/dolphin")
if(NOT EXISTS "${D}/src/melee/gm/gmmain.c")
  message(FATAL_ERROR "melee_game: no game sources at ${D}")
endif()

# ---- how game code is compiled: one definition, shared by the game and by the parity library ----
add_library(game_flags INTERFACE)
target_include_directories(game_flags INTERFACE
  "${CMAKE_CURRENT_SOURCE_DIR}/include" "${D}/src" "${D}/src/MSL" "${D}/extern/dolphin/include" "${SDK}"
  "${CMAKE_CURRENT_SOURCE_DIR}/include_stub" "${REPO}/port/runtime/abi")
target_compile_definitions(game_flags INTERFACE MU_NATIVE VERSION_GALE01 BUILD_VERSION=0)
target_compile_options(game_flags INTERFACE
  -O2 -g
  -include "${CMAKE_CURRENT_SOURCE_DIR}/include/mu_native.h"   # only C targets link game_flags
  -mno-ms-bitfields          # the console compiler packs a u8 and a u16 bitfield into one run; the MS layout does not
  -fno-strict-aliasing       # the sources read floats through integer pointers throughout
  -fwrapv
  -fno-builtin               # no folding sin() at compile time or sqrt() into an instruction: the game's own maths decides
  -ffp-contract=on           # fuse a*b+c only inside one expression, as the console compiler did
  -fexec-charset=CP932       # the game's string literals are Shift-JIS at run time
  -w)
set(MELEE_CPU_BASELINE "AVX2" CACHE STRING "Instruction set baseline, matching the host: AVX2, AVX or SSE2")
if(MELEE_CPU_BASELINE STREQUAL "AVX2")
  target_compile_options(game_flags INTERFACE -mavx2 -mfma)
elseif(MELEE_CPU_BASELINE STREQUAL "AVX")
  target_compile_options(game_flags INTERFACE -mavx)
endif()
# Disc pointers are 32-bit slots holding real addresses, so everything the game can point at has to
# sit below 4 GB: the image at a fixed base with relocation off, MEM1 placed by the host.
set(LOW_IMAGE_LINK_OPTIONS -static-libgcc -static-libstdc++ -Wl,--disable-dynamicbase -Wl,--disable-high-entropy-va)

# ---- routines that exist only as paired-single assembly ----
# Compiled from the recompiler's own translation of them, so they match the console by construction
# (see tools/leaf_native.py and ppcleaf/ppc.h). Generated at build time from the player's DOL.
set(LEAF_CPP "${CMAKE_CURRENT_BINARY_DIR}/generated/ppcleaf_generated.cpp")
add_custom_command(OUTPUT "${LEAF_CPP}"
  COMMAND "${MELEE_PYTHON}" "${REPO}/tools/leaf_native.py" --generated "${MELEE_TRANSLATION}" --dol "${MELEE_DOL}" --out "${LEAF_CPP}"
  DEPENDS "${REPO}/tools/leaf_native.py" "${MELEE_TRANSLATION}/function_names.cpp" "${MELEE_DOL}"
  COMMENT "Translating the paired-single maths routines" VERBATIM)
add_library(ppcleaf OBJECT "${LEAF_CPP}")
target_include_directories(ppcleaf PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/ppcleaf" "${REPO}/port/runtime/ppc")
# Contraction off: the translation already spells out every fused operation the console performed,
# and the compiler must not invent another.
target_compile_options(ppcleaf PRIVATE -O2 -g -std=gnu++17 -mavx2 -mfma -ffp-contract=off -fno-exceptions -fno-rtti -fno-strict-aliasing)

# ---- what is compiled ----
file(GLOB_RECURSE GAME_SOURCES CONFIGURE_DEPENDS "${D}/src/melee/*.c" "${D}/src/sysdolphin/*.c")
# The console's C library is kept only where its results reach the simulation: the maths, and rand.
set(LIBC_SOURCES "${D}/src/MSL/math.c" "${D}/src/MSL/math_1.c" "${D}/src/MSL/math_data.c" "${D}/src/MSL/trigf.c" "${D}/src/MSL/rand.c")
# The halves of the SDK that sit above the hardware. What sat below it is the host's job.
file(GLOB SDK_SOURCES CONFIGURE_DEPENDS "${SDK}/gx/*.c" "${SDK}/ax/*.c" "${SDK}/axfx/*.c" "${SDK}/mtx/*.c")
list(APPEND SDK_SOURCES "${SDK}/os/OSAlloc.c" "${SDK}/os/OSArena.c")
# Written for the hardware itself, or in its assembly; the native build has its own versions of these.
list(REMOVE_ITEM SDK_SOURCES "${SDK}/gx/GXFifo.c" "${SDK}/mtx/psmtx.c")
file(GLOB_RECURSE SHIM_SOURCES CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/shim/*.c")

add_library(melee_game SHARED ${GAME_SOURCES} ${LIBC_SOURCES} ${SDK_SOURCES} ${SHIM_SOURCES} $<TARGET_OBJECTS:ppcleaf>)
set_target_properties(melee_game PROPERTIES PREFIX "" C_STANDARD 11 C_EXTENSIONS ON EXCLUDE_FROM_ALL ON)
target_link_libraries(melee_game PRIVATE game_flags)
target_link_options(melee_game PRIVATE ${LOW_IMAGE_LINK_OPTIONS} -Wl,--image-base,0x50000000)

# ---- parity library ----
# Native routines that are meant to agree bit for bit with the translated game, offered to the MSVC
# harness (port/tests/native_parity.cpp), which runs each against its twin with the same inputs.
add_library(mu_parity SHARED parity/parity_table.c $<TARGET_OBJECTS:ppcleaf>
  "${D}/src/MSL/trigf.c" "${D}/src/MSL/math_data.c" "${D}/src/MSL/math_1.c")
set_target_properties(mu_parity PROPERTIES PREFIX "" C_STANDARD 11 C_EXTENSIONS ON)
target_link_libraries(mu_parity PRIVATE game_flags)
target_link_options(mu_parity PRIVATE ${LOW_IMAGE_LINK_OPTIONS} -Wl,--image-base,0x58000000)
