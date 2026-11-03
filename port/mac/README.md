## Staging agent-test variant

`python3 -B port/mac/build_full.py --agent-test-hook` enables the staging-only
`agent-test.json` hook in app/runtime compile units and builds/packages separately
under `build-mac-full-hook`. Omitting the flag builds `build-mac-full` with hooks
off. Both variants use the same `MELEE_GAME_LIBRARY` input and recomp headers.
Pass `--agent-test-hook` to `check_full.py` and `package_full.py` to select staging;
the package README and manifest label it **STAGING ONLY**. Release packaging runs
`port/tests/agent_test_hook_absent.sh` and refuses any hook-bearing artifact.
`package_full.py --agent-test-hook --output-dir dist/exp13` makes the unsigned
staging bundle there. `check_full.py policies --agent-test-hook` runs offline
L-cancel/ring regressions for arm64 and x86_64 plus the adapter/merge source guards.

> M7-0956 source now targets 0.9.56 and the native no-stack-protector ELF.
> The required shared queue was refused by the sandbox; no new staging build or
> runtime evidence was produced. See [M7-0956.md](M7-0956.md) for current changes,
> measured static checks, the refusal, and pending validation. Results below are
> historical M1 to M6 evidence, not 0.9.56 validation.

# Full Mac host spike (M3)

`build-mac-full/MeleeUnlocked.app` is the full Source Port host for x86-64 macOS
15+, running through Rosetta on Apple silicon. It compiles the Slippi/MU network
code, YouGame stdio bridge, replay reader/writer and shared ImGui settings panel.
The game remains the provided GCC Linux ELF; no game or dependency is rebuilt.
This is an unsigned local evidence build, with no signing, upload or release.
M2 remains a separate target (`bash port/mac/build-app.sh`, `build-mac/`).

## Modes

The same full host supports the YouGame player box and a separate game window.
Both modes use asynchronous translation by default, the split boot/frame prewarm
budget, and the bundled scale-2 seed. `MELEE_SYNC_SHADERS=1` remains diagnostic.

**Embedded:** the app passes `YOUGAME_STDIO=1`, `YOUGAME_EMBED=1` and
`YOUGAME_EMBED_PARENT=<decimal CGWindowID>` (the ID from Electron's
`window:<id>:0` media source). The host consumes the parent variable and wraps a
borderless, nonactivating AppKit panel with `SDL_CreateWindowFrom`. The panel
stays hidden until it has a valid rect, a completed swap and 500 ms of stable
parent bounds. The app owns fullscreen and size: `--fullscreen` is ignored
before the regular-window deferred-show decision, Alt+Enter and host size/fullscreen
controls do nothing while embedded. Escape sends exactly one existing
`yg {"key":"escape"}` event per press, before ImGui/fullscreen/menu input;
the app handles the transition from page fullscreen to theater and sends the new
rect. F1 opens/backs the game menu; F11 retains the legacy settings shortcut.
Tab retains the practice-overlay shortcut.

The app sends newline-delimited stdin commands:

```text
embed rect 40 68 800 500
embed show
embed focus
embed blur
embed hide
embed quit
```

Rects are points from the parent frame's top-left, including its title bar;
`frameRectLine` already adds that offset. Do not apply Retina scaling again.
Reissue rects when the player box, title bar, screen or app fullscreen changes.
Sides below 32 points hide the panel even after `show`. Focus/blur control its
keyboard focus without activating the game process. Parent bounds are cached
for 100 ms and ordering for 250 ms; explicit refreshes have a 20 ms floor.
Hidden/minimized/other-Space parents keep the panel hidden without fallback.
An actually missing parent gets a two-second grace, then the same surface becomes
a regular titled/resizable window. Invalid/missing IDs select a regular SDL window.
`--hidden` remains hidden through fallback. No screen capture, accessibility or
privacy grants are used. The adapter reads only the target window's bounds,
layer, number and owner PID, plus window-ID membership.

**Windowed/full screen:** without embedding, the host uses tonight's SDL window
and GL surface update behavior. `--fullscreen` starts hidden and shows the first
complete game picture in desktop fullscreen. Alt+Enter toggles fullscreen;
Escape first leaves SDL or native AppKit fullscreen without opening the menu.
Windowed Escape retains menu/back behavior. F1 opens settings and F11 the legacy
settings panel. After parent-loss fallback these normal window controls resume,
without automatically entering fullscreen.

