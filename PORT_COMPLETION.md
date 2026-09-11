# Port completion tracker

Working branch: `codex/port-completion`. Fable baseline: `f2840f6`, preserved on
`codex/fable-review` and `archive/fable-third-handoff`. The original third handoff
is retained in `HANDOFF_FABLE_3.md`; its proposed fixes are not verified results.

## Priority and acceptance

1. Stabilize native rendering without DLSS. Diagnose attack/effect stalls using
   reproducible matches, cold/warm shader caches, CPU/GPU timings and presentation
   intervals. Preserve geometry during shader compilation.
2. Deliver distinct high-refresh geometry at 120/144/165/200/240 FPS and unlocked;
   report sustainable targets rather than peak presentation counts. Keep guest
   simulation at its original rate. Use latest-state authored sampling, holding
   unsupported/discontinuous draws instead of adding a buffered simulation frame.
3. Add a persistent, controller/keyboard accessible PC settings overlay: monitor,
   fullscreen/windowed, resolution, cap, presentation, graphics, AA/upscaling,
   sharpness, volume, input and performance display. Implement DLSS SR/DLAA with
   correct depth, motion vectors, jitter, exposure, resets and output-resolution
   HUD. Native rendering remains available; initial DLSS excludes frame generation.
4. Finish reference gameplay validation, interpreter coverage, rollback and stock
   Slippi compatibility, supported online modes, cards, audio fidelity, controller
   reconnects and packaging. Resolve the differing replay byte before claiming
   deterministic online compatibility.
5. Compare latency with stock Slippi on matched hardware/settings at the same
   input delay. Software timing is not physical button-to-photon measurement.

## Current implementation work

- Forward authored sampling and safe current-pose fallback; full skinning,
  camera and effect coverage remain outstanding.
- Race-free authored counters; detached guest-state watchdog removed in favor
  of existing simulation-thread diagnostics.
- Monitor rational refresh cap, borderless fullscreen, missed-deadline pacing,
  optional buffered per-presentation CPU timing CSV.
- Cache recipes prewarm known pipelines before guest startup; cache namespaces
  follow shader/layout/backend source hashes, with atomic writes and size limits.
- Constants now build in CPU memory before contiguous GPU upload, and index
  scratch capacity is reused. Cached PSOs carry backend ownership generations.
- Automated muted benchmark harness, with separate startup and match summaries.

Initial build and all 16 tests passed; 2400 CPU/RAM/ARAM checkpoints matched in
headless, synchronous, threaded and authored modes. Cache prewarming is undergoing
additional verification. These changes do not yet
establish the first milestone. CPU submission intervals are not GPU completion or
physical display intervals. Authored draw counts do not establish pixel uniqueness.

## Fighter sub-frame animation (2026-09-11, Fable)

Skinned (envelope) draws are now authored-sampled: `SetupEnvelopeModelMtx` is observed with
its PObj and view matrix, the capture mirrors the game's slot construction (weighted bones,
inverse-bind matrices, the skeleton-root "right" transform from `_HSD_mkEnvelopeModelNodeMtx`,
and the bare single-bone case), and the render thread rebuilds every matrix slot at the
fractional frame after proving the reconstruction reproduces the matrices the game loaded.
Joint chains are captured once per joint per frame and shared. Motion that has no authored
track (fighter positions, knockback, items) advances by the last simulated per-frame delta,
bounded to 30 units so respawns and teleports hold. The camera advances by screw
extrapolation of its last change, applied to rigid and skinned draws alike (static stage
geometry therefore moves with the camera between ticks). Evidence: `reports/native-validation/
envb_sheet.png`, six consecutive presentations at 240 fps spanning two ticks; presentations
within one tick differ by 33k to 51k pixels, before this change by fewer than 10.
Remaining declines: looping/paused animations at wrap (s3), animated scale (s9), joints with
RObj constraints or quaternion flags (c4), other track channels (c8).

## DLSS (2026-09-11, Fable)

