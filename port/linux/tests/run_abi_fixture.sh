#!/bin/sh
# Run from the repository root; supply an uncompressed retail 1.02 ISO path.
set -eu
output=build-linux/host-check
mkdir -p "$output"
gcc-14 -std=c11 -Wall -Wextra -Werror -fPIC -shared -fvisibility=hidden \
  -Iport/runtime/abi port/linux/tests/abi_fixture.c \
  -Wl,-Bsymbolic,-z,defs,-Ttext-segment=0x82800000 -o "$output/abi_fixture.so"
g++-14 -std=c++17 -Wall -Wextra -Werror -Iport/linux -Iport/runtime/abi \
  -Iport/runtime/hle -Iport/runtime/host port/linux/tests/abi_loader_test.cpp \
  port/linux/host_services.cpp port/linux/input_script.cpp port/runtime/hle/ax_ucode.cpp -ldl -pthread \
  -o "$output/linux_abi_loader_test"
"$output/linux_abi_loader_test" "$1" "./$output/abi_fixture.so"
