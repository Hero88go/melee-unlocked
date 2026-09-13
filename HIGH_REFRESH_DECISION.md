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
