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

- `python tools/package_release.py --version X.Y.Z` writes `release/MeleeUnlocked-X.Y.Z-win64.zip` (version from `VERSION`), with `MeleeUnlocked.exe` (client)
  (about 38 MB): `melee_port.exe`, Streamline/DLSS DLLs, `Sys/` (GameSettings ini, codehandler,
  bootloader, GameFiles diffs), `MeleeUnlocked.bat`, README, licenses. No ISO, no DOL, no generated
  code. The user drops `melee.iso` next to the batch file.
- Verified: the packaged exe boots from its own folder, serves game files from `Sys/`, and logs in
  with the Slippi Launcher's `user.json` (fallback added in `slippi_online.cpp` `init()`).
- Repo hygiene stays: the public repo carries the recompiler and runtime, never the ISO, DOL or
  `port/generated/`. A clone rebuilds `port/generated/` from the user's own ISO.
- Online status for the release notes: wire-compatible with Slippi 3.6.4 (matchmaking ticket
  accepted by mm.slippi.gg, port-vs-port matches complete with identical replays). Port-vs-Dolphin
  determinism is unproven: label online play "experimental, direct codes with a partner first".

## Online verification against real Slippi (2026-09-11, Fable)

Chandler's requirement: real Slippi online, Dolphin players connect to the port with their normal
codes, nothing to change on their side.

- Real server, real opponent: one port instance queued Unranked at mm.slippi.gg and was matched
  with a Slippi Dolphin player (log `reports/native-validation/mmA/log.txt`). Ticket, match id,
  peer connection, character selections, game start, remote inputs and damage all worked. The
  opponent quit after 5 s because the scripted port player stood still (its inputs had run out
  before the match started).
- Checksum oracle: the game hands the EXI device a checksum of its finalized state every frame and
  each client sends its latest checksum to the peer. The port now compares the opponent's checksum
  with its own for the same frame and logs `DESYNC` on mismatch, `checksums agree through frame N`
  every 20 comparisons. Against a Dolphin player this is a direct bit-exactness test.
- Bot script: `port/scripts/online_bot.txt` uses the new `@match` / `@loop N` directives (input
  frames relative to online frame 1, repeating) so the port plays a whole game by itself.
- Time sync: the port's advance/skip works (measured with `tools/online_pair.py`: two local
  instances stay within a few ms of each other for a full game).
- Crash found and fixed: matches on stages with animated backgrounds died after about 30 s with a
  corrupted OS alarm queue. Root cause: MSL `__longjmp` had been translated as an ordinary
  function, so after restoring the guest registers it returned to its host caller instead of the
  `__setjmp` site (the game uses setjmp/longjmp to abort `HSD_ForeachAnim` walks in granime.c and
  hsd_3B34/3B5C). `__longjmp` is now an HLE that throws `ppc::GuestLongJmp`; the recompiler wraps
  every function that calls `__setjmp` in a retry loop with a catch that restores the registers
  (`ppc::longjmp_restore`) and re-enters at the saved return address through the entry dispatch.

## Widescreen 16:9 (2026-09-11, Fable)

Slippi's "Optional: Widescreen 16:9" code is compiled in as a run-time option (PC settings
checkbox, `--widescreen`, `widescreen 1` in port-settings.ini): the recompiler emits both variants
of its 3 patched instructions and 2 hooks behind `gecko::option_widescreen`, its table entries sit
at the end of the GCT so the port can hide them from the in-game code handler (terminator at
`optional_gct_offset`), and its 8 data writes are applied or restored at run time. The presenter
letterboxes at 16:9 while it is on. Online safe (same as Dolphin users toggling it).

## Beta 0.1 work (2026-09-11 evening, Fable)

Chandler's second play test: hitches on moves and match load, audio crackle, 130-150 fps, no
settings found, later a black screen after enabling DLSS, no menu music, "Dolphin looks sharper".

- Hitches: pipelines now compile on worker threads (draw skipped until ready), frame queue depth 4,
  recipes merged across shader versions at `shadercache/recipes.bin` and prewarmed in parallel
  with progress in the title (6555 pipelines: 10-13 s cold, 0.6 s warm). Scripted match at
  unlocked fps: 270-310 fps.
- Black screen: DLSS chose an EFB scale whose render height exceeded the output (1280x960 render
  into a 1280x720 16:9 output) so NGX evaluate failed every frame (0xBAD00005) at 1000 fps for
  three hours, writing a 2.7 GB log. Fixed: scale must fit both dimensions, repeated evaluate
  failures switch back to native, Streamline errors are rate limited.
- Controller: his log showed `gc adapter: found but cannot open (5)`: another program (Dolphin)
  held the adapter. The port retries every 2 s once it is free.
- Sharpness: default internal resolution is now Auto (integer scale covering the window, like his
  Dolphin 3x at 1080p), anisotropic filtering 16x, optional 4x SSAA (EFB at 2x the chosen scale,
  box filtered on present), contrast-adaptive sharpening slider in the present pass (Streamline's
  DLSS sharpness is deprecated, so this works with and without DLSS).
