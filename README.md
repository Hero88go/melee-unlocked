# Melee unlocked rendering development

**Active direction: actual native geometry rendering. Image interpolation and generated
frames are rejected; the old prototype launcher is disabled.**

The native port now boots and renders gameplay with immutable texture snapshots.
Run `run-native.bat` for the local development build, or add `--threaded-renderer`
to test the separate render thread. See [NATIVE_DEVELOPMENT.md](NATIVE_DEVELOPMENT.md).
Eleven tests pass, and 2,400 state checkpoints match with headless, synchronous and
threaded rendering. Independent high-FPS poses, audio and online play remain unfinished.

Fable's native recompilation/D3D12 prototype has been selectively incorporated behind
`MELEE_BUILD_EXPERIMENTAL_PORT=ON`. See [FABLE_REVIEW.md](FABLE_REVIEW.md) for fixes,
known defects, build instructions and validation. It is not yet an unlocked-FPS or
Slippi-compatible client; `--fast` accelerates simulation.

`native/AnimationTrack.h` evaluates authored animation curves at fractional times in
host-owned data. Synthetic tests pass; real packed-track decoding and model drawing
are not implemented. The asset inspector reads Falco's model and 222 animation archives
from the clean ISO. See `PROGRESS.md`. The remainder describes the retired experiment.

**Status: experimental, incomplete. No validated unlocked-FPS competitive client yet.**
`runtime/slippi` is the user's stock client copy, retained as a baseline.
`runtime/prototype` is a locally built Slippi fork with an opt-in D3D11 image-interpolation
presenter. It estimates image motion, not actual intermediate character poses, and adds
visual delay. Passing its tests does not establish Melee determinism, multiplayer
compatibility, visual fidelity, or input latency. See `ROADMAP.md` for the native-app phase.

## Retired experiment (historical instructions; launch disabled)

`run-prototype.bat` requests 120 FPS. `run-prototype.bat --fps 240` requests 240;
`run-prototype.bat --fps 0` removes the software presentation cap. These are targets,
not performance guarantees. D3D11 and windowed mode are required for this prototype.
Keep the original game window focused for controller/keyboard input. The companion
window is display-only. Closing it returns you to the original 60 FPS view. Both source
and output windows currently run, so this is not the final single-window user experience.

The counter reports submitted presents and interpolation attempts, not verified distinct
frames scanned out by the monitor. Resizing the source recreates presentation resources.
The source ISO, emulation speed, input polling and network protocol are not patched.
Rollback and ordinary save-state loads invalidate host interpolation history. Scene-cut handling, complete
load/reset coverage, latency measurement and multiplayer regression remain unfinished.

Offline training has booted and restored from a project-local save state. Diagnostic GPU
readbacks contain distinct intermediate images in gameplay, but show image-warp artifacts,
including around the HUD and moving scenery. This is not a fidelity acceptance pass.
Run `python tools/compare_captures.py` after an opt-in capture to produce PNG previews
and pixel-difference evidence in `reports/frame-comparison.json`.

## User requirement

Melee NTSC-U **1.02** with stock physics, hitboxes, input sampling, rollback and multiplayer;
independent 120, 144, 165, 240 Hz or uncapped rendering on each peer. Existing installation
and original ISO must remain intact. "Uncapped" means no software presentation cap, not
infinite achievable throughput or changing the display's physical refresh rate.

## Files

- `backups/slippi-netplay`: complete copied existing installation, including User data.
- `runtime/slippi`: independent portable baseline, with memory-card paths redirected here.
- `runtime/replays`: independent replay output.
- `runtime/prototype`: locally built opt-in experimental client.
- `melee`: https://github.com/doldecomp/melee at `039c4bf4ca33338c35d21901ad19b7ede19d19ad`.
- `slippi`: https://github.com/project-slippi/Ishiiruka at `e9d048ac6f2d77f96fcd1c0b04bc1533a7ff81e1`.
- `presentation/Timeline.h`: host-only immutable snapshot history and display clock.
- `tests/timeline_test.cpp`: rollback-generation rejection, stalls, frame gaps and arbitrary cadence.
- `tools/prepare_runtime.py`: backup hashes, path isolation and vanilla DOL verification.
- `reports/`: generated local verification results. Backups/runtime contain private account data;
  keep them local and excluded from source distributions.

