# Handoff: Fable session 3 (2026-09-11)

Branch `codex/fable-review`, build dir `build-review`, exe `build-review/port/Release/melee_port.exe`.
Build: `cmake --build build-review --config Release --parallel 1 -- /p:CL_MPCount=4` (in Git Bash prefix
`MSYS_NO_PATHCONV=1`). Regenerate guest code after emitter changes:
`python port/recomp/recomp.py --gct-base 0x8065CC80`. Validation: `ctest --test-dir build-review -C Release`
and `python tools/validate_native.py --iso <iso>` (VS match script, 3 render modes, 2400 checkpoints).

## What landed this session (all committed)

1. Slippi code tables re-integrated (Gecko applier, EXI device, VCDIFF game files, .slp recording).
2. Dispatch thunks for every Gecko cave instruction / hook / hook+4 / mid-function branch target
   (`Context::entry`, `analyze.py` thunks, `guest_table.cpp` `t_XXXXXXXX`).
3. Gekko interpreter fallback (`port/runtime/ppc/interp.cpp`) for code that only exists in RAM
   (SlippiCSS.dat `mnFunction`); `ppc::call` uses it for any unmapped target.
4. Input scripts with per-port entries (`p=N`); `port/scripts/vs_match.txt` reaches a 2-player match.
5. Slippi Online port: `slippi_net.{h,cpp}` (SFML-format packets, ENet netplay client, matchmaking
   client against mm.slippi.gg, user.json, direct codes), `slippi_online.{h,cpp}` (EXI online
   commands, savestates, lobby state). `--local-peer idx:port:ip:port` peers two instances without
   the server. Verified: full unranked match between two instances, replays identical except one
   state-flag byte at one frame; mm.slippi.gg accepted a ticket (`port_mm_probe`).
6. WUP-028 GameCube adapter over WinUSB (`gc_adapter.cpp`), rumble via PADControlMotor.
7. `run-unlocked.bat` (threaded renderer, `--fps unlocked --frame-mode authored`).

## Chandler's feedback on the first hands-on test and the fixes (in progress)

| Report | Cause | Fix |
|---|---|---|
| Menu bars jump up and down (text steady) | Sub-frame re-posing runs in menus; the geometric fallback extrapolates the eased menu-bar motion and overshoots every frame | Gate re-posing on "in a match": EXI sets `host::g_in_match` between CMD_RECEIVE_COMMANDS (0x35) and CMD_RECEIVE_GAME_END (0x39); outside a match the presenter shows each simulation frame as is (still at display rate) |
| No sound | The launcher default is `--volume 0` (dev constraint to keep my automated runs silent) | `run-unlocked.bat` passes `--volume 70`; my own test runs keep `--volume 0` |
| "Sim 60 Hz so it's not unlocked" | The title shows both numbers: the game logic is always 60 Hz, the display rate is the other number | Title reads `game 60 Hz (fixed by design)  |  display N fps` |
| Wants uncapped or monitor-rate cap, 200 Hz monitor | Only `--fps N|unlocked` existed and the window is a normal overlapped window | `--fps monitor` caps at the current display frequency (EnumDisplaySettings); `--fullscreen` borderless window covering the monitor so DWM does independent flip (true uncapped presentation with tearing allowed) |
| Slight freezes on certain moves (Bowser) | Pipeline state creation on first use of a shader combination (~9 ms each, includes HLSL compile) | Asynchronous shader/PSO compilation on a worker thread; the draw is skipped until its pipeline is ready (Dolphin's "async shaders" behaviour). Synchronous when capturing/hidden/headless so validation stays deterministic. The on-disk cache (`shadercache/`) makes later runs stutter-free |
| Where are DLSS / ray tracing options | Not implemented (milestone M7) | Roadmap below |

## Remaining roadmap (priority order)

1. Finish the fixes above if any are still uncommitted (check `git status`), rebuild, re-run
   `tools/validate_native.py`, commit.
2. Authored mode covers only rigid draws (`c1` capture failures are envelope-skinned fighters).
   Extend `capture_authored_pose` to envelope draws: SetupEnvelopeModelMtx observer already fires;
   capture the JObj chain per skin matrix (posMatrices index -> joint) and re-sample all joints.
   This is what makes fighters truly sub-frame animated instead of matrix-extrapolated.
3. Rollback and the render observer: a savestate load restores heap contents without JObjAlloc/
   Release hooks firing. Consider `gx::observer_reset()` on load (`slippi::online::rollback_count()`).
4. One differing byte (post-frame state bits 5, frame 1413, player 0) between the two local peers'
   replays: check whether Dolphin shows the same (it may be a non-synced visual flag).
5. Game reporting (`slprs_game_report_*` in Rust) and rank fetch: HTTP POST to Slippi's API with
   uid/playKey; needed for ranked. Stubbed with log lines in `slippi_online.cpp`.
6. Memory card (CARD HLE backed by an image in the user folder) so settings/unlocks persist.
7. Audio fidelity: AX mixer is approximate (resampling, some DSP commands).
8. RTX/DLSS/Reflex (M7): D3D12 already; add NVIDIA Streamline (DLSS SR/DLAA + Reflex markers around
   sim/present), motion vectors from the sub-frame matrices (previous/current per draw), DXR
   shadows/reflections with BLAS refit per frame, AA selector disabled while DLSS is on, separate
   sharpness slider. Settings UI: an in-window overlay or a config file `port.ini` read at start.
9. Packaging: standalone folder, README, no ISO/DOL/generated code in the repo.

## Standing rules

Never modify the real Slippi install or the ISO. Another Dolphin (other session) runs unrelated
tests: only stop processes by full path under this project, never by name. Keep Chandler's Chrome
audio untouched. No image interpolation: sub-frame frames come from re-posed geometry only.
