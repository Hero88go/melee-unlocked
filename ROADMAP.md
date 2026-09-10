# Project order and requirements

Direction correction: require actual geometry rendering at independent FPS. No image
interpolation or generated-frame solution. The earlier companion presenter is retired.
Build host-side authored animation evaluation and model rendering first, investigating
a Slippi simulation bridge. If that bridge is impractical, continue the native app.
Historical experimental steps below are not acceptance criteria or the active solution.

## 1. Slippi-compatible unlocked presentation — active

Keep vanilla Melee NTSC-U 1.02 simulation, input sampling and Slippi rollback/network
behavior. Target independently selectable 120, 144, 165, 240 FPS and uncapped output.
Keep the existing installation/data intact; use the verified backup and isolated runtime.

First visual experiment: an opt-in D3D11 companion presentation window, consuming shared
GPU copies and estimating motion between source images. It is an experiment to measure
presentation independence and visual limits, not the final geometry-fidelity solution.
Expect interpolation artifacts and approximately one simulation frame of visual delay.
Native geometry interpolation remains the intended fidelity target. Validate actual
distinct images, authoritative game states, mismatched peer rates, rollback and latency.

## 2. Independent native application — long-term objective

Proceed after evaluating the Slippi version, especially if its rendering is too limited.
Use doldecomp/melee as the behavioral reference/base, replacing GameCube platform services
and making rendering independent of the fixed simulation. The existing decomp builds a
PowerPC GameCube executable; it is not a portable PC engine yet. Reuse applicable Slippi
work, but do not assume emulator memory-snapshot rollback works unmodified in native code.
Define and test deterministic state ownership, floating-point behavior, input timelines,
serialization, rollback and resimulation before claiming equivalent stability.

### Native graphics requirements requested by the user

- NVIDIA Reflex integration for latency reduction, with supported modes exposed only
  when available and measurement markers tied to the correct simulation/render frames.
- Optional RTX ray tracing, interpreted as ray-traced lighting/shadows/reflections in
  the native application. Evaluate these effects separately for Melee's visual style,
  readability and latency budget. Retain the original-style rasterized path, expose
  quality/capability controls, and keep all lighting results out of simulation/rollback
  state. Ray reconstruction can be evaluated later if the chosen effects warrant it.
- Optional DLSS Super Resolution on supported RTX hardware. Preserve a complete native
  rendering path for unsupported GPUs. DLSS may help when GPU/pixel-bound; do not promise
  that it accelerates CPU-bound simulation or that all low-end systems support it.
- Configurable conventional antialiasing when DLSS is off. When DLSS is enabled, disable
  the separate conventional AA selector and let DLSS perform reconstruction/antialiasing.
  Restore the user's previous AA choice when DLSS is disabled.
- Separate sharpness slider, with a suitable post-reconstruction sharpening pass if the
  selected SDK does not expose sharpening. Sharpness controls edge/detail emphasis, not
  antialiasing sample count or quality. Do not present it as an "alias level" control.
- Consider DLAA as an optional quality mode. DLSS frame generation is a separate later
  evaluation, not implied by Super Resolution or a substitute for actual high-rate rendering.
- Supply proper motion vectors, depth, camera jitter and exposure, reset temporal history
  on rollback/camera cuts, and compose HUD after reconstruction/sharpening.
- Use runtime capability checks and benchmark image quality, actual throughput and latency.
  No NVIDIA feature is required for playing or for deterministic networking.

Primary references checked September 9, 2026:

- [NVIDIA Streamline](https://developer.nvidia.com/rtx/streamline)
- [DLSS integration guide](https://raw.githubusercontent.com/NVIDIA/DLSS/main/doc/DLSS_Programming_Guide_Release.pdf)
- [NVIDIA integration overview](https://developer.nvidia.com/blog/how-to-integrate-nvidia-dlss-4-into-your-game-with-nvidia-streamline/)

No native application, Reflex, DLSS or RTX ray-tracing integration has been implemented yet.
