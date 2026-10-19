#!/bin/sh
# Isolated host-only build for debugging the real native game ABI.
set -eu
output=build-linux/host-check
mkdir -p "$output"
version=$(head -n 1 VERSION | tr -d '\r')
g++-14 -std=c++17 -Wall -Wextra "-DMELEE_PORT_VERSION=\"$version\"" \
  -Iport/linux -Iport/runtime/abi -Iport/runtime/hle -Iport/runtime/host \
  port/linux/main.cpp port/linux/host_services.cpp port/linux/input_script.cpp \
  port/runtime/hle/ax_ucode.cpp -ldl -pthread -o "$output/melee_linux_boot"
