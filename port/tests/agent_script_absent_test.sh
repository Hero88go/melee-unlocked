#!/bin/sh
# Every new hook marker must be rejected independently in either named game executable.
set -eu
scanner=${1:?usage: agent_script_absent_test.sh <scanner>}
scratch=$(mktemp -d)
trap 'rm -rf "$scratch"' EXIT HUP INT TERM
for marker in 'netDelayMs' 'netJitterMs' 'AGENT TEST net delay' 'AGENT TEST: net delay' \
  'agent test: script' 'agent-test: script' 'agent-test: rng_seed' 'agent-test: time_base' \
  'rng_seed invalid value' 'time_base invalid value' 'script invalid name' 'script missing data directory' \
  'script missing or not a regular file' 'script cannot read size' 'script exceeds 64 KiB' \
  'script not supported on the Source Port' 'captureEvery' 'agent-captures' \
  'agent-test: frame_times' 'agent-test: sim_times' 'agent-test: replay_dir' 'agent-test: record_native' \
  'agent-test: slippi_menus' 'agent-test: no_gc_adapter' 'agent-test: early_rng_seed' \
  'frames invalid value' 'frames not supported on the Source Port'; do
  for dirty in melee_source.exe melee_port.exe; do
    printf 'MZ\0clean\0' > "$scratch/melee_source.exe"
    printf 'MZ\0clean\0' > "$scratch/melee_port.exe"
    sh "$scanner" "$scratch/melee_port.exe" >/dev/null
    printf 'MZ\0%s\0' "$marker" > "$scratch/$dirty"
    if sh "$scanner" "$scratch/melee_port.exe" >/dev/null 2>&1; then
      echo "missed $marker in $dirty" >&2
      exit 1
    fi
  done
done
