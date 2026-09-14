# Architecture and source-port destination

## Current implementation

Melee Unlocked is a native Windows executable whose main gameplay implementation is statically recompiled from the retail PowerPC executable into C++. It retains guest memory/register conventions and interpreter fallback; it is not currently a wholesale native compilation of the decomp C source. Native D3D12 rendering, audio, input, settings and Slippi integration surround that gameplay runtime. Full Slippi compatibility and high-refresh correctness remain validation gates.

The doldecomp Melee project is an important foundation: `port/recomp/symbols.py` reads its symbols, while `tools/generate_fobj_host.py` adapts its animation interpreter source for native authored animation. The reviewed decomp checkout is `039c4bf4ca33338c35d21901ad19b7ede19d19ad`. Upstream: https://github.com/doldecomp/melee . A matching-decomp percentage describes reconstruction of the original target binary, not PC portability. We do not claim this pinned checkout is 100% matching without a matching-build verification.

## Destination and staged migration

The goal is gameplay compiled directly from decomp source, native PC platform systems, verified Slippi compatibility and independently rendered high-refresh animation without changing authoritative gameplay timing.

1. Preserve the working recomp release and pin the decomp baseline. Review any prospective collaborator's source, license, build and known failures before reuse.
2. Establish a host-compatible decomp build, adapting pointer widths, endianness, memory layout, hardware interfaces and floating-point assumptions. Select 32-bit or 64-bit from actual dependency and renderer constraints.
3. Replace translated gameplay subsystems progressively, with explicit boundaries and reference state comparisons. Keep the old implementation as a validation reference until replacements pass.
4. Retain useful D3D12, audio, input, settings, launcher and updater systems. Adapt Slippi hooks and verify rollback/state determinism and stock-client compatibility against the new gameplay implementation.
5. Validate animation and effects at independent presentation rates. Add Vulkan through shared renderer interfaces after D3D12 correctness; additional backends must not delay gameplay verification.

Static recompilation can be faithful; direct source compilation does not automatically guarantee correctness. The latter is our intended architecture because it permits maintaining and extending gameplay at the reconstructed source level. Neither approach establishes a first-ever claim or guarantees latency/performance superiority.

Codex documentation update, 0.1.7. Fable's existing implementation remains the foundation; this hotfix does not perform the gameplay migration.
