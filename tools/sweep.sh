#!/bin/sh
# One scripted match per entry, using --match so the menus are not driven by cursor position.
# Usage: sh tools/sweep.sh <outdir> <frames> <spec>...
OUT="$1"; FRAMES="$2"; shift 2
R=build-sourceport/port/Release
ISO="C:/Games/Smash/DOLPHIN AND SMASH GAMES/Super Smash Bros. Melee (v1.02).iso"
mkdir -p "$OUT"
failed=0
for spec in "$@"; do
  tag=$(echo "$spec" | tr ':/' '__')
  log="$OUT/$tag.log"
  MELEE_NO_GC_ADAPTER=1 timeout 400 "$R/melee_source.exe" --iso "$ISO" --hidden --volume 0 \
    --time-base 1 --fast --script port/scripts/native_vs.txt --frames "$FRAMES" \
    --match "$spec" --capture "$OUT/$tag.ppm" --capture-frame $((FRAMES-100)) \
    --log-file "$log" > /dev/null 2>&1
  rc=$?
  if grep -q 'game crash\|game stopped\|game panic' "$log" 2>/dev/null; then
    why=$(grep -m1 'game crash\|game stopped\|game panic' "$log" | cut -c1-90)
    echo "FAIL $spec  $why"
    failed=1
  elif [ $rc -ne 0 ]; then
    echo "FAIL $spec  exit $rc"
    failed=1
  elif ! grep -q "exit requested after $FRAMES retraces" "$log"; then
    echo "FAIL $spec  incomplete run"
    failed=1
  else
    echo "ok   $spec"
  fi
done
exit "$failed"
