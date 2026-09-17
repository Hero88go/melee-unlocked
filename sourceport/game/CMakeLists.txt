# The game, built from source as a library the host runtime loads.
#
# A separate project because it needs a different compiler from everything else in this repository
# (see ../cmake/mingw-w64-x86_64.cmake). The host talks to it through one exported entry point and
# two tables of function pointers, so nothing here links against the host and the host links against
# nothing here.
cmake_minimum_required(VERSION 3.25)
project(melee_game C)

if(NOT CMAKE_C_COMPILER_ID STREQUAL "GNU" OR CMAKE_C_COMPILER_VERSION VERSION_LESS 14)
  message(FATAL_ERROR "melee_game needs GCC 14 or newer: scalar_storage_order, and -ffp-contract=on meaning what the standard says")
endif()

set(MELEE_DECOMP_DIR "${CMAKE_CURRENT_SOURCE_DIR}/../extern/melee" CACHE PATH "checkout of the game sources")
set(D "${MELEE_DECOMP_DIR}")
set(SDK "${D}/extern/dolphin/src/dolphin")
if(NOT EXISTS "${D}/src/melee/gm/gmmain.c")
  message(FATAL_ERROR "melee_game: no game sources at ${D}")
endif()

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

add_library(melee_game SHARED ${GAME_SOURCES} ${LIBC_SOURCES} ${SDK_SOURCES} ${SHIM_SOURCES})
set_target_properties(melee_game PROPERTIES PREFIX "" C_STANDARD 11 C_EXTENSIONS ON)
target_include_directories(melee_game PRIVATE
  "${CMAKE_CURRENT_SOURCE_DIR}/include" "${D}/src" "${D}/src/MSL" "${D}/extern/dolphin/include" "${SDK}"
  "${CMAKE_CURRENT_SOURCE_DIR}/include_stub")
target_compile_definitions(melee_game PRIVATE MU_NATIVE VERSION_GALE01 BUILD_VERSION=0)
target_compile_options(melee_game PRIVATE
  -O2 -g
  -include "${CMAKE_CURRENT_SOURCE_DIR}/include/mu_native.h"
  -mno-ms-bitfields          # the console compiler packs a u8 and a u16 bitfield into one run; the MS layout does not
  -fno-strict-aliasing       # the sources read floats through integer pointers throughout
  -fwrapv
  -fno-builtin               # no folding sin() at compile time or sqrt() into an instruction: the game's own maths decides
  -ffp-contract=on           # fuse a*b+c only inside one expression, as the console compiler did
  -fexec-charset=CP932       # the game's string literals are Shift-JIS at run time
  -w)
set(MELEE_CPU_BASELINE "AVX2" CACHE STRING "Instruction set baseline, matching the host: AVX2, AVX or SSE2")
if(MELEE_CPU_BASELINE STREQUAL "AVX2")
  target_compile_options(melee_game PRIVATE -mavx2 -mfma)
elseif(MELEE_CPU_BASELINE STREQUAL "AVX")
  target_compile_options(melee_game PRIVATE -mavx)
endif()
# Disc pointers are 32-bit slots holding real addresses, so everything the game can point at has to
# sit below 4 GB: the image at a fixed base with relocation off, MEM1 placed by the host.
target_link_options(melee_game PRIVATE -static-libgcc
  -Wl,--image-base,0x50000000 -Wl,--disable-dynamicbase -Wl,--disable-high-entropy-va)
