# Native client development

This is a working development executable, not the finished competitive client.
It boots the stock Melee 1.02 executable through static recompilation and renders
native geometry with D3D12. Approximate AX audio and experimental transform-based
sub-frame rendering are implemented. Memory cards, Slippi/netplay, rollback, and
authored fractional-time animation remain unfinished. Do not use `--fast` for normal
play: it accelerates simulation.

## Launch

```powershell
.\run-native.bat
.\run-native.bat --scale 3
.\run-native.bat --scale auto --window 2560x1440
.\run-native.bat --threaded-renderer
.\run-native.bat --fps unlocked --frame-mode extrapolate
.\run-native.bat --fps 240 --frame-mode interpolate
```

`--fps N|unlocked` requires an explicit experimental `--frame-mode authored`,
`--frame-mode interpolate` or `--frame-mode extrapolate`. It presents on the render
thread's timeline; add `--vsync` to synchronize presentation to the monitor.
Simulation targets 60 Hz and is never modified by presentation.

`authored` is the native path. Recompiled `HSD_JObjAlloc`/`JObjRelease` give every
joint a stable generation, `HSD_JObjDisp` and the rigid matrix setup pair each draw
with its joint, and the capture reads the joint chain's AObj/FObj tracks from guest
RAM. The render thread re-samples those packed tracks at `frame + phase * rate`
for each display deadline, rebuilds the chain's world matrix and applies the delta
to the captured draw (chain samples are cached per draw pair). Draws the authored
path declines (envelope-skinned and shared-vertex draws, looping or paused
animations, animated scale, static joints) fall back to the geometric estimate of
the selected mode; `interpolate` delays that estimate by one simulation frame,
`extrapolate` predicts it. Cuts, changing geometry and orthographic HUD draws keep
the current pose. Normal launch uses `off`, which presents each simulation frame
once. The threaded backend logs pairing rejections, phase histograms, authored
capture/sample counters and per-frame solver/submit cost every 5 s.

Rendering keeps three frames in flight with per-slot upload rings and descriptor
recycling. Compiled shader blobs and the D3D12 pipeline library persist under
`--shader-cache DIR` (default `shadercache/`, ignored by Git); the first match after
a clean cache stutters while pipelines compile, later runs do not. Measured on the
RTX 5070 with a warm cache: menus hold 240 fps, a match runs about 126-135 fps.

The launcher uses this project's native executable and existing clean ISO. It does
not open or modify the installed Slippi client. `--iso "path"` overrides the local
default. Internal resolution scales actual rasterization, the same way Dolphin's
"Internal Resolution" does: the EFB colour/depth targets are allocated at
640x528 times the multiplier, every viewport, scissor rectangle, clear and EFB copy
is scaled by the same factor, and the game's geometry is rasterized directly at that
size. Nothing is upscaled from a 640x480 image; textures stay at their authored
size and are simply sampled by more pixels. `--scale 1` is native GameCube EFB
resolution, `2` is twice each dimension (up to 25), and `auto` picks the smallest
integer multiplier that covers the window's 4:3 area (Dolphin's "Auto (Window
Size)") and re-allocates the EFB when the window is resized. `--window WxH` sets the
initial client size (default 1280x960). The experimental render thread has its own window
and graphics resources, with a bounded ordered queue; a slow renderer can still
back-pressure simulation. Every source frame is processed to preserve EFB copies.

GameCube controllers: a WUP-028 adapter (official or Mayflash in Wii U mode) with the
WinUSB driver that Slippi's setup installs through Zadig is opened automatically; its
four ports map to game ports 1-4 with stick/trigger origins taken when a controller is
plugged in, and rumble follows the game. Close Slippi Dolphin first: the adapter can be
held by only one program. Without an adapter the keyboard and XInput drive port 1.

Keyboard: arrows move, I/J/K/L control C-stick, Z/X/C/V map to A/B/X/Y, Enter is
Start, Q/W are L/R, E is Z, and T/F/G/H are D-pad. XInput controller 0 is supported.
Audio uses an approximate AX DSP HLE mixer and WinMM output; the session starts
muted (`--volume 0`), raise it with `--volume 50`. `--audio-dump out.wav` records the
unscaled mix. Volume applies only to this client's output samples. DSP resampling
and command coverage still need fidelity work. Closing its own window stops the game;
unrelated Dolphin windows and processes are not touched.