For either mode the app passes `--iso`, `--threaded-renderer`, `--settings-path`,
`--user-dir`, `--replay-dir`, `--card-dir`, `--log-file`, `--shader-cache`,
`--vanilla-only`, and optionally `--fullscreen`. Caller paths are resolved before
changing cwd; mutable outputs stay in those supplied profile/output paths.
Read-only assets come from `Contents/Resources`. The main thread owns Mac GL
presentation even when `--threaded-renderer` is supplied. The app owns signing
and staging; this local bundle is unsigned.

`python3 -B port/mac/check_embed.py` compiles the real adapter, SDL/AppKit link,
manual parent helper and all three affected host translation units. It runs only
the 84 pure policy checks, never AppKit or a game. Outputs are under
`build-mac-embed/`; each direct-clang compile and pure run checks >=15% free
memory, with at most two compiles, bounded owned PIDs and throwaway HOME/XDG.
`embed_test_parent.mm` is a lead-only manual helper; it is compiled, never run by
these automated checks. The lead owns the visible embed/fullscreen checks.

## M6 combined host evidence (2026-10-25)

The working tree combines `mac-full` at `0e3f7a7` with `e90bcb8`'s embed spike;
no git merge/commit was performed. The existing `after_swap(uint64_t)` hook calls
both embed readiness and regular-window first-frame handling. Embedded Escape
runs before regular fullscreen handling; embedded fullscreen startup is
suppressed before deciding to hide the window. Async translation, seed merging,
split prewarm, fullscreen surface updates and regular Escape behavior remain.

The full direct-clang build and `package_full.py` succeeded. M6 checks completed:
**84/84** pure embed checks, real adapter/SDL/AppKit link, manual-helper compile,
all three shared hook compiles, and **7/7** translation checks (async/sync stages
byte-identical; enqueue/check time 0.339 ms). No windows or game were launched.
These short pure checks used the direct-clang exception and per-build/run memory
gates. No checked M6/pure-test log contains `slippi: logged in as`.

The required shared-queue attempt for `check_full.py unit` was refused: shared
`~/.cache/yg-heavy-slots` operations and `sysctl` returned `Operation not
permitted`. The queue was stopped via its owned execution session (exit 130).
A process-list attempt to identify that owned queue PID was also refused; no
process-list retry was made. No queue bypass was attempted. **M6 unit-suite and
cold/seed parity reruns remain pending for the lead.** The existing **22/22**
unit and **1,672 match frames / zero differing values** cold/seed results below
predate this merge. The lead also supplied rendered `0e3f7a7` evidence at scale 2:
all three 3,300-frame runs matched 1,672 frames with zero differences. Frames
above 33 ms were sync cold **26** (worst **1,624 ms**), async cold **112**
(**619 ms**), async + seed **8** (**67 ms**). Async + seed remains the shipping
default; these measurements do not validate the merged embed surface.
Visible checks and GL compile/link timing require the lead;
no AppKit initialization was attempted in this sandbox.

The unsigned bundle is `build-mac-full/MeleeUnlocked.app`: **63,468,262 bytes**
(60.53 MiB file contents; `du -sh`: 62M), 872 files, zero symlinks, minimum macOS
15.0. `MELEE_GAME_LIBRARY` was explicitly set to the official
`spike/melee_game.so` when repackaging; its bundled bytes match. Resources include
both shader tools and the scale-2 seed (349 GLSL files, 748 recipes).
Top level: `Contents/{Info.plist,MacOS/MeleeUnlocked,Frameworks/libSDL2-2.0.0.dylib,
Resources/}`. Resources contain `bin`, `shader-seed`, `melee_game.so`,
`melee_game.snapexcl`, `Sys`, `SysPlayback`, `slippi_sys_general`, `lang`,
`Shaders`, `ui`, `ui_sources`, `licenses`, `TrainingMods.md`, `EXPERIMENTAL.txt`.
Build/check logs and the JSON bundle audit are under `build-mac-full/m6-*`;
embed logs are under `build-mac-embed/`.

