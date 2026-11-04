# Hit-shake and animation pacing patch

Prepared for the main patch window on 2026-11-04 in `melee-v1`, branch `feat/yougame-1.0`, on top of `3a1a1e02`. The commit is titled **Fix hit shake and reduce sub-frame animation work**. No version bump or publication is included.

## Changes to integrate

1. **Per-model hit/grab shake is no longer predicted as camera velocity.** Melee passes a shifted view to HSD for this shake. Both capture paths now identify that offset relative to the active camera, retain its original 60 Hz motion, and keep it out of the stage-camera vote. Fighter translation still advances normally. A pan that the model offset happens to cancel still samples correctly.
2. **Avoid decoding an animation track twice from its beginning.** The authored sampler validates the current sample as before, then reuses that decoded segment for the displayed time when it is mathematically equivalent. Boundary crossings, looping to another segment, KEY callbacks, inactive/finished tracks and out-of-range clocks use the existing interpreter. The generated host adapter is updated through `tools/generate_fobj_host.py`; do not edit generated `FObjHost.cpp` directly.
3. **Make persistent CPU training overrides visible.** PC settings > Game explains that these overrides affect Stand CPUs and remain active when mod packs are disabled. A Reset CPU training options button clears the eight rows through the existing save/live-update path. Preferences are not reset automatically.

The native pose snapshot layout and host/game API versions are unchanged. The existing quake field gains a model-shift flag; older hosts safely decline that flag. **Rebuild and ship the host and game DLL together** to get the Source Port hit-shake fix. `mu_pose.c` is picked up by the existing CMake shim-source glob. The decoder optimization is host-side and benefits both engines' authored presentation.

## Validation

- Release SSE2 Source Port host and GCC SSE2 native game DLL build successfully.
- CTest: **5/5** passed: `packed_animation`, `port_subframe`, `port_projection_2d`, `port_render_observer`, `port_native_pose_bridge`.
- Retail Fox/Marth/Falcon curve corpus: **4,779,892** cursor samples match fresh decoding bit for bit over **656 animations**, plus synthetic boundaries, reversed times, negative starts, large clocks and another-track fallback.
- Actual playback before/after: **2,397 frames / 4,794 player frames**, zero mismatches in the replay post-frame fields. This is not a complete-state checksum or live online compatibility test.
- Hit-shake regression failed before the fix and passes after it. Tests cover normal knockback continuing under shake, shake ending, pan cancellation, stage-camera isolation and stationary HUD behavior.

Local commands:

```powershell
cmake --build build-sse2 --config Release --target melee_source packed_animation_test port_subframe_test port_render_observer_test port_projection_2d_test port_native_pose_bridge_test --parallel 4
ctest --test-dir build-sse2 -C Release -R '^(packed_animation|port_(render_observer|subframe|native_pose_bridge|projection_2d))$' --output-on-failure
```

The optional retail corpus invocation is `build-sse2/Release/packed_animation_test.exe reports/therajah-20261104/animation-assets`. Those archives were extracted locally from the owner's ISO and are intentionally excluded from this commit.

## Measured performance and limits

Same replay, D3D12, internal resolution x3, 120 FPS authored presentation with Reflex, warm shader cache, 2,400 retraces, first 600 simulation frames excluded. The process was limited to four logical CPUs on an RTX 5070 / 200 Hz desktop; this is not the reporter's GTX 1650 / 120 Hz machine.

| Metric | Before (repeat baseline) | Track-cursor patch |
|---|---:|---:|
| Mean smoothing work | 1.128 ms | 0.880 ms |
| Smoothing p99 | 3.356 ms | 2.264 ms |
| Present-return interval p99 | 11.735 ms | 11.375 ms |

This removes about **22% of mean smoothing work and 33% at p99** in this comparison. Overall frame pacing improves much less. Do not describe it as a verified cure for the reporter's lag or a guarantee of stable 120 FPS. His exact video symptom has not been reproduced in a controlled gameplay comparison.

The supplied player logs are treated as his at the owner's request, but attribution is not independently established. They show random DI and SDI against the hit, plus random tech/getup, active even with cosmetics disabled. Those overrides apply to regular offline CPU matches as well as Training; they are disabled online and cannot explain his online claim.

The earlier shared-camera-cache optimization was discarded after inconclusive pacing results. Turning Reflex off also did not establish a pacing fix. Neither experiment changes the final code.

## Main patch window

- Include this commit directly if publishing from this branch, or cherry-pick it onto the release branch. The final chat message supplies its hash.
- Rebuild both AVX2 and SSE2 release host/game pairs using the normal release process. Only the SSE2 pair was built in this task; release packaging and live cross-play checks remain with the patch window.
- Keep release wording specific: **Reduced CPU work in Predict ahead animation; fixed exaggerated hit/grab shake; added a reset for CPU training options.**
- Raw timing CSVs, logs, replay comparisons and the fuller investigation remain locally under `reports/therajah-20261104/`. No player logs, replays, retail animation archives or compiled binaries are included in the commit.
