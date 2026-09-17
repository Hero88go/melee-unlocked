# Melee Unlocked 0.3.61

## Fixed

- The master Volume setting now scales music as well as game audio in both Windows audio output paths. It no longer behaves as an on/off switch for music.

## Removed

- Tap jump off was withdrawn. The previous implementation altered the reported stick position and could change diagonal input angles used for DI. It will return only as a game-side decision that does not distort controller input.

## Verification

- `melee_port.exe --version` reports `0.3.61`.
- The release archive contains the rebuilt game and launcher.
