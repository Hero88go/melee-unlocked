#!/bin/sh
# Scanner regression: each hook marker independently rejects an artifact.
set -eu
scanner=${1:?usage: agent_test_hook_absent_test.sh <scanner>}
scratch=$(mktemp -d)
trap 'rm -rf "$scratch"' EXIT HUP INT TERM
printf 'MZ\0no hook here\0' > "$scratch/clean.exe"
printf 'MZ\0agent-test.json\0' > "$scratch/file.exe"
printf 'MZ\0padBack\0' > "$scratch/pad.exe"
# ImGui's gamepad key name is in every full host and is not the hook (10-26: every full Linux build failed on it).
printf 'MZ\0GamepadBack\0ImGuiKey_GamepadBack\0' > "$scratch/gamepad.exe"
sh "$scanner" "$scratch/gamepad.exe"
printf 'MZ\0GamepadBack\0padBack\0' > "$scratch/padgamepad.exe"
printf 'MZ\0[yougame] agent test: padBack in %%d s\0' > "$scratch/padlog.exe"
printf 'MZ\0agent-test: keys ignored: \0' > "$scratch/keys.exe"
printf 'MZ\0agent-test: key \0' > "$scratch/key.exe"
sh "$scanner" "$scratch/clean.exe"
printf 'MZ\0netDelayMs\0' > "$scratch/delay.exe"
printf 'MZ\0netJitterMs\0' > "$scratch/jitter.exe"
printf 'MZ\0AGENT TEST net delay\0' > "$scratch/net.exe"
printf 'MZ\0AGENT TEST: net delay\0' > "$scratch/refusal.exe"
for marker in file pad padgamepad padlog keys key delay jitter net refusal; do
  if sh "$scanner" "$scratch/$marker.exe"; then
    echo "agent_test_hook_absent_test: missed $marker marker" >&2
    exit 1
  fi
done
# Linux ELF names use the same scan, including a dirty second artifact. No PE header required.
for dirty in melee_source melee_linux; do
  for marker in 'agent-test.json' 'padBack' 'netDelayMs' 'netJitterMs' \
    'agent-test: key escape' 'agent-test: script' 'agent-test: captureEvery not supported on Linux; ignored' \
    'present_log' 'present_frames' 'present-log v1' 'agent-test: present output'; do
    printf '\177ELF\0clean\0' > "$scratch/melee_source"
    printf '\177ELF\0clean\0' > "$scratch/melee_linux"
    sh "$scanner" "$scratch/melee_source" "$scratch/melee_linux"
    printf '\177ELF\0%s\0' "$marker" > "$scratch/$dirty"
    if sh "$scanner" "$scratch/melee_source" "$scratch/melee_linux"; then
      echo "agent_test_hook_absent_test: missed $marker in $dirty" >&2
      exit 1
    fi
  done
done