The lead can finish the blocked headless checks outside this sandbox, from this
worktree, with the existing memory-gated, muted, isolated runners:

```sh
m6check() {
  /Users/luis/Code/brawl-dolphin/notes/lead-records/yougame-v2-org/tools/heavy-slot.sh run --prio 1 --label melee-ports:m6-verify -- python3 -B port/mac/check_full.py "$@"
}
m6check unit
m6check parity
m6check parity --seed
```

Proposed commit: `mac: combine full-host parity and player-box embedding`.
Leave the existing `sourceport/extern/melee` submodule modification untouched.

## Full build

All inputs must already be on disk. From this worktree:

```sh
mkdir -p build-mac-full/gen
python3 tools/extract_dol.py /Volumes/YGWorktrees/build-out/test-discs/GALE01r2.iso build-mac-full/gen/main.dol
python3 port/recomp/recomp.py --dol build-mac-full/gen/main.dol --gct-base 0x8065CC80 --out build-mac-full/gen/port-generated
bash port/mac/build-full.sh
```

The DOL, translation headers and generated sources stay under `build-mac-full/`;
never commit them or copy them elsewhere. `build-full.sh` compiles incrementally
with Apple clang, at most two jobs, `-arch x86_64 -mmacosx-version-min=15.0`,
AVX2/FMA on runtime sources and `-Wl,-pagezero_size,0x10000` at link. The entry
point uses the same Rosetta/Intel CPU check as M2 (`cpu_check.h`). Each compile
and link checks `memory_pressure` for at least 15% free memory, waiting 60 seconds
otherwise. This uses the M3 brief's direct-build exception; no heavy-slot bypass
is implied for other work.

Inputs: `../deps/sdl2`, `../deps/tools/bin/{glslangValidator,spirv-cross}`,
`spike/melee_game.so`, and `spike/MeleeUnlocked-0.9.52/{melee_game.snapexcl,Sys}`.
The existing M1 inputs were already copied into this worktree's untracked
`spike/`. Shader tools are the provided self-contained universal executables;
the glslang symlink is dereferenced while copying (equivalent to `cp -L`).

The app contains `Contents/MacOS/MeleeUnlocked`, SDL2 under `Frameworks`, and
`Resources/{melee_game.so,melee_game.snapexcl,Sys,SysPlayback,slippi_sys_general,
lang,Shaders,ui,ui_sources,licenses,bin}`. Runtime assets and notices follow
`tools/package_linux_release.py`. Linux launchers, udev rules and Linux shared
libraries are omitted; optional 7-Zip, xdelta and librashader are not supplied.
Linux file/folder picker helpers are also unavailable on Mac.
No disc image, generated DOL or translation output enters the bundle. Packaging
asserts zero symlinks. Repackage an existing full binary with
`python3 -B port/mac/package_full.py`.

## Launch and checks

The executable accepts the YouGame launch arguments `--iso`,
`--threaded-renderer`, `--settings-path`, `--sys-dir`, `--user-dir`,
`--replay-dir`, `--card-dir`, `--log-file`, `--shader-cache`, `--vanilla-only`.
Arguments with paths are made absolute before the working directory changes to
`--user-dir`; defaults for settings, cards, replays, logs and caches are placed
under that profile. Relative auxiliary outputs and crash reports also stay
there. Read-only system/game/shader/UI assets resolve from `Contents/Resources`.
YouGame owns its own disc history; the Mac host does not write Windows launcher
history. `--threaded-renderer` is accepted, with presentation on the main thread
because creating the GL context on a render thread deadlocks AppKit.

```sh
python3 -B port/mac/check_full.py unit
python3 -B port/mac/check_full.py parity
python3 -B port/mac/check_full.py bridge
python3 -B port/mac/check_full.py compat
python3 -B port/mac/check_full.py graphics
python3 -B port/mac/check_full.py netplay
```

