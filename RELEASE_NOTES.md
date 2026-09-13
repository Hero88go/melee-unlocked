# v0.1.6-codex-preview — Codex graphics correctness update

This **Codex update** builds on Fable's implementation. It fixes demonstrated
rendering and lifecycle defects and adds measurements for the remaining work.
It is a development preview: graphics coverage, smooth high-refresh gameplay,
DLSS/DLAA quality, and responsiveness parity with Slippi are **not yet complete**.
Existing Fable releases are preserved.

## Graphics and animation improvements

- **Complete skinned poses:** a failed later bone/slot no longer leaves earlier
  slots partially advanced. Valid skinning matrices publish together.
- **Correct eighth texture selector:** texture generator 7 receives its own
  per-vertex matrix index instead of accidentally reusing generator 6's byte.
- **Coherent subframe texture motion:** all eight per-vertex selectors determine
  which matrices advance. Changed matrix bindings and texture-generation controls
  invalidate history. Discontinuous UV transforms hold together; UV sampling cannot
  overwrite overlapping position rows or be erased by a skinned-pose publication.
- **Coherent held geometry:** rejected rewritten vertex streams retain current
  vertices and matrices together. Changed bindings reject blending. Leaving
  authored mode clears the vertex override. Unsupported local joints share the
  correct interpolation timeline.
- **Backlog presentation repair:** sustained draining cannot indefinitely freeze
  the displayed image. At most two drained frames precede a forced presentation.
  Drained commands still execute in order, preserving EFB dependencies and frame
  recycling; expensive subframe pairing is skipped until a presentation needs it.
- **History reset repair:** draining and nonconsecutive source frames invalidate
  pose/motion history and request a DLSS reset.
- **Pipeline cache repair:** native and motion-vector pipeline variants have
  distinct cached ownership, preventing reuse with incompatible render targets.
- **Correct shader behavior:** approximate pipeline substitution was removed
  because it can display incorrect materials. Background compilation and prewarming
  remain. Unseen shaders can still cause a cold-cache stall. This substitution was
  inactive in the reported warm-cache flicker session, so removing it is not claimed
  as the diagnosis of that report.
- **Texture retention:** stable source reuse refreshes the shared content-cache
  lifetime. Draw pairing treats storage addresses consistently across all eight
  texture units while still checking captured texture content and object identity.
- **Fractional downsampling:** sampling uses the actual output-pixel footprint,
  with independent horizontal and vertical scaling.
- **Output sharpening:** sharpening runs after scaling/reconstruction in a separate
  output-resolution pass. Zero disables it; the slider changes captured pixels.
- **Clearer settings:** actual input/output dimensions are displayed. Unverified
  DLSS ratios, image-quality promises and the claim about matching Rivals' physics
  have been removed. The spatial AA path is labeled SSAA, not SMAA.

## Performance and diagnostics

- Detailed per-draw timers are now opt-in (`--profile-draws`). Two paired combat
  trials measured reductions of **0.230 and 0.235 ms** in median CPU submission
  cost, roughly **9% of that cost**. This is not a 9% overall FPS guarantee.
- `--frame-times` includes GPU command-list duration with the completed
  submission/source identifiers. Readback uses the existing frame-slot fence and
  adds no new GPU wait. These values exclude physical display latency.
- `--sim-times` separately records actual VI cadence, simulation work, pacing
  lateness/resynchronization and requested emulation speed. Presentation count
  alone cannot establish game speed or displayed smoothness.
- The benchmark supports native source-rate, forward authored, and delayed
  authored-interpolation comparisons, plus explicit shader-recipe prewarming.
- The shader-recipe collector supports reproducible offline manifests and
  validates/merges cache contents. The packaged cache contains **132 recipes**
  collected from the verified two-player contact/shield scenario. Coverage is not
  exhaustive, so other characters/stages/effects may still compile new shaders.
- Added a four-player Battlefield coverage fixture. Its replay records all four
  fighters and the expected contact/shield activity; ports 3/4 provide additional
  fighters rather than a complete four-player attack stress test.

