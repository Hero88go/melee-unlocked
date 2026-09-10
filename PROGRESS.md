# Development checkpoint — September 10, 2026

## Fable review integration

The experimental native recompiler and D3D12 renderer are imported on
`codex/fable-review`; Fable's separate branch and working changes are preserved.
Signed halfword instruction loads were corrected, the racy watchdog was removed,
and unfinished texture-cache patches were excluded. See `FABLE_REVIEW.md` for the
current acceptance limits and validation. Earlier sections below describe the
pre-integration checkpoint and retired experiment.

## Active work after direction correction

The user rejected image interpolation. Both old prototype launchers are disabled;
earlier presentation-rate measurements do not meet the native FPS requirement.

`native/AnimationTrack.h` implements immutable authored constant/linear/Hermite segment
sampling at fractional times. No guest pointers, callbacks or image inputs. Tests pass
for curved motion with equal endpoints, tangent units, boundaries, and out-of-order
samples at multiple rates. These are synthetic segments; actual packed Melee tracks
are not yet decoded and no native game renderer exists.

`tools/inspect_native_assets.py` reads the ISO without modifying it and extracts Falco's
model plus 222 animation archives into `runtime/native-assets`. Archive bounds and
public symbols were parsed; hashes and inventory are in `reports/native-assets.json`.
The fighter animation roots are `figatree` symbols; follow fighter animation structures
instead of assuming generic HSD_AnimJoint layouts when implementing the decoder.

Audit: `gm_1A45.c` separates simulation and drawing, but `aobj.c` advances animation
state and callback accounting. Running guest animation routines more often would mutate
game state. Next: decode authored tracks and model/skin data into host-owned structures,
apply poses to geometry, and design the simulation bridge and root-motion policy.
Slippi integration feasibility, physics equivalence, latency and multiplayer remain open.

## Retired experiment history

This is an incomplete experimental Slippi fork, not a completed competitive client.

## Verified in this session

- Full Release x64 Slippi solution compiled successfully after adding ordinary save-state
  history invalidation. Packaged executable SHA-256:
  `9d1737f0f5f38513a041409a0b4c0d8a1dd6cbc0700456053a8c8b866db78307`.
- Both CTest tests passed: presentation timeline and synthetic GPU interpolation.
- Booted offline Training, selected Falco versus Pichu on Fourside, and saved/restored
  project-local slot 1. Restore advanced the presentation epoch from 0 to 1.
- Clean single-instance training run reported approximately 240 presents/second while
  source frames remained near 60/second after load settled. Local evidence:
  `reports/presenter-240-single-instance.log`. These are submission counters, not monitor
  scanout measurements, simulation hashes, or end-to-end latency measurements.
- Gameplay GPU readbacks contain distinct quarter and three-quarter intermediate images.
  Pixel comparisons: `reports/frame-comparison.json`; previews: `reports/frame-captures`.
  Visible scenery/HUD warping means fidelity acceptance has NOT passed.
- Uncapped mode ran, but its throughput log overlaps another project instance and is not
  accepted as an isolated benchmark. The launcher now rejects another running executable
  from this project's exact runtime path; this rejection was verified.
- All project test processes were stopped at this checkpoint. Other installations and
  unrelated Dolphin processes were excluded by executable path. The launcher forces
  project audio volume to zero. Chrome audio settings were not changed in this session.

## Repeatable offline test

Launch `run-prototype.bat --fps 240`. After boot, run
`python tools/project_windows.py --state load` to restore project slot 1.
The window helper verifies the executable path before sending any messages. It enumerates
both the automation desktop and input desktop. `--tas` opens built-in controller controls;
`--button X` taps jump. Slider controls emit the appropriate native scroll event; plain
text edits did not reliably change controller state in earlier testing.

The generated DTM experiment failed before rendering with an invalid-write error. It
is not an accepted input-replay fixture. No stock-versus-modified deterministic input
comparison or network compatibility test has been completed.

## Remaining work

The primary rendering work is still substantial: capture and interpolate actual render
geometry/animation with stable identities, isolate HUD/discrete events, handle scene cuts
and all timeline discontinuities, and measure latency and simulation-state equivalence.
Mixed-rate peers and stock-client rollback tests remain required. Image interpolation
alone has not met the user's requirement that gameplay look and behave the same.

The native decomp application remains a later phase. Its plan includes optional RTX ray
tracing, DLSS, Reflex and AA/sharpness controls; none of these integrations is implemented.
