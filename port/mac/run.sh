#!/bin/bash
# Run from any directory. HOME applies to the child, never to the shared queue.
set -euo pipefail
root="$(cd "$(dirname "$0")/../.." && pwd)"
cd "$root"
mkdir -p build-mac/throwaway-home/{data,config,cache,state,runtime} build-mac/cards
queue=/Users/luis/Code/brawl-dolphin/notes/lead-records/yougame-v2-org/tools/heavy-slot.sh
"$queue" run --label melee-ports:m1-run -- /usr/bin/time -p /usr/bin/env \
  ROSETTA_ADVERTISE_AVX=1 \
  HOME="$root/build-mac/throwaway-home" \
  XDG_DATA_HOME="$root/build-mac/throwaway-home/data" \
  XDG_CONFIG_HOME="$root/build-mac/throwaway-home/config" \
  XDG_CACHE_HOME="$root/build-mac/throwaway-home/cache" \
  XDG_STATE_HOME="$root/build-mac/throwaway-home/state" \
  XDG_RUNTIME_DIR="$root/build-mac/throwaway-home/runtime" \
  MELEE_DSP_COEF="$root/spike/MeleeUnlocked-0.9.52/Sys/GC/dsp_coef.bin" \
  "$root/build-mac/melee_mac" \
  --iso /Volumes/YGWorktrees/build-out/test-discs/GALE01r2.iso \
  --game-library "$root/spike/melee_game.so" \
  --headless --no-gc-adapter --fast --frames 9000 --vanilla-game \
  --time-base 0 --rng-seed 0x12345678 --script spike/parity_vs.txt \
  --card-dir "$root/build-mac/cards" \
  --state-digest build-mac/mac-digest.raw.csv > build-mac/run.log 2>&1
python3 port/mac/convert_digest.py build-mac/mac-digest.raw.csv build-mac/mac-digest.csv
python3 spike/compare.py spike/win-digest.csv build-mac/mac-digest.csv > build-mac/compare.txt
cat build-mac/compare.txt