These checks use only the team disc, muted output, `SDL_MAC_BACKGROUND_APP=1`,
`--headless` or `--hidden`, empty per-check HOME/XDG/APPDATA directories and
explicit profile/output paths. They inherit no account or bridge environment.
Each process is bounded to 90 seconds, only its own PID is killed on timeout,
and a log containing `slippi: logged in as` fails the check. Rosetta checks use
`ROSETTA_ADVERTISE_AVX=1`, as in the M2 evidence runs. No normal visible or audible
launch is part of this spike. Unit builds use the root CMake targets with Apple
clang and `--parallel 2`; the per-compiler launcher also checks memory.

## M3-parity update (2026-10-25)

The full host now has the M2 GL surface update and probe, a hidden-until-first-frame
`--fullscreen` start, and Escape exits either SDL fullscreen or the green button's
AppKit fullscreen before the menu sees the key. Windowed Escape retains the
existing menu behavior. The YouGame bridge, menu/settings and netplay code remain
in this full target; this work does not validate desktop datagram handoff.

Unseen HLSL stages go to **one translation worker**. Only that worker runs
`glslangValidator` and `spirv-cross`, atomically publishing the cached GLSL. The
frame thread reads the completion without waiting and skips draws with an
unfinished program. It compiles and links ready GLSL on the frame thread, sharing
`MELEE_ASYNC_COMPILE_MS=3` across all on-demand work and prewarm in each submitted
frame. GL calls are indivisible: one call may exceed 3 ms, then no further compile
or link starts that frame. Cached GLSL bypasses helpers; linked programs draw
immediately. `MELEE_SYNC_SHADERS=1` restores synchronous translation and unbudgeted
on-demand compilation for diagnosis. Skipped draws are counted in stderr once
per second (and at shutdown). Failed translations log once and remain skipped;
the worker continues with other shaders. Shutdown drops queued work and joins
the active translation; each helper has a 15 s timeout and kills only its own PID.

Seed merge preserves existing player GLSL, merges/deduplicates recipes and retains
the whole recipe list even when prewarm is interrupted. Apple prewarm allows
`MELEE_PREWARM_BOOT_MS=500` before startup and `MELEE_PREWARM_FRAME_MS=4` between
frames; the latter also obeys the shared 3 ms compile allowance. The full launcher
sets `MELEE_SHADER_SEED` to bundled `Resources/shader-seed` when present. An
explicit empty value disables it for cold-cache checks. Both packaging scripts
copy `spike/shader-seed` if present. `seed-merge.py` is the reference merge tool:

```sh
python3 -B port/mac/seed-merge.py spike/shader-seed <cache-dir> [<cache-dir> ...]
```

The cache family matches M2, `gx-gl410-v2-s<N>`, **but the supplied M2 seed covers
only s1 and the full host defaults to s2** (Auto at 1280x960). Scale cannot be
relabelled because the fragment-coordinate height changes. `seed_build.cpp`
regenerates scale 2 using the production translator, without SDL, GL or the game:

```sh
python3 -B port/mac/check_full.py seed-build
python3 -B port/mac/package_full.py
```

This produced **349 scale-2 GLSL shaders / 748 recipes**, with all 349 generated
shader hashes also found in the supplied s1 seed. Both s1 and s2 are bundled.
Other scales still use async misses unless a matching seed is supplied. The seed
and game inputs stay in untracked `spike/`; no disc/generated DOL belongs in git.

The game-library packaging input is **`MELEE_GAME_LIBRARY` in `package_full.py`**;
default `spike/melee_game.so` (the official library used for all evidence here).
To swap in melee-native's already-built Linux ELF, repackage without changing code:

```sh
MELEE_GAME_LIBRARY=/absolute/path/to/melee-native/build/melee_game.so python3 -B port/mac/package_full.py
```

Pre-M6 evidence from `0e3f7a7` is under `build-mac-full/` (not rerun by the merge):

- `m3-parity-unit.log`: **22/22** root/YouGame unit tests passed.
- `checks/translation/console.log`: **7/7** worker/helper checks passed; duplicate
  requests return the same pending result, cache hits remain immediate while
  the worker is blocked, errors do not stop later jobs, async/sync generated GLSL
  matches byte-for-byte for real vertex and texture/sampler fragment shaders,
  and completed files match published results. Caller queue/check time: **0.611 ms**.
