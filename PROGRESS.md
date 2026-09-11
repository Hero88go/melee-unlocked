# Development checkpoint, September 11, 2026

## Authored sub-frame rendering, frames in flight, Slippi re-integration

- `--frame-mode authored` re-poses captured draws from the game's own animation
  tracks (joint generations from recompiled `HSD_JObjAlloc`/`JObjRelease`, draw
  pairing through `HSD_JObjDisp`/`SetupRigidModelMtx`, AObj/FObj chains read from
  guest RAM, packed tracks re-sampled at fractional frames on the render thread).
  Draws the authored path declines fall back to the geometric estimate. A match on
  Yoshi's Story sampled about 310k authored poses per minute with phases spread
  across the display timeline; burst captures show distinct intermediate frames.
- D3D12 keeps three frames in flight (per-slot upload rings, allocator and
  descriptor recycling), caches the pipeline per draw, and persists shader blobs
  plus the pipeline library under `shadercache/`. Warm runs: menus 240 fps, match
  126-135 fps on the RTX 5070 (submit about 4.7 ms per frame, solver about 1.7 ms).
- Validation after these changes: 15/15 CTest, 2,400 checkpoints identical across
  headless, hidden and threaded rendering.
- Slippi Online runs: netplay client, matchmaking and rollback savestates are ported
  wire-compatible with Dolphin. Two port instances peered locally (`--local-peer`)
  played an unranked match with rollbacks; both replays agree on 10,212 pre/post-frame
  records over 2,483 frames (one differing state-flag byte on a single frame is under
  investigation). A direct-mode ticket was accepted by mm.slippi.gg with the Launcher
  credentials. Mixed display rates (one peer at unlocked authored FPS, one at 60 Hz)
  stayed in sync. A WUP-028 GameCube adapter is supported over WinUSB, with rumble.
- Dynamically loaded code (SlippiCSS.dat) runs through a Gekko interpreter fallback.
- The archived Slippi/Gecko work is merged back: Gecko applier and code-table
  generation in the recompiler, EXI channel 1 Slippi device (GCT serving, game
  files with VCDIFF, log, delay, online status stub, .slp recording), and the
  in-cave subroutine/constant-CTR handling in the emitter. The crash on the
  helper-table trick (`call to unmapped guest address 80668588`) is addressed by
  dispatch-table thunks for every cave instruction, hook address and hook+4, so
  computed entries into cave code resolve at run time.

## Second Fable handoff review

The resolution/window controls, filtered half-scale EFB copies, approximate AX
mixer, and opt-in transform-based sub-frame renderer are retained. The complete
handoff, including the crashing uncommitted Slippi/Gecko experiment, is preserved
on `archive/fable-second-handoff` (`c2341e994b24337e38e333ce472ce7f9961fd8c0`).
The normal build uses the regenerated stock DOL and excludes that experiment.
See `FABLE_REVIEW_2.md` for review decisions and validation.

Review fixes preserve every source frame in the render queue, remove the detached
CPU-reading watchdog, preserve filtered-copy alpha, and apply volume only to this
client's PCM samples. Sub-frame pairing rejects frame gaps, changed geometry,
projection/state changes and orthographic HUD draws. Normal transforms use inverse
transpose; unsafe transform deltas keep the current pose. `--fps` now requires an
explicit experimental interpolation or extrapolation mode.

These modes estimate matrix motion; they do not provide stable object generations
or authored fractional animation. Audio resampling and DSP command coverage remain
approximate. Guest polling advances the timebase; retrace pacing tops up only when
below its target and does not undo excess polling time. Stock timing equivalence
has not been established. `--hang-watch` now uses synchronous poll diagnostics only.

## Native fidelity and thread isolation

- Texture and palette bytes (including mip chains) are now captured into immutable
  host-owned snapshots at each draw. Duplicate contents share storage; deferred
  rendering no longer reads guest RAM or TMEM. Mip counts are bounded to 1x1.
- Native 1280x960 Classic gameplay captures show clean fighter/stage/HUD rendering
  in the tested scene (`reports/native-validation/classic_01800.png`). Other scenes
  and remaining EFB copy behavior still need validation.
- `--threaded-renderer` moves the window and D3D12 work onto their own thread.
  Keyboard state is synchronized; normal shutdown drains and joins the renderer.
  A two-frame ordered queue preserves EFB dependencies. It can still back-pressure
  simulation. Fractional matrix estimates require an explicit experimental mode.
- Generator/runtime verify the stock DOL; host RAM copies validate complete ranges.
- Release build and 11 CTest checks pass. With the same script/clock, headless,
  synchronous and threaded rendering matched all 2,400 CPU/RAM/ARAM/event checkpoints.
  The worker drained all 2,399 submitted source frames on shutdown. This is local
  renderer isolation evidence, not stock-Dolphin equivalence or netplay validation.
- `run-native.bat`, `tools/validate_native.py` and `NATIVE_DEVELOPMENT.md` provide
  the native launch and repeatable validation paths. Existing Slippi is untouched.

Project remains incomplete: authored fractional animation, faithful audio, memory
cards, full deterministic rollback and Slippi networking remain unfinished.

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
