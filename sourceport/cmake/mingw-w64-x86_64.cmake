# Toolchain for the game library. It has to be GCC: the game's disc-resident structs stay big-endian in
# memory through scalar_storage_order, which no other compiler implements.
#
#   cmake -S sourceport/game -B build-sourceport-gcc -G Ninja
#         -DCMAKE_TOOLCHAIN_FILE=sourceport/cmake/mingw-w64-x86_64.cmake -DMELEE_MINGW_ROOT=<.../mingw64>
set(CMAKE_SYSTEM_NAME Windows)
set(CMAKE_SYSTEM_PROCESSOR x86_64)
if(NOT MELEE_MINGW_ROOT)
  set(MELEE_MINGW_ROOT "$ENV{MELEE_MINGW_ROOT}")
endif()
if(NOT MELEE_MINGW_ROOT)
  message(FATAL_ERROR "set MELEE_MINGW_ROOT to a MinGW-w64 GCC 14 or newer (the folder holding bin/gcc.exe)")
endif()
list(APPEND CMAKE_TRY_COMPILE_PLATFORM_VARIABLES MELEE_MINGW_ROOT)
file(TO_CMAKE_PATH "${MELEE_MINGW_ROOT}" MELEE_MINGW_ROOT)
set(CMAKE_C_COMPILER "${MELEE_MINGW_ROOT}/bin/gcc.exe")
set(CMAKE_CXX_COMPILER "${MELEE_MINGW_ROOT}/bin/g++.exe")
set(CMAKE_AR "${MELEE_MINGW_ROOT}/bin/gcc-ar.exe")
set(CMAKE_RANLIB "${MELEE_MINGW_ROOT}/bin/gcc-ranlib.exe")
set(CMAKE_MAKE_PROGRAM "${MELEE_MINGW_ROOT}/bin/ninja.exe" CACHE FILEPATH "")
set(CMAKE_FIND_ROOT_PATH_MODE_PROGRAM NEVER)