- `checks/parity-async-cold/compare.txt` and `checks/parity-async-seed/compare.txt`:
  each has **3,300 retraces; 1,672 match frames x 76 fields; 0 differing values**
  against Windows (127,072 compared values per run). Both start at frame 1,629.
  Wall times **13.240 s cold / 11.437 s seed-present**. The profiles start empty.
- These parity runs are **headless simulation checks**: the shader switch is on
  and the seed is present in the second run, but headless mode creates no GL
  renderer and therefore exercises neither draw skipping nor renderer seed merge.
  Rendered async parity and seed startup still require the lead's commands below.
- No `MELEE_FRAME_TRACE` exists in this full host. The runner records every frame's
  existing `MELEE_SIM_TIMES` CSV and reports `sim_ms > 33` in `timing.json`.
  Headless async cold/seed each had **0 / 3,299** work intervals over 33 ms
  (worst **32.178 / 29.450 ms**). **Rendered sync-versus-async hitch counts are
  unavailable**: the established AppKit/HIServices sandbox refusal prevents even
  a hidden SDL window. This step was not retried or routed around.
- All completed checks have zero `slippi: logged in as` entries. No network
  service is enabled in scripted Mac checks, including the lead's visible runs.
  No signing, production operation, upload or dependency installation occurred.

### Exact graphics/fullscreen commands for the lead

Run outside this sandbox from this worktree. Each invocation creates new HOME,
XDG, settings, card, replay and log directories; uses only the team ISO; mutes
output; gates memory at >=15%; disables the adapter/spectator/matchmaking; and
kills only its owned host PID after 120 s. These visible checks retain the full
settings UI. The shared queue owns each external long check:

```sh
cd /Volumes/YGWorktrees/ClaudeWorktrees/melee-ports/m3-mac-full
m3check() {
  /Users/luis/Code/brawl-dolphin/notes/lead-records/yougame-v2-org/tools/heavy-slot.sh run --prio 1 --label melee-ports:m3-parity-window -- python3 -B port/mac/check_full.py "$@"
}

# Hidden rendered parity + cold-cache hitch comparison; then a seeded run.
m3check graphics --scale 2 --sync-shaders
m3check graphics --scale 2
m3check graphics --scale 2 --seed

# Windowed resize, SDL fullscreen + Escape, and green-button fullscreen + Escape.
m3check graphics --windowed --scale 2 --seed --video-test resize
m3check graphics --windowed --scale 2 --seed --video-test sdl
m3check graphics --windowed --scale 2 --seed --video-test appkit

# Deferred fullscreen launch + Escape; separately, windowed Escape opens the menu.
m3check graphics --windowed --scale 2 --seed --fullscreen --video-test escape
m3check graphics --windowed --scale 2 --seed --video-test menu
```

Each graphics run executes the 3,300-frame scripted match at normal pacing and
compares its digest. Results, `command.txt`, the explicitly constructed test
environment, `frame-times.csv`, `timing.json` and `present-*.ppm` are under
`checks/graphics-{sync,async}-{cold,seed}[-<video-test>]`; old results are renamed
with `-previous-<timestamp>` rather than overwritten. Compare sync/async cold
`hitches_over_33ms`, not the headless numbers above. Require 1,672 compared match
frames and zero differing values for each rendered parity run.

The log must report `surface probe ... full size` after resize/fullscreen, never
`SMALLER than the window`. Fullscreen must fill the desktop with the game at the
selected aspect, with no old-size picture in its middle. Escape logs
`leaving full screen (menu untouched)` and must leave the process alive. The
fullscreen-start log must say `first frame ... showing the window in full screen`;
no visible startup window precedes that picture. The separate menu run must
show the menu and keep the game running. Also exercise F1/settings and return
to the match. `MELEE_TEST_{RESIZE,FULLSCREEN,TOGGLE,ESCAPE}` counts are **presented
frames** in the full host; `MELEE_TEST_SHOT` captures the scaled, overlaid window
picture before swap. `MELEE_TEST_SURFACE` enables the far-corner surface probe.

## M3 evidence and limitations

