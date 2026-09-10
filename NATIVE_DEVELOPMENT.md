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
.\run-native.bat --threaded-renderer
```

The launcher uses this project's native executable and existing clean ISO. It does
not open or modify the installed Slippi client. `--iso "path"` overrides the local
default. Internal resolution scales actual rasterization; the window starts at
1280x960 and can be resized. `--scale 1` is native GameCube EFB resolution, `2` is
twice each dimension, and so on. The experimental render thread has its own window
and graphics resources, with a bounded ordered queue; a slow renderer can still
back-pressure simulation. It does not yet add intermediate animation frames.

Keyboard: arrows move, I/J/K/L control C-stick, Z/X/C/V map to A/B/X/Y, Enter is
Start, Q/W are L/R, E is Z, and T/F/G/H are D-pad. XInput controller 0 is supported.
No audio is produced by the current port. Closing its own window stops the game;
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
