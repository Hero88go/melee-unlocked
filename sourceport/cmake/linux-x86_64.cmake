# Native Linux game toolchain: GCC 14+ is required for scalar_storage_order and
# the simulation's fp-contract behavior. Run inside Linux/WSL, never MinGW.
# cmake -S sourceport/game -B build-linux-game -G Ninja \
#   -DCMAKE_TOOLCHAIN_FILE=../cmake/linux-x86_64.cmake
set(CMAKE_SYSTEM_NAME Linux)
set(CMAKE_SYSTEM_PROCESSOR x86_64)
find_program(MU_LINUX_GCC NAMES gcc-15 gcc-14 gcc REQUIRED)
find_program(MU_LINUX_GXX NAMES g++-15 g++-14 g++ REQUIRED)
set(CMAKE_C_COMPILER "${MU_LINUX_GCC}")
set(CMAKE_CXX_COMPILER "${MU_LINUX_GXX}")