- Full host and unsigned app: built successfully; bundle size about 55 MB.
- Root CMake: **22/22 unit tests passed** on x86-64/Rosetta.
- Packaged headless parity: **3,300 retraces; 1,672 match frames × 76 fields;
  zero differing values** against Windows (127,072 compared values). Both match
  starts are host frame 1,629; 983 pre-match rows differ as expected. Wall time
  22.079 s. Evidence: `build-mac-full/checks/parity/{host.log,digest.csv,compare.txt}`.
- The first parity attempt had no match frames: POSIX `_putenv_s(name, "")`
  incorrectly retained an empty variable, enabling the game's lag-test boot.
  Correcting it to remove the variable restored the reference path. The initial
  run is retained under `checks/parity-previous-*`; it is not counted as a pass.
- Mac compatibility regression checks passed: MD5/SHA-1/SHA-256 standard vectors,
  streaming updates, short-output/error statuses, empty environment removal and
  tracked virtual-memory release. Evidence: `checks/compat/console.log`.
- Real host transport integration passed **2 stdio exchanges (`hello`, `mpCode`)**
  against a synthetic local app, with no sockets or account files. It links the
  same full-host runtime/app objects; it does not claim a real desktop match.
  Evidence: `checks/bridge/{result.json,console.log}`. A 30-frame vanilla boot
  did not request an identity (the online layer initializes lazily), so the
  transport check uses a separate entry point calling `wait_identity`.
- Hidden graphics aborted in AppKit/HIServices during SDL video initialization,
  exit 3 after 11.522 s, before a GL context or settings UI could be validated.
  Evidence: `checks/graphics/{console.log,host.log,user/melee_port_crash.txt}`.
  Dependent graphical/settings checks stopped; no privacy or sandbox changes.
- Loopback UDP bind was refused with `EPERM` / `Operation not permitted`; the
  two-instance match was skipped, without retrying the blocked socket operation.
  Evidence: `checks/netplay/refused.txt`; the external commands are below.
- M2 rebuilt successfully after the shared CPU/audio changes:
  `build-mac/MeleeUnlocked.app`; evidence `build-mac-full/m2-build.log`.
- All completed checks have zero `slippi: logged in as` entries. An initial
  automated run's default spectator bind was refused; automated Mac runs now
  disable that listener before starting the game.
- CommonCrypto provides MD5/SHA-1/SHA-256; `arc4random_buf` provides random bytes;
  HTTP uses SDK curl. No OpenSSL dependency. Windows status codes use signed
  32-bit values on LP64 so failures remain failures.
- Media decoding is deliberately stubbed on Apple: no FFmpeg video backgrounds,
  decoded border pictures or compressed music-pack playback. Game AX audio is
  compiled in; DSP coefficients resolve from the bundle.
- Raw HID and libusb GameCube adapter support are unavailable; SDL controllers
  and keyboard are compiled in. No macOS privacy grants or input-monitoring APIs
  are added. Steam/process discovery is unavailable. The Mac routing table uses `sysctl`;
  NAT-PMP/UPnP and actual Internet matchmaking remain unvalidated.
- The existing spike ELF loader patches exactly 5,928 Linux TLS canary sites.
  Host exit exceptions are caught inside callbacks and leave ELF execution via
  setjmp/longjmp. This still needs a production game built without stack protector
  and a reviewed lifetime/unwind boundary.
- YouGame's stdio request/response and broker stream are compiled in. The legacy
  WinHTTP WebSocket transport is unavailable on Apple. This checkout's newer
  direct datagram handoff takes Windows shared-memory/event handles only; no Mac
  fd handoff contract or desktop implementation was provided. A direct-mode
  match is refused explicitly. **Online YouGame matches are not yet proven**;
  the Mac app's datagram handoff remains a release blocker. Local Slippi UDP
  tests do not prove that desktop integration.
- A Python bytecode-only validation was refused when the system Python attempted
  to write its default cache under `/Users/luis/Library/Caches/com.apple.python`.
  That check was stopped and was not retried around the sandbox restriction.

## Two-instance local test for the lead

Loopback was refused here. In an authorized environment, run these exact two
commands in separate terminals from this worktree (each enforces memory, 90 s,
muting and fresh HOME/XDG/profile isolation; neither enables matchmaking):

```sh
python3 -B port/mac/check_full.py peer-0
```