The original ISO is read at
`C:/Games/Smash/DOLPHIN AND SMASH GAMES/Super Smash Bros. Melee (v1.02).iso`.
The preparation tool extracts its DOL locally for decompilation builds, without modifying the ISO.
The whole-disc SHA-256 is recorded for local identity, not checked against a reference digest;
the DOL SHA-1 is checked against the decompilation project's known vanilla 1.02 hash.

## Why the renderer needs new work

Source inspected locally:

1. `melee/src/melee/gm/gm_1A45.c` calls `HSD_GObj_80390CFC` to run game procedures,
   then separately calls `HSD_StartRender`, `HSD_GObj_80390FC0`, and XFB copy.
2. `melee/src/sysdolphin/baselib/gobj.c` dispatches game and draw callbacks. Separating
   callbacks in the source is useful, but **does not prove drawing is side-effect-free**.
3. `slippi/Source/Core/VideoBackends/DX11/Render.cpp`, `Renderer::SwapImpl`, draws the
   emulated framebuffer and presents it once. Repeating Present would repeat images.
4. `slippi/Source/Core/Core/Slippi/SlippiSavestate.cpp`, `Load`, restores selected guest
   memory ranges while preserving others. A host rendering cache needs an independent
   invalidation generation, including loads and scene changes, and must reject queued stale work.
5. Slippi's EXI code adjusts emulation speed for peer time synchronization. A display clock
   must follow actual simulation timestamps without replacing that synchronization logic.

Preferred implementation: retain the stock game executable and Slippi protocol; use the
decompilation as a map to capture scene identity and transforms, and add rendering-only
interpolation in a custom emulator. A shifted decompilation DOL cannot simply replace the
stock DOL while assuming Slippi's address-based patches and savestate ranges still work.

Extra views need stable identity for fighters, joints, items, stage objects, camera and
effects; object addresses alone are insufficient because allocations can be reused.
Interpolate positions and rotations in host-owned copies, never guest hitbox or skeleton
memory. Spawn/despawn, respawn, camera cuts, state loads and rollback are discontinuities.
HUD and event-driven effects need explicit policies, not generic matrix blending.

Ordinary interpolation between two completed simulation frames requires a delayed visual
timeline (roughly one game frame for a continuously bracketed interval). Extrapolation can
avoid that delay but guesses movement before hits, direction changes and rollback corrections.
Neither should silently be described as identical latency and perfect intermediate visuals.
The current timeline supports delayed interpolation and clamps outside its bracket; it
does not guess future motion. It does not implement transforms, animations or GPU drawing.

## Remaining implementation and acceptance gates

1. Capture immutable render packets, complete with stable object generation identifiers,
   camera, joint transforms, materials and required graphics resources. Audit guest writes.
2. Replay those packets in a host rendering context at independent display deadlines;
   interpolate actual geometry and animation, with discrete-event policies and bounded queues.
3. Wire rollback/load/scene invalidation to packet generation at the producer, not merely
   at presentation. Discard stale queued GPU work and release resources safely on resize/stop.
4. Measure actual distinct images at 120/144/165/240 Hz and uncapped. A counter reporting
   240 Present calls or duplicated frames is not acceptance.
5. Run identical recorded inputs through stock and modified builds. Compare authoritative
   game-state traces on the same simulation frames, including RNG, physics and collision state.
   Replay post-frame fields alone are insufficient to prove complete state equivalence.
6. Test two clients with different render rates, then a modified client against stock,
   under forced rollback, jitter, pauses, disconnects and long sessions. Protocol compatibility
   and matched local tests do not alone prove compatibility with current matchmaking.
7. Measure input-to-photon latency and GPU/CPU contention. A slow presenter must not stall
   simulation; use a separate resource/command path, with no unbounded queue or extra input polls.

No full acceptance gate above has been passed yet. The standalone shader test verifies
a synthetic translated midpoint using WARP, not Melee visual fidelity. Do not distribute
the stock baseline or experiment as a completed unlocked-FPS client.

## Presentation foundation tests

```
cmake -S . -B build -G "Visual Studio 16 2019" -A x64
cmake --build build --config Release
ctest --test-dir build -C Release --output-on-failure
```

Upstream Slippi is GPL-2.0-or-later; new presentation code uses the same license.
No game ISO, extracted DOL, private account data or backup should be distributed with source.
