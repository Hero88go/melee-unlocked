# Changelog

## Unreleased — Codex graphics correctness update

Retains Fable's rendering, authored animation, audio, launcher, updater and
Streamline foundation. Repairs presentation starvation under sustained backlog,
subframe timeline/binding errors, temporal history invalidation, pipeline cache
variants, texture retention, fractional downsampling and output sharpening.

Also repairs launcher ISO fallback/build races, isolated login selection,
truncated updater downloads and several audio lifecycle edge cases. The exact
changes, tests, attribution and outstanding release gates are recorded in
[the Codex review](CODEX_GRAPHICS_REVIEW.md).

This is an unreleased development update. Existing Fable releases are preserved.
DLSS quality, Dolphin parity and demanding high-refresh acceptance are still
under validation; this entry is not a completion claim.
