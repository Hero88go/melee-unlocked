# Fixes merged for 0.9.104

Merge source: `feat/hq-fountain-background` at `540c52e3`. Destination:
`rel/0.9.104`. VERSION stays 0.9.104. This merge is not a release approval.

## Rebuild coordination

Do not build or run game benchmarks during the current FPS benchmark. Its owner
will rebuild the complete release and rerun pixels against 0.9.103, replays,
online and FPS gates afterwards. These post-merge gates have not run here.

The uncommitted `port/runtime/gx/gx_core.cpp` benchmark switch in `melee-0104`
must remain byte-for-byte intact. The six uncommitted preset/shader edits in
`melee-rtx` were preserved when the fixes also merged into `feat/rtx` at
`57a78640`. They are still uncommitted and are not included in this release merge.

Reprepare the isolated native source checkout from the merged
`sourceport/patches/melee-native.patch` before building the game library. Do not
reuse an older prepared checkout merely because the host executable rebuilt.

## Changes and existing evidence

- Offline Source Port L/R cycles installed costumes across the fighter's native
  colors, including original fighters on an m-ex CSS and an extra starting color.
  It updates the native costume and portrait; occupied colors are excluded.
  Online/Teams keep their per-color selection and queue freeze rules. Two real
  game runs exercised ten picks each and loaded an extra Marth model in a match.
- Imported sets stay under their source pack, with a compact numeric set picker.
  Eight synthetic Marth variants retain one pack identity. The final pack-row
  screenshot still needs the new `MELEE_SETTINGS_SCROLL_TO=packs` capture hook.
- Training completes its one-shot destination handoff even when it restarts the
  same mode. The original pro-CPU exit trap reproduced twice on 0.9.103; two
  candidate runs returned to the main menu.
- Optional Minimal pause-screen import includes both supplied styles and its HD
  label. Style names now describe the actual overlay: PAUSE label or no pause
  overlay. Real paused captures passed in Source Port and Static Recomp. No pack
  assets are bundled in the repository.
- Linux `--play [--iso path]` directly launches the saved engine and propagates
  its exit status. Launcher integration coverage passed before the merge.
- Beyond Melee Static Recomp: Sys menu deltas use the explicit retail base,
  rather than applying retail deltas to the mod's differently laid-out archives.
  The original assertion reproduced twice with the official patch; two fixed
  runs reached a match without the bad relocations or assertion.
- Missing/unstartable xdelta tools recover a pinned, hash-verified official tool
  into the user catalog. Missing and invalid executable fault tests passed,
  including actual WinHTTP recovery. The production installer downloaded and
  installed Animelee with a missing preferred tool; all three Diet variants
  built and matched their pinned output hashes.
- Reflex refreshes a queued simulation frame after the driver wait and uses the
  same submit boundary for its render-latency marker with Reflex on or off.
  Both modes ran with the real NVIDIA plugin. Physical input-to-display +5 ms
  was not reproduced; these traces are not a physical latency measurement.
- Costume visibility walks now bound indices by the loaded mesh count and skip
  null entries. Accessory loading records that count. An extracted production
  fixture failed twice before and passed twice after. Full native/game gates
  are still required after this latest change.

Evidence stays outside the game repository under
`bugwatch/runs/since-0103-20261105`. Earlier AVX2/SSE2 Windows and Linux suites
passed before the newest host/native changes. Do not treat those results as
validation of the final RTX merge.

## New reports that must not be described as fixed

The three supplied non-crash bundles have been downloaded and read:

- Nov 5, 22:05 Arizona, Source Port: doubles desync after a rematch (B-857).
- Nov 5, 22:21 Arizona, Static Recomp: L/R actions fail with a GameCube adapter
  (B-858). The adapter eventually opens; the log has no raw presses or binding
  settings to establish a cause.
- Nov 5, 22:35 Arizona, Source Port: doubles desync with online speed options
  enabled (B-859). The log confirms checksum disagreements; it does not prove
  those display/speed options caused them. The existing four-peer rematch
  transport regression passes in isolation, so that alone does not close these.

Another 0.9.103 crash (B-856) resolves to Jigglypuff accessory visibility during
match introduction after a cosmetic override. The new mesh bounds fixture
reproduces the invalid-index failure mechanism. The exact player's costume is
unavailable, so the full player crash has not been reproduced or closed.

Steam Deck Vulkan device-lost (B-843) remains hardware-specific. A hidden Linux
lavapipe run with Khronos validation reached Classic intro (03:01, stage 97),
showed no validation errors, and hit its 180-second software-rendering timeout
at retrace 1385. The reported RADV stage-99 failure was not reproduced. Related
Linux adapter/Vulkan chat reports B-860 through B-864 still need their own
hardware/setup diagnosis. No speculative driver or netplay fix was included.