Streamline 2.10.3 is integrated (`port/runtime/gx/gx_streamline.*`): signed interposer
loaded from the executable folder, DXGI/D3D12 creation proxied, per-frame jitter,
constants, tagged colour/depth/motion vectors and `slEvaluateFeature` before the
present blit. Motion vectors are rendered into a second target from each draw's
previous presented pose (kept per draw identity). Modes: DLAA, Quality, Balanced,
Performance, Ultra Performance (`--dlss`, settings overlay, `dlss=` in the ini). The
EFB integer scale is derived from the mode's optimal render size, so on a 1280x960
window Quality renders at EFB x2 (1280x960); on a 1440p fullscreen output it renders
1280x960 into 1920x1440. Verified: a windowed DLSS Quality match rendered cleanly
with the HUD crisp; a 3-frame burst during movement showed no obvious ghost trail.
Unverified: jitter sign against NVIDIA's convention (`--dlss-jitter-sign -1` flips
it if shimmer/blur appears on static edges), HUD-less input (the HUD is upscaled with
the scene), exposure (auto), Reflex (not started).

Requested next by Chandler: proper widescreen (16:9) once gameplay is smooth: enable
Slippi's "Widescreen 16:9" Gecko code at recompile time and present 16:9 instead of
the 4:3 letterbox (output size, projection and culling follow the code).

## Required validation

Run all CTests and renderer-isolation traces, then demanding matches at each cap.
Exercise attacks, shields, hit effects, particles, multiple fighters, stages,
transitions, long runs, resize, cache eviction and multiple in-flight GPU frames.
Capture consecutive images to establish distinct poses separately from FPS.
Record cold/warm results and unresolved frame-time spikes. Complete remaining
features and clean-package checks before claiming project completion.

All automated game instances use `--volume 0`. Preserve the original ISO, Fable
checkout/branch, real Slippi installation, unrelated processes and Chrome audio.

## Initial measured evidence (2026-09-11)

2400-frame scripted matches on this PC, hidden D3D12 window, scale 2, authored
mode, unlocked, muted. Match summaries start at source frame 1500. These trials
precede pipeline prewarming and are not proof of full fighter subframe coverage.

| Warm cache measurement | Before upload changes | After upload changes |
|---|---:|---:|
| Median CPU submission interval | 3.963 ms | 2.892 ms |
| p99 CPU submission interval | 7.603 ms | 6.176 ms |
| Observed maximum | 20.182 ms | 7.681 ms |
| Match presentations | 3409 | 4587 |

Evidence: `reports/high-refresh-first/summary.json`,
`reports/high-refresh-upload/summary.json`, and
`reports/completion-isolation/render-state-comparison.json` (local artifacts).
The full handoff is preserved byte-for-byte in `reports/handoffs/fablework3.txt`.

## Distribution (2026-09-11, Fable)

Decision: GitHub Releases for downloads (unlimited bandwidth, no site to host), GitHub Issues for bug
reports (template in `.github/ISSUE_TEMPLATE/bug_report.yml`). A launcher with an updater can come
later; a zip is enough for the first Reddit post.

- `python tools/package_release.py --version X.Y.Z` writes `release/MeleePort-X.Y.Z-win64.zip`
  (about 38 MB): `melee_port.exe`, Streamline/DLSS DLLs, `Sys/` (GameSettings ini, codehandler,
  bootloader, GameFiles diffs), `MeleePort.bat`, README, licenses. No ISO, no DOL, no generated
  code. The user drops `melee.iso` next to the batch file.
- Verified: the packaged exe boots from its own folder, serves game files from `Sys/`, and logs in
  with the Slippi Launcher's `user.json` (fallback added in `slippi_online.cpp` `init()`).
- Repo hygiene stays: the public repo carries the recompiler and runtime, never the ISO, DOL or
  `port/generated/`. A clone rebuilds `port/generated/` from the user's own ISO.
- Online status for the release notes: wire-compatible with Slippi 3.6.4 (matchmaking ticket
  accepted by mm.slippi.gg, port-vs-port matches complete with identical replays). Port-vs-Dolphin
  determinism is unproven: label online play "experimental, direct codes with a partner first".
