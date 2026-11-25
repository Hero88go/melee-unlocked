# melee_game.so built on the Mac: a GCC cross compiler that targets Linux, since the Mac host loads the
# game as a Linux ELF (elf_loader.cpp). Included by linux-x86_64-cross.cmake and
# linux-aarch64-cross.cmake, which set MU_CROSS_ARCH. port/mac/build_deps.sh fetches messense's
# GCC 15.2 builds into ../deps/cross; MU_CROSS_ROOT points elsewhere. Used by port/mac/build_all.sh.
set(CMAKE_SYSTEM_NAME Linux)
set(CMAKE_SYSTEM_PROCESSOR ${MU_CROSS_ARCH})
if(DEFINED ENV{MU_CROSS_ROOT})
  set(MU_CROSS_ROOT "$ENV{MU_CROSS_ROOT}")
else()
  get_filename_component(MU_CROSS_ROOT "${CMAKE_CURRENT_LIST_DIR}/../../../deps/cross/${MU_CROSS_ARCH}-unknown-linux-gnu" ABSOLUTE)
endif()
set(MU_CROSS_PREFIX "${MU_CROSS_ROOT}/bin/${MU_CROSS_ARCH}-unknown-linux-gnu-")
if(NOT EXISTS "${MU_CROSS_PREFIX}gcc")
  message(FATAL_ERROR "No ${MU_CROSS_ARCH} Linux cross GCC at ${MU_CROSS_PREFIX}gcc; run port/mac/build_deps.sh")
endif()
set(CMAKE_C_COMPILER "${MU_CROSS_PREFIX}gcc")
set(CMAKE_CXX_COMPILER "${MU_CROSS_PREFIX}g++")
set(CMAKE_AR "${MU_CROSS_PREFIX}ar")
set(CMAKE_RANLIB "${MU_CROSS_PREFIX}ranlib")
set(CMAKE_STRIP "${MU_CROSS_PREFIX}strip")
set(CMAKE_OBJCOPY "${MU_CROSS_PREFIX}objcopy")
set(CMAKE_FIND_ROOT_PATH_MODE_PROGRAM NEVER)
set(CMAKE_FIND_ROOT_PATH_MODE_LIBRARY ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_INCLUDE ONLY)
