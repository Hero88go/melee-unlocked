# Codex decision: geometry rendering at independent presentation times

Keep the original simulation cadence and Fable's authored renderer. Presentation
must not execute extra gameplay steps or write sampled poses back into guest RAM.

| Method | Decision and tradeoff |
| --- | --- |
| Repeated source images | Correctness baseline; a higher present count does not demonstrate smoother motion. |
| Previous/current pose interpolation | Real geometry frames with known endpoints. Keep as a labeled comparison mode; its timeline trails the latest state. |
| Authored animation sampling | Primary foundation. Sample supported tracks beyond the latest pose without buffering another complete simulation frame. Reject animation/action discontinuities and hitstop. |
| Continuous-motion prediction | Selective supplement for validated objects and cameras. Predictions can overshoot collisions or discrete events; hold unsupported cases. |
| Vertex-stream blending | Retain for compatible rewritten primitives. Changed bindings or discontinuous geometry must hold the entire current draw. Matching vertex counts alone does not prove object continuity. |
| Image interpolation/frame generation | Excluded from the primary gameplay path. It does not establish correct newly rendered poses. |

Fixed simulation and independent rendering are compatible. Known-state
interpolation smooths sampling of fixed updates; variable simulation timesteps
can change physical behavior. See [Glenn Fiedler's fixed-timestep explanation](https://gafferongames.com/post/fix_your_timestep/).
Our preference for forward authored sampling is a project decision driven by
the requirement to avoid an additional buffered simulation frame, not a claim
that prediction is always more accurate than interpolation.

## Rivals of Aether II evidence

Rivals II is the requested feel reference. The official workshop material
describes Unreal assets and animation imports with a 60 Hz sample rate:
[official workshop documentation](https://rivals2.com/workshop/).
An asset import rate does not prove the shipping game's simulation or
presentation algorithm. Further primary-source research on September 13 found:

- Dan Fornace describes custom C++ collision/update logic and confirms SnapNet in
  his [developer interview](https://softwareengineeringdaily.com/podcasts/rivals-of-aether-with-dan-fornace/),
  around 21:29–24:32 in its original transcript. Standard Unreal physics settings
  therefore do not establish Rivals II's gameplay implementation.
- Its hosting provider independently confirms the
  [SnapNet/dedicated-server integration](https://edgegap.com/blog/rivals-of-aether-2-how-is-its-online-experience-is-so-good-netcode-rollback-dedicated-server-orchestration).
- SnapNet explicitly documents [simulation-frame interpolation for high-refresh presentation](https://www.snapnet.dev/docs/core-concepts/simulation-vs-presentation/),
  including the example of 60 Hz simulation and 144 Hz rendering. This is stronger
  evidence than an animation import rate, but it describes SDK behavior rather
  than every shipping Rivals II setting or override.
- [Network prediction](https://www.snapnet.dev/docs/core-concepts/interpolation-vs-prediction/)
  and presentation interpolation are separate decisions. Predicting networked
  gameplay does not imply that visual geometry extrapolates beyond the latest
  locally simulated pose.

Verified: custom gameplay, SnapNet, a simulation/presentation split in that SDK,
and SDK support for interpolation between simulation frames. Still unverified:
Rivals II's exact render-phase offset, per-object interpolation overrides,
skeleton/camera evaluation, input sampling phase and render-queue settings.
Do not claim we have reproduced its exact implementation or measured its latency.
The former blanket assertion that no technical implementation evidence was found
is superseded by these sources; the exact per-object implementation remains open.

For this port, coherent delayed interpolation must be compared seriously with
forward authored sampling. The preview retains the existing forward default;
the eventual choice must account for both motion errors and measured response.
A standard interpolation path can add one physics step of visual delay, while
extrapolation can overshoot and require correction; see
[Unity's explanation of both modes](https://docs.unity3d.com/6000.0/Documentation/Manual/rigidbody-interpolation.html).
Neither is universally best independent of the game's constraints.

### Further SDK detail and the 0.2 implementation decision

SnapNet's [entity and renderer manual](https://www.snapnet.dev/docs/unreal-engine-sdk/manual/entities/)
explicitly separates three operations: interpolation of entity transforms between
fixed ticks, reduction of network prediction errors, and optional renderer
position/rotation smoothing. `MarkTeleported()` suppresses interpolation across
a teleport. Optional renderer smoothing is disabled by default and applies to
non-local predicted entities. A renderer receives its entity update before the
main world's actors tick. These are documented SDK behaviors, not verified
Rivals II configuration values.

The [official Rivals II animation guide](https://rivals2.com/workshop/knowledge-base/character-creation/animation-overview/)
also says that the main character mesh plays clips directly without animation
blending/dynamic postprocessing; separate meshes can carry dynamic bones. That
rules out assuming that every visible component uses one generic pose-blending
algorithm. It does not document the fractional clip time or camera offset.

**0.2 decision:** repair and evaluate the existing `authored-interpolate` path
alongside forward authored sampling. Both render geometry; neither is image
frame generation. This is an implementation of the documented *method family*,
not a claim to copy Rivals II's private settings. Do not switch the default just
because a method sounds more authentic. Compare motion coherence and response
under the same CPU load, queue policy, refresh target and gameplay input.

At source tick `n`, conventional interpolation shows poses between `n-1` and
`n`; forward sampling shows the latest pose plus a bounded fraction of the
next authored/estimated motion. The former has known endpoints but an older
visual timeline. The latter avoids that specific buffered tick but can be wrong
at a stop, collision or animation change. Neither fixes a simulation that cannot
finish its own tick budget. Network prediction of gameplay is a separate axis.

## Method survey for this port

This table compares useful algorithm families rather than every proprietary
implementation. The project decisions below are engineering judgments, not
claims about Rivals II's shipping internals.

| Method | Motion and response tradeoff | Decision |
| --- | --- | --- |
| Hold/repeat the latest 60 Hz pose | Preserves captured geometry; repeated presentations cannot supply intermediate motion. | Keep as correctness and response baseline. |
| Interpolate completed poses | Linear positions and quaternion rotations use known endpoints; curved trajectories may require more information. A previous/current timeline adds visual age. | Implement and validate coherent object, camera and effect timelines; compare with forward mode. |
| Sample authored clips fractionally | Recovers animation curvature from the game's actual tracks. It still needs a validated clock and action identity; it cannot predict a future interrupt. | Retain as the common skeleton/animation foundation in both modes. |
| Extrapolate latest continuous motion | Can reduce visual age; collisions, hitstop, angle wraps and acceleration can produce overshoot/corrections. | Bound the horizon and require observed continuity; hold uncertain cases. |
| Interpolate rewritten vertices | Produces new geometry but vertex count and draw ordinal alone do not identify the same primitive. Atlas changes can represent a new glyph. | Require stable identity/bindings; reject uncertain streams rather than deforming text or effects. |
| Hybrid per-component rendering | Supported poses can advance while unsupported components hold; inconsistent timelines can make a fighter slide against its platform or effects. | Permit only with explicit coverage and synchronized camera/event handling. |
| Higher-rate cosmetic simulation | Cosmetic particles or secondary motion can update independently if they never feed gameplay. | Later, after proving which state is cosmetic and how rollback resets it. |
| Shadow simulation / run-ahead | Repeated save/advance/restore work can hide some internal response delay. It does not itself create correct fractional geometry and adds substantial state/CPU work. | Defer: existing simulation budget, external side effects and rollback fidelity must be solved first. |
| Raise authoritative physics rate or use variable steps | Changes the meaning of Melee's frame-based rules unless a much larger equivalence proof is supplied. | Exclude: preserve original input windows, hitstop, timers, RNG and online timing. |
| Optical-flow / AI frame generation | Synthesizes images without an additional authoritative input result; occlusions, text and fast effects need separate treatment. | Exclude from the primary gameplay path. |
| Late camera reprojection / frame warp | May refresh camera orientation, but cannot reconstruct a new fighter attack, platform collision or concealed geometry. | Not a replacement for character/stage rendering. |

For the fixed-step/accumulator model see
[Fix Your Timestep](https://gafferongames.com/post/fix_your_timestep/).
For save/advance/restore and its performance requirements see the implementation
author's [Libretro run-ahead guide](https://docs.libretro.com/guides/runahead/).

## Presentation delivery is a separate problem

An accurate pose can still feel poor if it waits in a queue or reaches the display
at irregular intervals. VSync, VRR, caps, unlocked operation, frame queue depth,
and when input/state are sampled must be measured separately from pose generation.
[Microsoft's waitable-swap-chain guidance](https://learn.microsoft.com/en-us/windows/uwp/gaming/reduce-latency-with-dxgi-1-3-swap-chains)
describes waiting for presentation capacity before preparing the next frame, with
a latency/throughput tradeoff. The current port uses three resource slots and has
not yet validated a waitable-swap-chain delivery path. Resource-slot count alone
is not a measurement of the display queue.

For 0.2, compare source-rate, forward and interpolated modes using both actual VI
cadence and presentation timing. Report median and tail intervals, source age,
prediction/hold coverage and input-to-visible-response separately. Include a
matched Slippi control before declaring responsiveness improved. Software/GPU
timestamps do not measure the monitor panel or physical button-to-photon latency.

The new Yoshi's Story fixture and main-menu/character-select captures target the
reported background speed and text instability. Long-running animation clocks
must not use a spatial relative-error tolerance: at large frame numbers that
can accept a paused tick as continuous playback. Neither a high FPS counter nor
a changing background passes these tests.

## What counts as passing

Capture consecutive frames with source sequence and sampling phase. Inspect
fighters, skinning, camera motion, shields, attacks, particles and transparent
effects individually; a changing timer or background is insufficient evidence.
Use temporal inspection alongside image differences, because flicker also
produces distinct pixels. Report held, authored, camera-only and vertex-blended
draw coverage independently.

Measure 120/144/165/200/240, monitor and unlocked modes at fixed native settings,
with fresh and warm caches. Submission intervals and source age are software
measurements, not physical scanout or button-to-photon latency. Sustained
four-player and demanding-stage gates remain separate from two-player smoke
benchmarks. A high peak FPS does not pass the smoothness gate.
