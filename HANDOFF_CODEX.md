# Melee Unlocked: handoff for Codex (2026-09-11, written by Fable)

Repo `C:\Users\Chandler\NEW project\melee-unlocked`, GitHub https://github.com/hero88go/melee-unlocked
(public, branch main; the old melee-port URL redirects). Build dir `build-review` (Release, VS 2022):

    MSYS_NO_PATHCONV=1 cmake -S . -B build-review -DMELEE_BUILD_EXPERIMENTAL_PORT=ON   # after adding sources (GLOB)
    MSYS_NO_PATHCONV=1 cmake --build build-review --config Release --target melee_port melee_unlocked --parallel 1 -- /p:CL_MPCount=6

Chandler plays `build-review\port\Play\melee_port.exe` via `run-unlocked.bat` (refresh with
`tools\snapshot_play.bat` only when no melee_port.exe is running). His log: `melee_port.log` at the
repo root (read it before asking him anything). Test runs must use `--log-file` and `--volume 0
--hidden` so they never clobber that log or make noise. Never touch the real Slippi install or the
ISO. Never `Stop-Process -Name`; kill only PIDs under this project. No em dashes in any text.

Docs: `PORT_COMPLETION.md` (technical state + evidence), `HANDOFF_FABLE_3.md` (history and the
older roadmap), `README.md` (public). Memory file for Claude: `melee-pc-port.md`.

## Where things stand (all verified unless marked)

Working and released (v0.1.3-beta, 2026-09-11 22:50, tag 102b01d):
- Static recompilation of the 1.02 DOL + Slippi Gecko codes, native D3D12 renderer, 60 Hz game
  logic with unlocked display rate and authored sub-frame animation (Predict / Interpolate modes).
- Slippi online against real Dolphin players (mm.slippi.gg), game reporting + replay upload, rank
  fetch. Three of Chandler's Dolphin replays reproduce frame-exact in the playback build.
- Memory card (GCI folder), WASAPI audio, jukebox music, widescreen 16:9, DLSS/DLAA via Streamline,
  SSAA, anisotropic, CAS sharpening, PC settings panel (F1 / Z+Start, corner button).
- Melee Unlocked Launcher (`port/app/launcher.cpp`, target `melee_unlocked`, plain Win32): Play
  page (ISO, Slippi account line, version), Build tab (drop ISO: header check, source build when
  in a checkout, pipeline precompile), updater with a yes/no prompt (never auto-installs).
  Optional; `MeleeUnlocked.bat` is the manual path. `VERSION` file is the single version source.
- Updater `port/runtime/host/updater.cpp`: GitHub release list (pre-releases included), download,
  `update.bat` swap, relaunch. `shutdown()` must be called before exit (joinable thread caused
  0xC0000409 at exit when the settings panel had started a check; fixed 2026-09-11 20:20).
- Packaging: `python tools/package_release.py` (reads VERSION) -> `release/MeleeUnlocked-<v>-win64.zip`
  with launcher, bat, Sys, warmed `shadercache/recipes.bin`, README, licenses.
  Publish: `git push origin HEAD:main`, `gh release create vX -F notes --prerelease zip`.
- Discord: `.github/workflows/discord-release.yml` posts each published release to a webhook.
  Needs the repo secret `DISCORD_WEBHOOK` (Chandler creates the webhook in his server; see the
  file header). Not set yet.

## Fixed today (2026-09-11 evening) for Chandler's stability complaints

His symptoms: textures turning black in matches, whole-screen flicker, audio cutting out,
"fuzzy" image vs Dolphin, black screen on borderless fullscreen.

1. Pipeline explosion: the shader uids hashed all 16 TEV stages, all tref/ksel and all 8 texgens
   even when unused, so leftover register state made new pipelines forever (32313 in his one
   session, 30-50 new per 10 s mid-match). `gx_shader.cpp` make_ps_uid/make_vs_uid now mask to
   numtevstages/numindstages/numTexGens (like Dolphin). A cold scripted match now creates 116
   pipelines total.
