#!/bin/sh
# Fails when a built Windows, Linux or Mac host carries the staging-only agent-test hook.
# Windows: pass either exe to check both siblings. Linux: pass melee_source and/or melee_linux
# explicitly (the full and minimal hosts live in different build directories).
set -eu
: "${1:?usage: agent_test_hook_absent.sh <melee_source.exe|melee_port.exe|melee_source|melee_linux|MeleeUnlocked> [other artifacts...]}"
case "$1" in
  */melee_source.exe|melee_source.exe) set -- "$@" "$(dirname "$1")/melee_port.exe" ;;
  */melee_port.exe|melee_port.exe) set -- "$@" "$(dirname "$1")/melee_source.exe" ;;
esac
for exe do
[ -s "$exe" ] || { echo "agent_test_hook_absent: $exe is missing or empty" >&2; exit 2; }
if LC_ALL=C grep -a -q 'agent-test\.json' "$exe"; then
  echo "agent_test_hook_absent: $exe contains the agent-test hook; rebuild without -DYOUGAME_AGENT_TEST_HOOK=ON" >&2
  exit 1
fi
if LC_ALL=C grep -a -E -q 'netDelayMs|netJitterMs|AGENT TEST net delay|AGENT TEST: net delay' "$exe"; then
  echo "agent_test_hook_absent: $exe contains network test hooks" >&2
  exit 1
fi
# The hook's "padBack" key and log line, but not ImGui's "GamepadBack" key name, which every full host carries:
# each run of letters ending in padBack must be exactly GamepadBack (a "_" ends the run, so ImGuiKey_GamepadBack too).
if LC_ALL=C grep -a -o -E '[A-Za-z]*padBack' "$exe" | LC_ALL=C grep -q -v -x 'GamepadBack'; then
  echo "agent_test_hook_absent: $exe contains padBack; rebuild without -DYOUGAME_AGENT_TEST_HOOK=ON" >&2
  exit 1
fi
if LC_ALL=C grep -a -E -q 'agent[ -]test: (script|captureEvery|frames|rng_seed|time_base|frame_times|sim_times|replay_dir|record_native|slippi_menus|no_gc_adapter|early_rng_seed)|script (invalid name|missing data directory|missing or not a regular file|cannot read size|exceeds 64 KiB|not supported on the Source Port)|(rng_seed|time_base) invalid value|captureEvery|agent-captures|frames (invalid value|not supported on the Source Port)' "$exe"; then
  echo "agent_test_hook_absent: $exe contains agent script/capture hooks; rebuild without -DYOUGAME_AGENT_TEST_HOOK=ON" >&2
  exit 1
fi
if LC_ALL=C grep -a -E -q 'agent-test: (keys ignored:|key )' "$exe"; then
  echo "agent_test_hook_absent: $exe contains agent key hooks; rebuild without -DYOUGAME_AGENT_TEST_HOOK=ON" >&2
  exit 1
fi
if LC_ALL=C grep -a -E -q 'present_log|present_frames|present-log v1|agent-test: present output' "$exe"; then
  echo "agent_test_hook_absent: $exe contains presentation test hooks" >&2
  exit 1
fi
echo "agent_test_hook_absent: ok ($exe)"
done
