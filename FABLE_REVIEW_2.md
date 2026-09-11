# Second Fable handoff review

Reviewed September 10, 2026 against `fablework2.txt` and the actual source.

## Preservation

Fable made commits `7ce9e4e`, `1391428`, and `7a365e5` directly on
`codex/fable-review` in this checkout. His complete working tree, including the
uncommitted Slippi/Gecko attempt, was saved before editing on
`archive/fable-second-handoff`, commit
`c2341e994b24337e38e333ce472ce7f9961fd8c0`. The source manifest is local at
`reports/fable2-review/source-before.json`. The separate original
`melee-unlocked-claude` checkout remains at `1d3b0ab` with its changes untouched.
No commits were rewritten.

## Accepted and corrected

- Retained internal-resolution auto scale, window sizing and filtered half-scale
  EFB copies. The blit shader now preserves alpha instead of forcing it opaque.
- Retained approximate AX DSP mixing, guest polling/AI clock integration and WAV
  capture. Volume now scales only this client's device PCM; WAV samples remain
  unscaled. Removed the detached watchdog that raced live guest CPU state.
- Retained matrix-motion sub-frame code as an explicit experiment. `--fps` requires
  `--frame-mode interpolate` or `--frame-mode extrapolate`; normal launch is off.
- Fixed the render thread's queue drain that discarded unrendered source frames
  and their EFB operations. Every source frame is processed before advancing.
- Tightened pairing to adjacent frames with matching geometry, BP state and
  projection; orthographic HUD draws are excluded. Fixed matrix row indexing,
  inverse-transpose normal transforms, exact endpoints and cut fallback. Rejected
  non-rigid fallback blends; cuts retain the current pose in both modes.

## Excluded

The uncommitted Slippi EXI, Gecko patching, VCDIFF and related recompiler/HLE edits
are archived, not enabled. The handoff reports repeated boot crashes at guest
address `80668588`; no functioning rollback or network session was demonstrated.
Generated code was rebuilt from the verified vanilla DOL after excluding them.

## Validation

- Release build succeeded; all 13 CTest checks passed. The final additional
  interpolation-cut regression also passed in the rebuilt sub-frame test.
- 2,400 checkpoints match exactly across headless, synchronous D3D12 and threaded
  D3D12 modes: CPU registers, RAM, ARAM and selected event counters. All runs exit
  successfully without the invalid-access diagnostics checked by the validator.
  Threaded mode processed and presented all 2,399 submitted source frames.
- An additional paced 1,800-retrace interpolation run at requested 120 FPS, auto
  scale and 960x720 matched all 1,800 headless checkpoints. It presented 3,478
  views from 1,799 sources. The gameplay capture was visually inspected.
- This is not evidence of finished high-FPS gameplay: sampled logs reported zero
  paired draws with the conservative checks. Gameplay fell to 18?22 display FPS
  during concurrent validation. Captured phases were already clamped to 1.0.
  No sustained performance or intermediate-animation claim is accepted.
- Silent headless WAV capture contains 969,440 stereo frames at 32 kHz (30.30 s),
  with signal above the tool's RMS threshold in 26 one-second bins. Device output
  drops blocks under load; audible fidelity has not been established.
- Both executable and launcher reject `--fps 120` without an experimental mode.

Local evidence is in `reports/fable2-review/`: `ctest.log`, `isolation/`,
`interpolate-comparison.json`, `interpolate.log`, `interpolate.png`, and `audio.wav`.
These artifacts include local game data and are not committed.

## Remaining project limits

Draw IDs are address/ordinal heuristics, not semantic object generations. Conservative
full-state comparisons can reject otherwise useful pairs. Authored fractional-time
animation, reliable scene/discontinuity handling and display-rate independence are
unfinished. AX uses approximate resampling and incomplete DSP command coverage;
polling/timebase changes still need stock timing validation. Trace equality proves
renderer isolation only, not stock-Dolphin equivalence or complete rollback state.
EFB fidelity, GPU cache/descriptor bounds, memory cards, rollback, Slippi networking,
latency measurement and packaging remain open. The overall project is incomplete.
