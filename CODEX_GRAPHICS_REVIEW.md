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
- Skinning validates every envelope slot before publishing any sampled matrix.
  A late rejection previously left earlier slots advanced when camera carry did
  nothing, mixing different poses within one draw. The new regression failed on
  the old implementation and passes with the complete-pose commit.
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
- Loading workers now have explicit lifetimes: the Slippi file preloader joins
  at shutdown, and music decoding uses one owned worker with a latest-request
  mailbox. Disc-directory initialization is serialized. Disc counters and the
  published simulation duration are atomic; background I/O no longer races or
  contaminates simulation-thread cost accounting.
- Replay comparisons require all reference frames and complete player/follower
  coverage, include shield health, reject failed processes, and use fresh isolated
  output folders. Previously a truncated replay could pass by comparing only the
  overlapping frames. Added four focused coverage regressions.
- Added an isolated, muted Dolphin replay capture tool with bounded playback,
  recorded settings/hashes and actual PNG dimensions. Both renderers must start
  at the first recorded frame: skipping the countdown changes visual effects.

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

The loading-worker follow-up passed another full Release build, 14/14 CTest,
the same 2,400-checkpoint comparison with zero mismatches, and three isolated
one-retrace exits during background preload (all exited successfully in about
2.3 seconds). These checks do not prove recovery from every device/fatal error.

### Initial refresh sweep

Two-player Mario/Luigi Battlefield, 3x EFB, 1920x1080 window with a 1440x1080
4:3 image, AA/reconstruction/sharpening off. Each cap ran twice for 3,000
retraces, with separate cold and warm caches. Warm-cache CPU submission intervals:

| Target | Median ms | P99 ms | Maximum ms |
| --- | ---: | ---: | ---: |
| 120 | 8.344 | 10.122 | 13.577 |
| 144 | 6.944 | 9.004 | 10.214 |
| 165 | 6.051 | 8.291 | 9.553 |
| 200 | 4.994 | 7.043 | 8.120 |
| 240 | 4.087 | 6.455 | 7.829 |
| Monitor (200 Hz) | 4.994 | 7.041 | 8.564 |
| Unlocked | 3.360 | 6.243 | 9.824 |

These are CPU intervals, not measured scanout or GPU execution times. Background
applications consumed substantial CPU during the sweep and were left unchanged.
The script includes movement and one missed attack, but no landed hits; most
sampled time is idle. No target passes the demanding-match smoothness gate from
these results. A separate combat fixture now verifies landed damage and shield
depletion from the recording, instead of assuming the input script exercised them.

### First matched Dolphin captures

An isolated Slippi playback copy and the native playback build used the same
offline recording, first frame -123, 4:3 camera, no AA and no reconstruction.
At 640x480/1x EFB/1x anisotropy, sampled frames 50, 100, 150, 200, 223 and 240
had mean absolute channel differences of 0.069–0.137 on the 0–255 scale.
At 1440x1080/3x EFB/16x anisotropy, frame 223 measured 0.151. The timer matched
exactly at original resolution; visible residual differences concentrate on
textured platform edges. The 1080p capture setup verifies actual image dimensions;
an earlier window-clipped run was rejected.

These are encouraging baseline comparisons of one scene. They do not establish
parity for all menus/stages/effects, subframe motion, or temporal reconstruction.

A verified combat recording adds two landed hits (Luigi reaches 15% damage) and
shield depletion to 50.27. At matched 1080p settings, 108 consecutive combat
frames (580–687) measured 0.086–0.118 mean absolute channel difference. Impact,
smoke, damage digits, camera movement and fighter poses were compared visually.
Frames after Dolphin's configured replay stop are excluded because its scene
transition no longer matches the port's continuing match. This remains a small
regression sequence, not a comprehensive graphical acceptance suite.

An authored 240-FPS capture rendered multiple distinct fighter poses from source
frame 590 at phases 0.238, 0.587 and 0.880, and across the impact sequence.
The capture performs GPU readbacks and later writes images, causing its own
stalls; it is visual evidence only and cannot establish sustainable FPS.

The Codex recipe/sharpening tools and benchmark/isolation changes were initially
included in shared-checkout commit `8090249`. This record preserves their Codex
attribution without rewriting published history. Private correspondence is not
part of the public review.

## Remaining acceptance gates

Reproduce and eliminate remaining graphics defects against matched Dolphin
captures; validate animation discontinuities, rewritten geometry and rollback;
complete genuine high-refresh coverage and sustained frame-time checks; repair
DLSS sizing, motion/depth/camera inputs and HUD composition; finish shader coverage,
updater lifecycle and clean-package tests. Audio-device recovery and runtime
resize/DLSS transitions remain outstanding.
Rivals of Aether II is the feel reference, but its exact rendering implementation
has not been verified. No image frame generation is part of the primary path.

Private handoffs, user credentials, personal logs and game assets are excluded.
