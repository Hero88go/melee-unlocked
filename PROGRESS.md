# Development checkpoint — September 10, 2026

## Audio (AX DSP high-level emulation)

- The recompiled AX library now runs for real (`__AXOutInit` is no longer stubbed):
  `DSPAddTask` fires the task's init callback synchronously, `DSPAssertTask` runs the
  resume callback (which builds the command list), and `DSPSendMailToDSP` feeds
  `port/runtime/hle/ax_ucode.cpp`, a port of Dolphin's AX HLE (ucode 0x4e8a8b21, the
  CRC the DOL's ucode image hashes to) that mixes voices from ARAM into guest RAM and
  writes the parameter blocks back. The AI DMA clock is derived from the guest
  timebase (one 640-byte frame per 5 ms), so the whole sequence is deterministic.
- Host output: WinMM 32 kHz stereo (`--volume 0-100`, default 0 keeps the session
  muted), `--audio-dump out.wav` records exactly what the DMA played;
  `tools/wav_stats.py` reports signal per second. The headless Classic run produced
  30.3 s of audio for 1,800 frames with 26 audible seconds (boot jingle, menu music,
  the match); a paced muted run dropped 27 of ~3,000 blocks.
- Two runtime fixes this uncovered: generated code now polls for host events at loop
  back-edges (`ppc::backedge`, every 1,024 iterations) so guest spin loops on flags
  set by interrupt callbacks terminate (the SFX bank sync loop hung otherwise), and
  the timebase is topped up to exactly one frame per retrace regardless of how many
  polls happened. `--hang-watch S` reports the stuck function if a retrace stalls.
- Callback registration HLE returned the old callback before storing the new one for
  AI and ARAM DMA callbacks; fixed.

## Native unlocked frame rate (sub-frame presentation)

- `--fps N|unlocked` (with `--frame-mode extrapolate|interpolate|off`) runs the
  render thread on its own timeline. Every draw captured from the GX FIFO carries a
  stable identity (display-list address, call ordinal, draw ordinal; immediate-mode
  draws use texture/size/ordinal). Between two 60 Hz simulation frames the presenter
  pairs each draw with its previous instance, takes the per-slot delta of the XF
  position matrices, decomposes it into a screw motion (rotation about an axis plus
  slide) with per-axis scale, and applies the fraction `t` of that delta on top of the
  current pose (extrapolate, zero added latency) or the previous pose (interpolate,
  one frame of latency). Normal matrices follow the rotation. Non-rigid deltas use a
  bounded linear blend; teleports and camera cuts keep the exact simulation pose.
  Nothing blends images and nothing touches guest state (`port/runtime/gx/subframe.*`).
- Evidence (`reports/native-validation/unlocked.log`, `burst_*.ppm`): a paced run
  presented 35,090 frames for 1,799 simulation frames; captured presented frames
  34952 and 34953 both belong to simulation frame 1721 (phases 0.416 and 0.987) and
  differ in 90% of pixels: the falling item, both fighters and the camera advanced.
  `port_subframe_test` covers the fractional screw math, cut detection, blend fallback
  and identity pairing. Simulation state traces with the unlocked presenter match the
  locked run (see `unlocked_trace.csv` versus `scale_s1.csv`).
- Internal resolution follows Dolphin: `--scale N|auto` rasterizes the EFB at
  640x528 x N (auto follows the window), scaled viewport/scissor/clears/EFB copies,
  half-scale copies filtered. Window size via `--window WxH`.
- Not yet done: GPU frames in flight (the presenter still waits for each frame's GPU
  work), display-rate input sampling is unchanged (the game samples at 60 Hz), and
  particles that are re-created every frame (new identities) do not extrapolate.

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
  simulation and does not produce fractional animation frames yet.
- Generator/runtime verify the stock DOL; host RAM copies validate complete ranges.
- Release build and 11 CTest checks pass. With the same script/clock, headless,
  synchronous and threaded rendering matched all 2,400 CPU/RAM/ARAM/event checkpoints.
  The worker drained all 2,399 submitted source frames on shutdown. This is local
  renderer isolation evidence, not stock-Dolphin equivalence or netplay validation.
- `run-native.bat`, `tools/validate_native.py` and `NATIVE_DEVELOPMENT.md` provide
  the native launch and repeatable validation paths. Existing Slippi is untouched.

Project remains incomplete: independent authored sub-frame rendering, audio,
memory cards, full deterministic rollback and Slippi networking are still absent.

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
