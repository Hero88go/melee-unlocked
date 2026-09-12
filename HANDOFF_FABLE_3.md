# Melee PC port: handoff and next plan (2026-09-11, Fable, for Codex or the next session)

## Context

Repo `C:\Users\Chandler\NEW project\melee-unlocked`, branch `codex/port-completion`, build dir
`build-review` (Release). Play snapshot for Chandler: `build-review/port/Play/melee_port.exe`
(`tools/snapshot_play.bat` refreshes it; `run-unlocked.bat` launches it in a 1920x1440 window).
Latest commits: 91152d8 (longjmp fix, widescreen, checksum oracle, card HLE), 87c4733 (fullscreen
startup fix, melee_port.log), 895c783 (WASAPI audio), ab436a2 (handoff roadmap).
Uncommitted, built and measured: parallel authored solver in `port/runtime/gx/subframe.cpp`
(solver 2.2 ms -> 1.4 ms per presented frame). Commit it first.

Docs: `PORT_COMPLETION.md` (state + evidence), `HANDOFF_FABLE_3.md` (roadmap), memory file
`melee-pc-port.md`. Every session writes `melee_port.log` in the working directory; read it
instead of asking Chandler to paste the console.

## What was done today (all verified)

- Real Slippi online: a port instance queued Unranked at mm.slippi.gg and played a real Dolphin
  player (log `reports/native-validation/mmA/log.txt`). Checksum oracle added: the port compares
  the opponent's finalized-frame checksum with its own and logs `checksums agree through frame N`
  / `DESYNC`. Local two-instance matches: 0 mismatches over 2000+ frames.
- Crash fix: MSL `__longjmp` had been recompiled as a plain function (returned to the host
  caller). Now HLE throws `ppc::GuestLongJmp`; `port/recomp/emit.py` wraps every `__setjmp`
  caller in a retry loop with a catch that calls `ppc::longjmp_restore` and re-enters at the
  saved return address. This was the ~30 s online crash on animated stages (alarm queue corrupt).
- Widescreen 16:9: Slippi's optional code compiled both ways (`gecko.py RUNTIME_OPTIONAL`,
  `gecko::option_widescreen`), GCT tail hidden/shown at run time, data writes applied/restored,
  presenter letterboxes at 16:9. PC settings checkbox, `--widescreen`, `widescreen 1` in ini.
- Memory card: `port/runtime/hle/hle_card.cpp`, slot A as a folder of .gci files (Dolphin GCI
  folder format) at `--card-dir` (default `User/GC/CardA`). Game creates and reads its save.
- Audio: WASAPI shared mode on the default render device, WinMM fallback (`audio.cpp`).
- Fullscreen startup bug (white screen): swapchain was created at 1280x960 while the popup
  covered the monitor. Fixed in `threaded_backend.cpp` (real client size after fullscreen).
- Release: `tools/package_release.py --version X` builds a ~38 MB zip (no ISO/DOL); GitHub
  Releases + Issues recommended; `.github/ISSUE_TEMPLATE/bug_report.yml`.
- Tooling: `tools/online_pair.py` (two-instance online matches + timing analysis),
  `port/scripts/online_bot.txt` with `@match` / `@loop N` script directives, Slippi Launcher
  login fallback, `--card-dir`, render-thread errors logged.

## Chandler's notes from the play test (2026-09-11 14:30-14:45)

1. "Too many lags and freezes when doing moves and when loading into a match."
2. "Sound crackles sometimes and stutters sometimes."
3. "Make sure it's truly unlocked; I averaged ~130 fps, need constant 200 fps; probably
   unoptimized as a whole." (His log: 140-156 presented fps in a match, solver 2-2.7 ms +
   submit 3-4 ms per presented frame, ~1350 draws; my scripted match: 178-192 fps.)
4. "Don't know where the settings menu for graphics/DLSS is." (It is F1 or Z+Start in the game
   window; he never found it. Needs an on-screen hint.)
5. "Make DLSS not enabled by default" (it is off by default; the log line "DLSS feature ready"
   reads as if it were on). "Real DLSS with quality/performance choice" (the F1 combo has
   Native/DLAA/Quality/Balanced/Performance/Ultra Performance; untested by him).
6. Earlier: "controller not working", "sound not working" on the first launch. His log shows the
   adapter opened and WinMM audio at 70%; my test instance may have held the adapter at that
   moment. Unconfirmed after the relaunch.

## Beta 0.1 state (2026-09-11 18:20)