## Stability, launcher, audio and updater

- Slippi file preloading and song decoding use owned workers that join on shutdown.
  Song decoding uses a latest-request mailbox instead of detached worker lifetimes.
- Disc-directory initialization is serialized; disc counters and published
  simulation timing are atomic. Background loading no longer races with or
  contaminates simulation-thread cost accounting.
- Invalid remembered ISO paths no longer prevent local ISO discovery. Launcher
  build workers receive a copied ISO path and leave UI-owned preferences alone.
- Explicit test user directories no longer silently discover a real Slippi login.
  Normal launchers opt into login discovery. Automated runs preserve shared ISO
  preferences and use separate cards, replays and logs.
- Truncated path API results, failed HTTP reads, incomplete updater bodies, and
  asset-size mismatches are rejected. Failed installer writes/process launches
  do not close the running game. Full updater install/restart verification remains.
- WASAPI setup failures release event handles; reopened streams reset consumer
  state. Buffered-duration sampling cannot underflow. Default-device switching
  recovery remains outstanding.
- Packaging checks the executable version and recipe integrity, refuses to
  overwrite an existing package, and includes the project license and detailed
  change/validation documents.

## Validation and practical limits

- Full Release build and **14/14 CTest** passed for the graphics fixes, including
  GPU resource reuse, tiny descriptor/upload pools, resizing and sharpening.
- The skinning and eighth-texture-selector regressions fail on the defective
  implementations and pass with the fixes.
- Repeated **2,400-checkpoint** comparisons across headless, native, threaded and
  authored rendering found **zero gameplay-state mismatches** in the tested
  scenarios. Renderer isolation is not a complete Dolphin determinism oracle.
- Replay comparison now requires every reference frame and complete
  player/follower coverage, including shield state. Four regressions cover
  incomplete/divergent recordings; four timing-analysis tests cover clock traces.
- Isolated Dolphin/native comparisons use matching replay state and output
  settings. A **108-frame 1080p combat sequence** covers contact, flashes, smoke
  and camera motion; mean absolute channel differences are **0.086–0.118 on a
  0–255 scale**. Subsequent native captures remain unchanged. This limited sequence
  does not certify every menu, stage, effect or AA setting.
- Authored captures show distinct fighter geometry at multiple phases within
  one source frame. Capture readbacks affect pacing, so those captures are visual
  evidence, not FPS benchmarks.
- 120/144/165/200/240, monitor and unlocked modes have been exercised. Recurring
  frame-time tails remain. In a prewarmed 240 FPS combat trial, GPU work was
  0.432 ms median / 0.710 ms P99, but CPU presentation submission intervals reached
  6.775 ms P99. **No demanding-match refresh target is certified yet.**
- A clean extracted package completed a muted 3,000-retrace four-player trial.
  Its measured match interval averaged **56.99 VI retraces/sec**, with requested
  emulation speed fixed at 1.0 and fast mode disabled. This exposes a real
  four-player slowdown; it is not evidence of full-speed four-player acceptance.

Still open: broader moving graphics/rollback coverage, consistent four-player and
demanding-stage pacing, matched Dolphin AA/menu quality, DLSS input dimensions,
projection/depth/motion/jitter and crisp HUD composition, full updater lifecycle,
audio-device recovery, stock-client online compatibility, and controlled
Slippi responsiveness/physical latency comparisons. DLAA is a quality mode,
not a promised performance improvement. Ray tracing and frame generation remain
outside this preview.

Fable's renderer, recycling, ordered EFB processing, texture snapshots, authored
animation, PC settings, Streamline, audio, launcher, updater and online foundations
remain. A later Codex animation-cache allocation experiment was set aside because
concurrent gameplay made its timing comparison inconclusive; it is not included.

Extract the preview into a **new folder** to compare it with an older build.
You supply your own Melee NTSC 1.02 ISO. F1 (or Z + Start) opens PC settings.
See `CODEX_GRAPHICS_REVIEW.md` and `HIGH_REFRESH_DECISION.md` for measurements,
method tradeoffs and remaining acceptance gates.