- Music: Slippi Jukebox ported (`port/runtime/hle/jukebox.cpp`, HPS DSP-ADPCM decoder from
  hps_decode 0.3.0), mixed into the WASAPI/WinMM output; Music slider in PC settings.
- Settings: panel opens on first launch; Anti-aliasing, Anisotropic filtering, Sharpening,
  Sub-frame animation toggle (live), Music volume added; `--sharpness/--ssaa/--anisotropy`.
- Release: `MeleeUnlocked.bat` accepts a dropped ISO (`%1`) or `melee.iso`; zip ships the warmed
  recipes; README rewritten for the public repo; LICENSE (GPL-2.0).

Not done / answered: replay playback validation against Dolphin needs the Slippi Playback code set
(a second recompile variant) and is not implemented; ranked reporting; RTX; "DLSS 5" does not
exist in the SDK we ship (Streamline 2.10.3, DLSS 4 era; the NVIDIA App can override the DLL).

## Replay playback build (2026-09-11 night, Fable, in progress)

Chandler's validation request: play one of his Slippi Dolphin replays in both clients and require
identical positions, damage and results. Approach: a second translation of the game against the
Slippi Playback code set (`port/slippi_sys_playback`, from the Launcher's playback Dolphin) built
as `melee_port_playback.exe`; the EXI side of playback (prepareGameInfo, prepareFrameData,
IsStockSteal, IsFileReady, gecko list with Dolphin's denylist from the injection lists) is ported
in `port/runtime/hle/slippi_playback.cpp` over the vendored SlippiLib parser
(`port/third_party/slippilib`). The recording path records the played-back game, and
`tools/replay_compare.py <replay.slp>` diffs the recording against the original per player-frame
(state, position, facing, percent, stocks). Since the game installs the replay's own code list at
run time, the recompiler takes `--extra-gct gecko_list.bin --extra-gct-base <addr>` (the port logs
where the game put the list) so those caves run as translated code too.

### Result (2026-09-11 18:50)

`tools/replay_compare.py Game_20260321T214554.slp` (Marth vs Samus, Yoshi's Story, Slippi 3.19.0,
recorded on Slippi Dolphin in March 2026): all 9610 frames play back in the port and the port's
own recording matches the original on every player-frame for action state, x/y position, facing,
percent and stocks, except one value: Samus's y at frame -113 (spawn animation) differs by one
float ulp (0x41E05C00 vs 0x41E05BFF) and does not propagate. 19220 player-frames compared,
1 mismatch. Without the replay's own code list translated in (first run) the game diverged from
frame 221 on, so `--extra-gct` is required for playback builds.

Two more of Chandler's replays: Game_20260321T214328 (8161 frames, 1 mismatch: the same spawn ulp)
and Game_20260321T214020 (10035 frames, 29688 player-frames, 0 mismatches). All three carry the same
code list except one data word, so one playback build covers a Slippi version.

## Game reporting and ranked (2026-09-11 19:40, Fable)

Ported SlippiRustExtensions' game reporter and rank fetcher to C++ (`slippi_report.cpp`, WinHTTP,
nlohmann json): `reportOnlineGame` GraphQL mutation after every online game with the replay
upload (stored-block gzip) when the server returns an upload URL, `reportOnlineMatchStatus` for
set completion / game setup and start / abandoned on exit / poor performance, ISO MD5 (BCrypt,
cached by path+size+mtime), rank at login from the users REST API and after ranked games from
`getRankedMatchPersonalResult`, answered to the game through CMD_GET_RANK. A local test match's
report was accepted by internal.slippi.gg. Ranked mode is therefore no longer blocked on the port
side; it still needs a real ranked set against a Dolphin player to confirm the rank display.

## DLSS jitter sign (2026-09-11 19:50)

Calibrated by capturing the same static menu frame with `--dlss-jitter-sign 1` and `-1`: +1 blurs
text, -1 reconstructs it sharp (Laplacian energy 52 vs 155). Default is now -1.

## Flicker, stutter and sound at match start (2026-09-11 20:30, Fable)

Chandler's session log showed bursts of 70-98 new pipelines during a match even with 31k
prewarmed (new character/stage combinations), and each burst skipped 2k-20k draws while the
workers compiled: objects popped in and out ("a TON of flickering"). The simulation also stalled
on synchronous disc reads at match load (50 MB) and then ran at 61-63 Hz to catch up, which is
where the sound crackled.

- Fallback pipelines: a draw whose real pipeline is still compiling now renders with a generic
  pipeline (position, vertex colour, texture 0; one per raster state, built synchronously from
  tiny shaders) instead of being skipped. Approximate shading for a few frames, nothing vanishes.
- Disc reads are asynchronous on a worker thread; completion is delivered at a fixed virtual
  time (a quarter frame after the request, in order) so timing stays deterministic across runs
  (validate_native) while the simulation no longer blocks on the file system.
- After a stall the simulation resumes at 60 Hz instead of sprinting to catch up (the catch-up
  threshold went from 200 ms to 34 ms): no more fast-forwarded audio.
- In-client updater (`host/updater.cpp`, settings panel): checks the newest GitHub release,
  downloads the zip and hands over to `update.bat` (waits for exit, unpacks with tar, copies over,
  relaunches). `VERSION` at the repo root is the single source of the version string.