Packaged: `release/MeleePort-0.1.0-beta-win64.zip` (tools/package_release.py). Latest commit
63df903. Done since the plan below was written: P1 (async pipelines, merged recipes, prewarm with
progress), P2 partially (parallel solver; scripted match 270-310 fps), P3 (panel opens on first
launch, sharpening, SSAA, anisotropic, auto resolution, DLSS size fix + fallback), music
(jukebox), Slippi Sys vendored, README/LICENSE. Open: Chandler's "sprint looks weird" and
"backgrounds sped up" (sub-frame path; toggle exists), replay-vs-Dolphin validation (needs
Slippi Playback code set), ranked reporting, RTX, GitHub publish (needs his repo URL).

## Plan (priority order)

### P1. Kill the hitches (his items 1 and 2)

Root cause to confirm from `melee_port.log` "pso ... created N" counters and the frame-times
CSV (`--frame-times`): pipeline creation on the render thread during a match, and the sim thread
blocking behind it (`frame_queue.h` `push` waits while 2 frames are queued), which also starves
the AI DMA (audio crackle).

- `gx_d3d12.cpp` around line 615 (`CreateGraphicsPipelineState` in the draw path): compile new
  PSOs on a worker pool; while a PSO is pending, skip that draw (Dolphin's "skip drawing"
  behaviour). Keep the pipeline library store. Prewarm stays (`shadercache/<hash>/recipes.bin`).
- Ship a warmed `recipes.bin` in the release zip (play through menus, several characters and
  stages first) so first launches rarely compile.
- Never block the sim thread on the renderer: if the queue is full, drop the oldest frame instead
  of waiting (`frame_queue.h`). The sim must keep 60 Hz regardless of the renderer.
- Check other match-load stalls: disc reads (`host::disc_read`), texture decode/upload on first
  use. Log any single sim frame over 20 ms with what ran (`--hang-watch` exists).
- Verify: `tools/benchmark_native.py` / a scripted match with `--frame-times`; count frames over
  16.7 ms before/after; `audio: N blocks dropped` must be 0.

### P2. Constant 200 fps (his item 3)

- Commit the parallel solver (done, 1.4 ms). Next: the submit path, `constants` at 0.8-1.3 us per
  draw is the biggest item (`fill_vs_constants`, upload ring): upload once per draw identity when
  matrices are unchanged, or pack constants for all draws in one memcpy; then `upload` 0.23-0.38.
- Target: solver + submit under 4 ms at ~1400 draws, then 200 fps holds with headroom. Measure
  with the `display:` and `render cost:` log lines in a scripted match (`vs_match.txt`).
- Present pacing: with the cap set to "Match monitor" (200 Hz) the deadline pacing should be used
  rather than unlocked, so frames land on refresh boundaries. Check `--fps monitor` on a 200 Hz
  panel with the frame-times CSV.

### P3. Settings discoverability and DLSS (items 4 and 5)

- Draw an on-screen hint for the first 10 seconds ("F1 or Z+Start: PC settings") and put the
  key in the window title. ImGui overlay lives in `pc_settings.cpp`.
- Rename the boot log line to "dlss: available (off)" and make sure a fresh install starts with
  Upscaling = Native. Verify each DLSS mode renders (Quality/Balanced/Performance) and log the
  internal resolution DLSS picked. Jitter sign calibration is still unverified.

### P4. Controller and sound follow-up (item 6)

- Read his next `melee_port.log`: `gc adapter:` and `audio:` lines. If the adapter is on port 2,
  port 1 is keyboard by design (`window.cpp` input_poll); consider "first plugged controller
  drives P1" as an option.

### P5. Online follow-ups

- Direct-code and Teams tests against Dolphin players; ranked needs game reporting
  (`slippi_online.cpp` stubs, Slippi API POST with uid/playKey).
- Render observer reset on savestate load (rollback) so re-posing never uses stale joints.

### P6. Remaining roadmap

Authored gaps (looping wrap, animated scale, RObj joints), audio fidelity (AX mixer), Reflex
markers, DXR shadows/reflections, AA + sharpness slider, one differing replay byte, README.

## Verification checklist for any build handed to Chandler

1. `ctest --test-dir build-review -C Release` and `python tools/validate_native.py` (0 mismatches).
2. `python tools/online_pair.py --frames 5400 --script port/scripts/online_bot.txt`: both exit 0,
   `DESYNC` count 0, no `FATAL`, no `ALARM QUEUE`.
3. Scripted match at unlocked fps: `display:` >= 200 fps, `render cost:` under 4 ms, audio
   `0 blocks dropped`.
4. `tools/snapshot_play.bat` only after 1-3 pass; never overwrite Play while his process runs.

## Standing rules

Never touch the real Slippi install or the ISO. Another Dolphin (Opus session) may be running:
never kill by name, only my own PIDs by full path. Keep automated runs muted (`--volume 0`) and
hidden. No image interpolation; game logic stays 60 Hz. No em dashes in text. Keep going until
done; Chandler's usage is nearly out, so leave the tree committed and this file current.
