#!/bin/bash
# All compilation enters the shared machine-wide queue. No network/dependencies.
set -euo pipefail
root="$(cd "$(dirname "$0")/../.." && pwd)"
cd "$root"
mkdir -p build-mac
queue=/Users/luis/Code/brawl-dolphin/notes/lead-records/yougame-v2-org/tools/heavy-slot.sh
version="$(head -n 1 VERSION | tr -d '\r')"
exec "$queue" run --label melee-ports:m1-build -- \
  /usr/bin/clang++ -arch x86_64 -mmacosx-version-min=15.0 -std=c++17 -O2 -g -Wall -Wextra \
  -fno-strict-aliasing -ffp-contract=on -DMELEE_NO_SLIPPI \
  "-DMELEE_PORT_VERSION=\"$version\"" \
  -Iport/linux -Iport/runtime/abi -Iport/runtime/hle -Iport/runtime/host \
  -Iport/runtime/ppc -Iport/runtime/gx -Inative \
  port/mac/main.cpp port/mac/elf_loader.cpp port/linux/host_services.cpp \
  port/linux/input_script.cpp port/runtime/hle/ax_ucode.cpp \
  -Wl,-pagezero_size,0x10000 -o build-mac/melee_mac
