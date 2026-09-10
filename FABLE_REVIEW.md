# Fable integration review — 2026-09-10

Source: `../melee-unlocked-claude`, branch `claude/port`, commit
`1d3b0ab` (following `998a517`, `3cfdf88`, `998461c`). Its branch and working
files are preserved. The handoff transcript is review context, not a new task list.
The original Codex folder had no top-level Git repository; its existing foundation
was recorded first, then integration continued on `codex/fable-review`.

## Incorporated

- Committed `port/` recompiler, host services, GX decoder and D3D12 renderer as
  an explicitly opt-in experimental target. These are useful scaffolding for
  booting the game and inspecting real native geometry.
- Committed menu/CSS input scripts and `tools/ppm2png.py`.
- Uncommitted `classic_fox.txt` and `pick_fox.txt` as diagnostic input sequences.
  Their names do not establish which fighter they actually select.
- Uncommitted host invalid-read tracing, which aids investigation without
  changing simulation results.
- Existing Codex animation/viewer code, tests, Slippi source and launch settings
  retained. Fable's replacement root CMake and progress document were not copied.

## Findings and disposition

1. **Signed halfword loads were incorrect.** `emit.py` sent `lha`, `lhau`,
   `lhax` and `lhaux` through the unsigned `ld16` expression. For example,
   `0xffff` became 65535 instead of -1. Fixed by explicit sign extension, keeping
   unsigned loads unchanged. The regression test decodes actual instructions,
   compiles their emitted C++, and executes 40 signed/unsigned boundary cases
   across direct/indexed and updating/non-updating forms. This is a concrete
   simulation bug; its relationship to the reported heap faults requires testing.

2. **Texture snapshots are incomplete.** GX captures registers and geometry, but
   D3D12 decodes textures from live guest RAM and the final TMEM state at frame
   submission. A palette or texture overwritten earlier in the frame can be
   wrong. Fable's uncommitted changes hash data at draw capture but still decode
   later data; they also omit mip levels from that content hash. Rejected that
   cache change and its associated GX hash fields. The committed renderer retains
   the known underlying bug; it is not represented as fixed.

3. **EFB invalidation attempt is incomplete.** The uncommitted patch checks a
   capped prefix of destination RAM even though EFB copies aren't written there,
   and reuses cached resources by address without validating the full requested
   texture interpretation. Left it out rather than treating it as a fidelity fix.
   EFB copying/format conversion and guest reads still need work.

4. **Diagnostic thread had data races.** A detached watchdog read the live CPU,
   retrace counter and renderer containers while the main thread modified them.
   Removed this thread from the imported executable; frame logging remains.

5. **High simulation throughput is not unlocked rendering.** `host::retrace`
   skips its sleep in `--fast`; GX submits a frame when the guest copies to XFB,
   and the renderer waits for its GPU work. No independent display timeline,
   sub-frame pose evaluation, Slippi EXI, rollback or multiplayer is implemented.
   No deterministic equivalence claim is accepted from instruction coverage or
   successful boot alone. Audio and memory-card services remain stubbed.

6. **Input/memory validation remains incomplete.** The generator and runtime do
   not enforce a matching DOL digest. Several host copies check a starting pointer
   without validating the entire range; unsupported guest accesses can log and
   return zero. Keep this a local experiment with the already verified stock
   NTSC 1.02 ISO, pending a separate bounds and execution-semantics audit.

7. **Internal resolution is real rasterization scaling.** The committed renderer
   scales EFB color/depth targets, viewport and scissor through `--scale`. This is
   useful native-resolution infrastructure, though copy/texture fidelity still
   needs validation. It is independent of the unresolved high-FPS work.

## Build and validation

Use 64-bit Python and the existing clean game data (generated guest code is local
and ignored):

```powershell
python port/recomp/recomp.py
cmake -S . -B build-review -G "Visual Studio 17 2022" -A x64 -DMELEE_BUILD_EXPERIMENTAL_PORT=ON
cmake --build build-review --config Release --parallel 8
ctest --test-dir build-review -C Release --output-on-failure
```

Default builds keep `MELEE_BUILD_EXPERIMENTAL_PORT=OFF`. The retired image-generation
prototype remains disabled. Native port execution is a development test only;
`--fast` must not be used as evidence of correct high-FPS gameplay.

Local build/test logs and source-preservation hashes are in `reports/fable-review/`.

- Release x64 build passed with VS 2022, including `melee_port` and the existing
  viewer/tests. Two existing duplicate `NOMINMAX` macro warnings remain.
- CTest: 6/6 passed, including 40 executed halfword-load regression cases.
  The retained image-interpolation shader test is historical; its success is not
  acceptance of generated frames for this project.
- Headless Classic script completed 2,400 retraces and exited with code 0 using
  `--time-base 1 --fast --script port/scripts/classic_fox.txt`. No invalid-access
  (`mmio read`/`mmio write`) or fatal diagnostics appeared. This is consistent
  with the signed-load fix helping, but a different clock/scene from Fable's
  capture prevents claiming that all reported heap faults are resolved.
- Extracted source DOL SHA-1 remains the verified stock value
  `08e0bf20134dfcb260699671004527b2d6bb1a45`.
- Fable's HEAD, working-tree status and 65 reviewed file hashes matched the
  pre-review snapshot exactly, including the dirty files and two new scripts.
- This review did not validate the imported renderer visually, benchmark latency,
  compare full game state against Dolphin, or test netplay. Texture corruption,
  independent high-FPS rendering and simulation equivalence remain open.