2. Black surfaces: draws whose pipeline was compiling used a generic fallback pipeline (texture 0
   times colour), which renders wrong for many TEV setups. `get_pso` now waits up to 12 ms per
   presented frame (`pso_wait_budget_us_`) for the worker to deliver the real pipeline and only
   then falls back; a draw's own job is pushed to the front of the queue.
3. Audio: WASAPI underruns filled silence without counting. Now counted (`audio_underruns`,
   logged at exit as "N output gaps (M ms of silence)") and after an underrun output holds until
   48 ms are buffered (`PREFILL_FRAMES`) instead of crackling near-empty.
4. Fullscreen black screen: `window_set_fullscreen` replaced the style with bare `WS_POPUP`,
   dropping WS_VISIBLE, so DWM stopped composing the window (screen kept a stale frame). Now
   `WS_POPUP | WS_VISIBLE` + SWP_SHOWWINDOW. Alt+Enter toggles fullscreen (`window_take_fullscreen_toggle`).
   Verified with a monitor capture: game renders at 1920x1080 after the toggle, exit code 0.
5. Sharpness: the present blit used one bilinear tap; at 5x-10x internal resolution on a 1080p
   window that skips most rendered pixels. The blit now box-filters the whole footprint of an
   output pixel (up to 4x4 taps, `sharp.w`), so high internal resolution + SSAA looks
   supersampled like Dolphin's 3x + 4x SSAA. DLSS Quality/Balanced/Performance render BELOW the
   window size by design (1280x960 at 1080p): that was the "fuzzy" look in his session
   (`dlss 2` in his ini). The panel now explains this and recommends Native 3x+ with SSAA or DLAA.

Verification on the v0.1.3-beta exe: validate_native 2400 checkpoints 0 mismatches, cold-cache
scripted match 0 audio drops / 2 gaps 15 ms, mid-match capture clean (Battlefield, both fighters,
correct textures), two-instance online match 0 desyncs on both peers.

## Still to do (priority order)

1. Re-run after the latest build and before any release: `python tools/validate_native.py --iso <iso>`,
   `python tools/online_pair.py --frames 5400 --script port/scripts/online_bot.txt` (no DESYNC),
   a cold-cache scripted match (`--shader-cache <fresh dir>`, `--frames 4200 --script
   port/scripts/vs_match.txt --card-dir <copy of User/GC/CardA>`): expect 0 fallback draws after
   the first seconds, `sim 60 Hz` everywhere, few output gaps.
2. Sim stalls during cold compiles: the 12 ms render-thread wait can fill the frame queue (depth 4)
   and block the simulation (one `sim 45 Hz` interval mid-match with a cold cache). Options: drop
   the oldest queued frame instead of blocking the sim (`frame_queue.h`), lower the budget to
   6 ms, and ship a recipes.bin generated with the new uids (play menus + several characters and
   stages, then copy `shadercache/recipes.bin`; the package script picks it up).
3. Chandler's open reports to confirm with him: "sprinting looks weird", "some backgrounds sped
   up" (A/B with Sub-frame animation = Interpolate), sound "not perfect" (read the new gap count
   in his melee_port.log after his next session), remaining "whole screen flicker" (not
   reproduced in captures; if it persists, capture a burst with `--capture --capture-sim-frame N
   --capture-burst 8` at the moment he reports).
4. Ranked: reporting is implemented and accepted by the server, but no live ranked set has been
   played from the port. Test with his account only with his consent.
5. RTX (ray tracing) not implemented. Audio is an approximate mixer (AX ucode port is the real fix).
6. One-ulp spawn divergence in two Dolphin replays (does not propagate).
7. Discord webhook secret (needs Chandler), then publish a release to test the workflow.
8. Release hygiene: bump `VERSION`, `release/RELEASE_NOTES_<v>.md`, package, `gh release create`.
   Users on an older version get the launcher prompt automatically.
