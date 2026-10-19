#!/bin/sh
# Locate the executable without changing how relative user paths resolve.
set -eu
game_dir=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
if [ "$#" -eq 0 ]; then
  if [ -f "$game_dir/melee.iso" ]; then
    exec "$game_dir/melee_linux" --iso "$game_dir/melee.iso"
  fi
  printf '%s\n' 'Supply your Melee NTSC 1.02 ISO:' './MeleeUnlocked.sh "/path/to/melee.iso"' >&2
  exit 2
fi
case "$1" in
  --*) exec "$game_dir/melee_linux" "$@" ;;
  *) iso_path=$1; shift; exec "$game_dir/melee_linux" --iso "$iso_path" "$@" ;;
esac