```sh
python3 -B port/mac/check_full.py peer-1
```

The first launches the packaged host with `--headless --volume 0 --frames 1200
--online-test direct --local-peer 0:43171:127.0.0.1:43172 --test-stage 2
--rng-seed 0x12345678 --time-base 0`; the second uses
`--local-peer 1:43172:127.0.0.1:43171` with the same other flags. Both use the
team ISO and explicit `--settings-path --sys-dir --user-dir --card-dir
--replay-dir --log-file --shader-cache --state-digest` paths. Each complete host
command is written to `build-mac-full/checks/netplay-{0,1}/command.txt`. Check for
actual connected/match frames and compare their digests by `match_frame`:

```sh
python3 spike/compare.py build-mac-full/checks/netplay-0/digest.csv build-mac-full/checks/netplay-1/digest.csv
```

A one-command bounded two-process runner is `python3 -B port/mac/check_full.py
netplay`; it first checks whether loopback is allowed and stops on refusal.

# Offline Mac build (M2 historical evidence)

**Experimental, not tested yet.** `build-mac/MeleeUnlocked.app` is an unsigned
x86-64 macOS 15+ bundle. It builds with SDL2 input/audio and the shared GX OpenGL
renderer. **A playable graphics match is not yet demonstrated.** The local
sandbox aborts during AppKit application registration, before SDL can create even
a hidden window. No match screenshots were produced. Do not describe this as a
validated playable build.

## Build and launch

```sh
bash port/mac/build-app.sh
build-mac/MeleeUnlocked.app/Contents/MacOS/MeleeUnlocked --iso <disc>
```

Use a retail NTSC 1.02 disc. The host has no disc picker, settings panel, online
host, or GameCube adapter support. The first SDL GameController maps to port 1;
keyboard input also merges into port 1, even with a controller connected:

- Arrows: main stick; Z/X/C/V: A/B/X/Y; Enter: Start.
- Q/E: L/R; S: Z; I/J/K/L: C-stick.
- Close the window to exit.

The title and bundle display name say “Experimental, not tested yet”. The game
library, Sys directory and shader tools resolve from the executable's
`../Resources`, without relying on the current working directory. Cards default
to `$XDG_DATA_HOME/melee-unlocked/Card A` or `$HOME/.local/share`; shader caches
use `$XDG_CACHE_HOME/melee-unlocked/shaders` or `$HOME/.cache`. `--card-dir`,
`--log-file`, `--state-digest`, `--capture` and `--capture-frames` select explicit
output locations. There are no default writes into the bundle.

The M2 brief explicitly authorizes direct clang builds and hidden/headless tests
of at most about 90 seconds. `build-app.sh` uses serial incremental compilation,
checks at least 15% free memory before every compile/link, and waits 60 seconds
between checks when below that threshold. Every compile/link targets macOS 15.0.
No game library, SDL or shader compiler is built. Inputs must already exist:

- `../deps/sdl2/include/SDL2` and `../deps/sdl2/lib/libSDL2-2.0.0.dylib`.
- `spike/melee_game.so`, `spike/MeleeUnlocked-0.9.52/{melee_game.snapexcl,Sys}`.
- `../deps/tools/bin/{glslangValidator,spirv-cross}` (self-contained universal inputs).

`build.sh` / `run.sh` retain the older shared-queue headless workflow; `build.sh`
also targets macOS 15.0. The current sandbox cannot use that queue, so M2's
explicit direct-build/test exception was used.

## Rendering and packaging

The Mac entry point requests an SDL OpenGL 4.1 core forward-compatible context.
The slim offline host creates its window directly; it does not compile the full
settings/window application from `window_sdl.inl`. On Apple, `renderer_gl.cpp`
uses OpenGL.framework instead of GLEW, converts generated shaders to GLSL 410
with `--no-420pack-extension`, reapplies VSBlock/PSBlock bindings 0/1 and Tex0..7
samplers after linking, and uses framebuffer blits instead of the 4.3 image-copy
call. All remaining GL calls compile against Apple's core `gl3.h`. The authored-frame presenter code is included; Mac presentation stays on the
main thread to avoid the AppKit deadlock. The default display target is 60 FPS. Linux retains its OpenGL 4.5 path and separate shader cache namespace.

