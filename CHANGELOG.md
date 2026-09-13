# Changelog

## 0.1.6-codex-preview — Codex graphics correctness update

Retains Fable's rendering, authored animation, audio, launcher, updater and
Streamline foundation. Repairs presentation starvation under sustained backlog,
subframe timeline/binding errors, temporal history invalidation, pipeline cache
variants, texture retention, fractional downsampling and output sharpening.

Also repairs launcher ISO fallback/build races, isolated login selection,
truncated updater downloads and several audio lifecycle edge cases. The exact
changes, tests, attribution and outstanding release gates are recorded in
[the Codex review](CODEX_GRAPHICS_REVIEW.md).

The follow-up owns and joins loading workers, fixes shared timing/data races,
rejects incomplete replay comparisons, and adds isolated Dolphin captures plus
a verified contact/shield regression scenario.
It also repairs the eighth texture generator's per-vertex matrix index, with
an image regression and repeated resize/sharpening resource checks.
GPU timing is now available in frame traces. Detailed draw timers are opt-in;
disabling their normal-rendering overhead saved about 0.23 ms of median CPU
submission time in two paired combat trials. Frame-time spikes remain.
Further subframe fixes reject changed matrix bindings, follow per-vertex texture
selectors, hold complete UV transforms at cuts, and preserve texture animation
when publishing a skinned pose.

This is a development preview. Existing Fable releases are preserved.
DLSS quality, Dolphin parity and demanding high-refresh acceptance are still
under validation; this entry is not a completion claim.
