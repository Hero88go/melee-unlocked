# Melee Unlocked 0.6.6

## Fixed

- Replay playback now keeps screen shake consistent with Slippi Dolphin. PC-only, host-gated
  Gecko hooks are no longer exposed as executable replay codes; older recordings are denylisted
  for the same hooks during playback.
- Replay viewing supports `--music 0-100` and `--no-music`, so music can be muted without
  affecting game sound effects.
- Launching Melee Unlocked no longer writes direct-code or Teams-code history into the shared
  Slippi Launcher profile. Dolphin/Slippi friend-list and profile data remain isolated.
- D3D12 fallback pipelines are prewarmed and no longer block the presentation thread on driver
  shader compilation, reducing render hitches.
- Fountain of Dreams reflections are restored.
- The Discord release updater now formats and posts the latest release notes reliably, including
  multi-message notes and safe webhook handling.

## Verification

- Native runtime library builds successfully with Visual Studio 2022 x64 Release.
- Gecko replay-layout regression test passes.
- Replay code-list handling preserves normal Slippi codes while terminating before PC-only hooks.