Bundle layout:

```text
MeleeUnlocked.app/Contents/
  MacOS/MeleeUnlocked
  Frameworks/libSDL2-2.0.0.dylib
  Info.plist
  Resources/
    melee_game.so
    melee_game.snapexcl
    Sys/
    bin/glslangValidator
    bin/spirv-cross
    EXPERIMENTAL.txt
```

The host's LC_BUILD_VERSION minimum is 15.0. The bundle has zero symlinks and
has not been signed, uploaded or released. SDL is the provided universal dylib.
The shader tools now come from the provided self-contained universal inputs,
with symlinks dereferenced. They depend only on macOS system libraries. Historical
M2 tests below used an earlier copied Homebrew tool whose libraries were missing;
that packaging issue is resolved by the current input set. No library paths or
system settings were modified to make them work.

## Measured evidence and remaining checks

Initial rebuilt headless host, team disc, vanilla parity script, seed
`0x12345678`, time base 0, 9,000 retraces:

- Loop 10.678798 s / 1.186533 ms per retrace; process wall 11.22 s.
- **7,200 match frames × 76 fields, zero differing values** versus Windows.
- Match starts at host frame 1,629 on both; 983 pre-match rows differ.
- `build-mac/{headless.log,mac-digest.raw.csv,mac-digest.csv,compare.txt}`.

The final SDL/graphics-enabled executable was also run headless from the bundle,
without `--game-library` or `MELEE_DSP_COEF`. It successfully found both bundled
resources and completed 9,000 retraces: loop 14.637064 s / 1.626340 ms per
retrace, process wall 17.36 s. Its independent comparison is recorded in
`build-mac/bundle-compare.txt`, with raw/converted `bundle-digest*.csv` and
`bundle-headless.log`. These timings include callbacks, disc I/O and CSV output.

Rosetta acceptance uses `sysctlbyname("sysctl.proc_translated") == 1` on macOS
15+, with the AVX2/FMA/XGETBV check retained for native Intel. In this sandbox the
translated-process sysctl query is refused with `Operation not permitted`, so a
launch without `ROSETTA_ADVERTISE_AVX=1` failed CPU detection. That query was
stopped. The successful headless runs used the brief's authorized Rosetta flag;
normal no-environment launch still needs testing outside this sandbox.

A muted, hidden `--graphics-self-test build-mac/gx-test.ppm` run reached SDL video
initialization, then aborted in HIServices `_RegisterApplication` through
`NSApplication sharedApplication`. `build-mac/graphics.log` contains the host
backtrace. No OpenGL context or image was created. Dependent presenter and match
window tests were stopped; no attempt was made to bypass application registration
or the sandbox. The copied shader tool's missing-dylib failure is separately in
`build-mac/shader-tool.log`.

When an authorized environment can initialize AppKit, run these checks hidden,
muted (`SDL_AUDIODRIVER=dummy`), with HOME and all XDG directories set to throwaway
folders under `build-mac`, a memory check before each run, a process timeout of
at most 90 seconds, and only the team disc:

- `--graphics-self-test <output.ppm>` and `--presenter-self-test` (always hidden).
- `--iso /Volumes/YGWorktrees/build-out/test-discs/GALE01r2.iso --hidden
  --frames 3300 --vanilla-game --time-base 0 --rng-seed 0x12345678
  --script spike/parity_vs.txt --card-dir <scratch-cards>
  --capture-frames <scratch-frames>`.

`--capture-frames` captures match frames 30, 300, 600 and 900 as PPM files; it
requires graphics, and `--hidden` keeps that graphics window invisible. `--headless`
disables graphics entirely. `--capture <file.ppm>` captures the final frame.
Physical controller/keyboard play and audible output remain untested. SDL audio
is compiled in without a new silence stub; headless output is intentionally off,
and tests used the dummy audio driver. Native jukebox support remains disabled
by the existing host; game AX audio is retained.

The existing ELF bridge remains spike-only: it patches exactly 5,928 Linux TLS
canary sites and escapes game stop/panic via setjmp/longjmp. Production requires
a game built without stack protector and a reviewed host/guest stop boundary.
