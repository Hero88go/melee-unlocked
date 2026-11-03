#!/bin/sh
# Offline P4 checks; no sockets or packages. Run from the fork root.
set -eu
build_dir=$(mktemp -d /tmp/yougame-direct-test.XXXXXX)
trap 'rm -rf "$build_dir"' EXIT HUP INT TERM
san_flags=
if [ "${SANITIZE:-0}" = 1 ]; then
  san_flags='-fsanitize=address,undefined -fno-sanitize-recover=all -fno-omit-frame-pointer'
fi
clang++ -std=c++20 -O1 -Wno-deprecated-declarations $san_flags -Iport/runtime/hle -Iport/third_party \
  port/tests/yougame_transport_test.cpp -o "$build_dir/transport"
"$build_dir/transport"
clang++ -std=c++20 -O1 -Wno-deprecated-declarations $san_flags -Iport/runtime/hle -Iport/third_party \
  port/tests/yougame_room_test.cpp -o "$build_dir/room"
"$build_dir/room"
clang -std=c11 -O1 -Wall -Wextra -Wno-unused-parameter $san_flags -Iport/third_party/enet/include \
  port/tests/yg_enet_socket_test.c port/third_party/enet/yg_enet_socket.c \
  port/third_party/enet/callbacks.c port/third_party/enet/compress.c port/third_party/enet/host.c \
  port/third_party/enet/list.c port/third_party/enet/packet.c port/third_party/enet/peer.c \
  port/third_party/enet/protocol.c -o "$build_dir/enet"
"$build_dir/enet"
clang++ -std=c++20 -O1 -Wno-deprecated-declarations $san_flags -Iport/runtime/hle -Iport/third_party \
  port/tests/yougame_ranked_test.cpp -o "$build_dir/ranked"
"$build_dir/ranked"
clang++ -std=c++20 -O1 -Wno-deprecated-declarations $san_flags -Iport/runtime/hle -Iport/third_party \
  port/tests/yougame_agent_test.cpp -o "$build_dir/agent-test"
"$build_dir/agent-test" "$build_dir"
printf 'MZ\0no hook here\0' > "$build_dir/without.exe"
printf 'MZ\0agent-test.json\0' > "$build_dir/with.exe"
port/tests/agent_test_hook_absent.sh "$build_dir/without.exe" >/dev/null
if port/tests/agent_test_hook_absent.sh "$build_dir/with.exe" 2>/dev/null; then echo "hook check missed the hook" >&2; exit 1; fi
clang -std=c11 -fsyntax-only -Iport/third_party/enet/include port/third_party/enet/yg_enet_socket.c \
  port/third_party/enet/win32.c port/third_party/enet/host.c port/third_party/enet/peer.c \
  port/third_party/enet/protocol.c port/tests/yg_enet_socket_test.c
clang++ -std=c++20 -Wno-deprecated-declarations -fsyntax-only -Iport/runtime/hle -Iport/third_party \
  port/tests/yougame_transport_test.cpp port/tests/yougame_room_test.cpp port/tests/yougame_ranked_test.cpp \
  port/tests/yougame_agent_test.cpp
if command -v x86_64-w64-mingw32-g++ >/dev/null 2>&1; then
  x86_64-w64-mingw32-g++ -std=c++20 -Wno-deprecated-declarations -Wno-cpp -fsyntax-only \
    -Iport/runtime/hle -Iport/runtime/host -Iport/runtime/ppc -Iport/third_party -Iport/third_party/enet/include \
    port/runtime/hle/yougame_link.cpp port/runtime/hle/slippi_net.cpp \
    port/tests/yougame_transport_test.cpp port/tests/yougame_room_test.cpp port/tests/yougame_ranked_test.cpp \
    port/tests/yougame_agent_test.cpp
  # main.cpp targets C++17; retain its pre-char8_t filesystem strings in this C++20 check.
  x86_64-w64-mingw32-g++ -std=c++20 -fno-char8_t -Wno-deprecated-declarations -Wno-cpp -fsyntax-only \
    -DMELEE_SOURCE_PORT -Iport/runtime/hle -Iport/runtime/host -Iport/runtime/ppc \
    -Iport/runtime/gx -Iport/runtime/abi -Iport/third_party -Iport/third_party/enet/include -Inative \
    port/app/main.cpp
  # The staging-only agent-test hook path (CMake YOUGAME_AGENT_TEST_HOOK) compiles too.
  x86_64-w64-mingw32-g++ -std=c++20 -fno-char8_t -Wno-deprecated-declarations -Wno-cpp -fsyntax-only \
    -DMELEE_SOURCE_PORT -DYOUGAME_AGENT_TEST_HOOK -Iport/runtime/hle -Iport/runtime/host -Iport/runtime/ppc \
    -Iport/runtime/gx -Iport/runtime/abi -Iport/third_party -Iport/third_party/enet/include -Inative \
    port/app/main.cpp
  x86_64-w64-mingw32-gcc -std=c11 -Wall -Wextra -Werror -Wno-unused-parameter -fsyntax-only \
    -Iport/third_party/enet/include port/third_party/enet/yg_enet_socket.c port/third_party/enet/win32.c \
    port/third_party/enet/host.c port/third_party/enet/peer.c port/third_party/enet/protocol.c \
    port/tests/yg_enet_socket_test.c
fi
