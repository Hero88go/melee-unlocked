# Native client development

This is a working development executable, not the finished competitive client.
It boots the stock Melee 1.02 executable through static recompilation and renders
native geometry with D3D12. Audio, memory cards, Slippi/netplay, rollback, and
independent sub-frame animation are not implemented. Do not use `--fast` for normal
play: it accelerates simulation.

## Launch

```powershell
.\run-native.bat
.\run-native.bat --scale 3
.\run-native.bat --scale auto --window 2560x1440
.\run-native.bat --threaded-renderer
.\run-native.bat --fps unlocked
.\run-native.bat --fps 240 --frame-mode interpolate
```

`--fps N|unlocked` presents on the render thread's own timeline (uncapped, or capped
at N per second; add `--vsync` to lock to the monitor). The simulation stays at 60 Hz
with unchanged physics. Each presented frame between two simulation frames is
rasterized from re-posed geometry: every draw's position matrices get the fraction of
their per-frame rigid delta (rotation about the true screw axis, slide, scale), so a
240 Hz display shows four distinct native frames per simulation frame. `--frame-mode
extrapolate` (default) adds no latency and continues the last motion; `interpolate`
shows the exact in-between pose one frame late; `off` presents each simulation frame
once. The window title shows the simulation and display rates.

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
back-pressure simulation. It does not yet add intermediate animation frames.

Keyboard: arrows move, I/J/K/L control C-stick, Z/X/C/V map to A/B/X/Y, Enter is
Start, Q/W are L/R, E is Z, and T/F/G/H are D-pad. XInput controller 0 is supported.
Audio is mixed by the emulated AX DSP and played through WinMM; the session starts
muted (`--volume 0`), raise it with `--volume 50`. `--audio-dump out.wav` records the
mix. Closing its own window stops the game;
unrelated Dolphin windows and processes are not touched.

## Build

Use Windows x64, VS 2022 Build Tools with C++, CMake, and Python 3.12 x64.
The generator needs the existing local decomp symbol map and verified stock DOL.

```powershell
python port/recomp/recomp.py
cmake -S . -B build-review -G "Visual Studio 17 2022" -A x64 -DMELEE_BUILD_EXPERIMENTAL_PORT=ON
cmake --build build-review --config Release --parallel 4
ctest --test-dir build-review -C Release --output-on-failure
```

The generator and runtime both reject a non-matching DOL. Generated guest source
and all game data remain local and ignored by Git. Default foundation builds still
exclude the experimental port.

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
