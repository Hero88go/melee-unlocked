# Codex graphics correctness update

Initial reviewed baseline: Fable's `3f09f2c`; subsequent shared-checkout commits
through `b8360b1` are retained. Codex changes use `codex/graphics-correctness`.
This is an implementation record, not a declaration that the full graphics plan
or Dolphin image-quality parity has been achieved.

## Retained

Fable's renderer, recycling pool, ordered EFB processing, immutable texture
snapshots, authored animation, PC settings, Streamline, audio, online services,
launcher and updater remain the foundation.

## Codex changes

- Removed approximate shader substitution after a compile-time budget expires.
  It omitted material behavior and could visibly change a surface when its real
  shader arrived. Shader workers and prewarming remain; an unseen shader now
  waits for correct rendering. Cold-cache compilation can still hitch.
  This is not the demonstrated cause of the reported warm-cache flicker: the
  reported session recorded no fallback draws.
- Cached pipeline ownership distinguishes native and motion-vector variants.
  Switching DLSS no longer permits a cached pipeline with the wrong render targets.
- Draining unpresented frames clears temporal pose history and requests a DLSS
  reset. Motion history requires consecutive frames.
- Sustained backlog cannot select drain mode indefinitely: at most two drained
  frames precede a forced presentation. Drain mode skips subframe pairing work;
  pairing is rebuilt before the next presentation. Queue order and recycling stay.
- Rejected rewritten geometry holds current vertices and matrices together.
  Matrix-binding changes reject blending; leaving authored mode clears the vertex
  override. Held local joints use the interpolation timeline, with discrete cuts
  rejected. Texture-address pairing rules now cover all eight units symmetrically.
- Downsampling uses the actual output-pixel footprint rather than rounding it
  to the sampling tap count, avoiding footprint distortion at fractional ratios.
- Sharpening runs on a separate output-resolution image after scaling or
  reconstruction, blending continuously from zero. Zero bypasses the pass.
- The PC settings panel reports actual render/output dimensions and removes
  unverified DLSS ratios, image-quality promises and the Rivals physics claim.
- Stable texture reuse refreshes the content-cache entry as well as its source
  shortcut; relocated identical content continues to share the retained snapshot.
- Explicit `--user-dir` prevents implicit login discovery outside that folder.
  Normal release launchers opt into discovery with `--discover-launcher-login`.
  Automated runs do not rewrite shared launcher ISO preferences.
- Invalid remembered ISO paths no longer block local ISO discovery. Launcher build
  workers receive a copied path and no longer write UI-owned preferences.
- Path API truncation is checked. Updater HTTP failures and truncated bodies are
  rejected, including size mismatches against release metadata. Failed installer
  writes/process launches do not close the game. Full restart validation remains.
- WASAPI setup failures release event handles, reopened streams reset consumer
  state, and buffered-duration sampling cannot underflow. Device-change recovery
  is still outstanding.
- Added `tools/build_recipes.py`: muted offline collection, checksummed cache
  merging, manifest-driven scenarios and explicit coverage reporting. Default
  scenarios are not an exhaustive character/stage matrix.

## Validation and attribution

Full Release rebuild and 14/14 CTest pass after the principal note fixes, including
new regressions for drain fairness, interpolation endpoints, matrix-binding cuts,
mode transitions, queue-buffer recycling and texture retention. The final full
rebuild including UI/updater changes also passed 14/14.

Native sharpening capture at 1920x1080, EFB 3x, SSAA off, DLSS off, GX source
sequence 1800, prepared offline Mario/Luigi Battlefield fixture:

| Strength | Mean absolute channel difference from zero (0–255) |
| --- | ---: |
| 0.001 | 0.0000 |
| 0.5 | 0.5780 |
| 1.0 | 1.2022 |

These measurements establish slider effect and near-zero continuity, not Dolphin
parity or absence of every visual defect. Separate fresh-card captures exercised
menu sharpening. The old menu-navigation script requires a prepared card to
reliably enter a match; benchmark results without verified scene identity are not
accepted. The repeated 2,400-checkpoint comparison after the subframe changes
passed with zero mismatches across headless, native, threaded and authored match
runs. This checks renderer isolation and is not a Dolphin oracle.

The Codex recipe/sharpening tools and benchmark/isolation changes were initially
included in shared-checkout commit `8090249`. This record preserves their Codex
attribution without rewriting published history. Private correspondence is not
part of the public review.

## Remaining acceptance gates

Reproduce and eliminate remaining graphics defects against matched Dolphin
captures; validate animation discontinuities, rewritten geometry and rollback;
complete genuine high-refresh coverage and sustained frame-time checks; repair
DLSS sizing, motion/depth/camera inputs and HUD composition; finish shader coverage,
updater lifecycle and clean-package tests. Also audit detached loading workers at
shutdown, audio-device recovery, and runtime resize/DLSS transitions.
Rivals of Aether II is the feel reference, but its exact rendering implementation
has not been verified. No image frame generation is part of the primary path.

Private handoffs, user credentials, personal logs and game assets are excluded.
