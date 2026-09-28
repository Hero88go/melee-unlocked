#!/bin/sh
# Compile one game source file the way the melee_game build does, into a scratch object, without
# touching the ninja build (several people can run this at once). Prints only the compiler's errors.
#   sourceport/tools/cc1.sh src/sysdolphin/baselib/jobj.c      (path relative to extern/melee)
ROOT="/c/Users/Chandler/NEW project/melee-sourceport"
N=/c/Users/Chandler/toolchains/winlibs-gcc-15.3/mingw64/bin/ninja.exe
rel="$1"
obj="CMakeFiles/melee_game.dir/C_/Users/Chandler/NEW_project/melee-sourceport/sourceport/extern/melee/$rel.obj"
cmd=$(cd "$ROOT" && $N -C build-sourceport-gcc -t commands "$obj" | tail -1)
[ -z "$cmd" ] && { echo "no build command for $rel"; exit 2; }
out="$(cygpath -m "${TMP:-/tmp}")/cc1_$$_$(basename "$rel").obj"
cmd=$(printf '%s' "$cmd" | tr '\' '/' | sed -E "s# -o [^ ]+# -o \"$out\"#; s# -MD -MT [^ ]+ -MF [^ ]+##")
cd "$ROOT/build-sourceport-gcc" && eval "$cmd" 2>&1 | grep -E "error|note: in expansion" | sed 's#C:/Users/Chandler/NEW project/melee-sourceport/sourceport/extern/melee/##'
rc=${PIPESTATUS:-0}
rm -f "$out"