## Build

Use Windows x64, VS 2022 Build Tools with C++, CMake, and Python 3.12 x64.
The generator needs the existing local decomp symbol map and verified stock DOL.

```powershell
python port/recomp/recomp.py --gct-base 0x8065CC80
cmake -S . -B build-review -G "Visual Studio 17 2022" -A x64 -DMELEE_BUILD_EXPERIMENTAL_PORT=ON
cmake --build build-review --config Release --parallel 4
ctest --test-dir build-review -C Release --output-on-failure
```

The generator and runtime both reject a non-matching DOL. Generated guest source
and all game data remain local and ignored by Git. Default foundation builds still
exclude the experimental port.

The generator applies Slippi's code tables (`slippi/Data/Sys/bootloader.gct` plus
the enabled codes of `GameSettings/GALE01r2.ini`, assembled exactly like Dolphin's
`GenerateGct`) before translation: memory writes patch the image, C2 caves are
spliced in place of the hooked instruction, hooks outside any function and C0 caves
become synthetic functions, and every cave instruction, hook address and hook+4 gets
a dispatch-table thunk so Slippi's helper-table trick (`bl x; x: blrl`, then a
computed `bctrl` into a table of branches) and function pointers into cave code
resolve at run time. `--gct-base` is the address the game reports when it loads the
main table over the EXI device (`slippi: game loads the GCT ... at ADDR` in the log);
pass `--no-slippi` for a vanilla build. `--sys-dir` and `--replay-dir` point the EXI
device at the Slippi Sys folder (game files, VCDIFF patches) and the .slp output.

Slippi Online is ported: `--user-dir DIR` names the folder holding the Launcher's
`user.json` (default `runtime/slippi/User/Slippi`, the isolated copy), `--online-delay N`
sets the input delay (default 2), `--chat on|direct|off` the quick chat setting and
`--netplay-port N` fixes the UDP port. Matchmaking talks to mm.slippi.gg exactly like
Dolphin (create/get ticket over ENet, JSON), the netplay client exchanges pads/acks/
selections/chat on the same wire format, and rollbacks use Dolphin's savestate regions.
`--local-peer idx:port:ip:port` on two instances peers them without the server for
local rollback tests (`port/scripts/online_unranked.txt` drives a full match). Game
reports and rank fetching are logged, not sent, so ranked play is incomplete.

## Reproducible validation

```powershell
python tools/validate_native.py --iso "path-to-clean-1.02.iso"
```

This runs the same 2,400-retrace Classic input sequence at a fixed clock seed in
headless, synchronous graphics, and threaded graphics modes. It compares CPU
registers, full main RAM, full audio RAM and selected event counters at every
checkpoint. Reports go to `reports/native-validation/`. This tests renderer
isolation; it is not a Dolphin comparison or complete serialization of host HLE
queues, callbacks and state for rollback.

For silent GPU captures without a visible window:

```powershell
build-review/port/Release/melee_port.exe --iso "path-to-clean-1.02.iso" --hidden --frames 2400 --fast --time-base 1 --script port/scripts/classic_fox.txt --capture reports/native-validation/classic.ppm --capture-every 600
python tools/ppm2png.py reports/native-validation/classic_01800.ppm reports/native-validation/classic_01800.png
```

## Next acceptance gates

1. Correct remaining EFB copy/format/guest-memory semantics and bound GPU caches.
2. Capture stable scene/object generations and authored animation state; render
   real fractional-time poses without modifying simulation memory or interpolating
   images. Remove cross-frame resource dependencies before dropping queued views.
3. Validate full simulation behavior against stock Dolphin using identical input,
   time and game state. Resolve unsupported instructions, timing and HLE behavior.
4. Implement complete host/guest snapshot ownership, restoration and resimulation,
   then integrate Slippi protocol and test mixed-rate peers and forced rollback.
5. Finish audio, input/device configuration, persistence and packaging. Add the
   requested optional graphics integrations after the core fidelity/latency gates.

No completed high-FPS or online compatibility claim is supported yet.
