#!/bin/bash
# Builds the native Linux game, Source Port and launcher from this tree and packages
# release/MeleeUnlocked-<version>-linux-x86_64.tar.gz. Run under Linux or WSL (Ubuntu 24.04):
#   bash tools/build_linux_release.sh
# publish_release.ps1 runs it before every publish, so each release carries the Linux package.
set -euo pipefail
cd "$(dirname "$0")/.."
python3 tools/build_linux.py
# The compatibility build for processors without AVX2, which the launcher starts on them.
python3 tools/build_linux.py --cpu SSE2 --build-dir build-linux-compat --game-build-dir build-linux-game-compat
# Every function a Gecko code may make "return at once" must still be one GCC here never copied into a
# caller (sourceport/game/shim/mu_gecko_functions_linux.inc). If this fails, regenerate it:
#   python3 tools/gecko_functions.py --linux --objdump objdump build-linux-game/melee_game.dbg build-linux-game-compat/melee_game.dbg
python3 tools/gecko_functions.py --check --linux --objdump objdump build-linux-game/melee_game.dbg build-linux-game-compat/melee_game.dbg
python3 tools/package_linux_release.py
