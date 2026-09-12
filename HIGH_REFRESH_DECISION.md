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
presentation algorithm. No verified developer description of its exact
high-refresh implementation was found in this review. Community explanations
are insufficient to attribute a particular technique to it. Remove the old
window-title assertion about matching Rivals' physics.

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
