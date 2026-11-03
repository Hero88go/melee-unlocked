#!/bin/sh
# Checking either named executable must reject a hook in the other engine too.
set -eu
scanner=${1:?usage: agent_test_static_absent_test.sh <scanner>}
scratch=$(mktemp -d)
trap 'rm -rf "$scratch"' EXIT HUP INT TERM
for entry in melee_source.exe melee_port.exe; do
  for dirty in melee_source.exe melee_port.exe; do
    for marker in 'agent-test.json' 'agent test: padBack'; do
      printf 'MZ\0clean\0' > "$scratch/melee_source.exe"
      printf 'MZ\0clean\0' > "$scratch/melee_port.exe"
      sh "$scanner" "$scratch/$entry"
      printf 'MZ\0%s\0' "$marker" > "$scratch/$dirty"
      if sh "$scanner" "$scratch/$entry"; then
        echo "missed $marker in $dirty when checking $entry" >&2
        exit 1
      fi
    done
  done
done
rm "$scratch/melee_source.exe"
if sh "$scanner" "$scratch/melee_port.exe"; then
  echo 'missing sibling was accepted' >&2
  exit 1
fi
