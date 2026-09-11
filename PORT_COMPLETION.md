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
