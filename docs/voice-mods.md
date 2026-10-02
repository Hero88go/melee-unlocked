# Voice mods per skin

One zip per mod: the costume, its pictures and the fighter's sounds travel together, and the
sounds are heard only while that skin is the one worn. Both engines use the same catalog code
(`port/runtime/host/cosmetic_mods.cpp`) and the same rules.

## How the game keeps fighter sounds

- `audio/us/<bank>.ssm` (English) and `audio/<bank>.ssm` (Japanese) hold the samples of one
  fighter: `fox.ssm`, `falco.ssm`, `captain.ssm`, `mars.ssm` (Marth), `emblem.ssm` (Roy), `zs.ssm`
  (Zelda and Sheik), `ice.ssm` (both Ice Climbers), and so on. 25 fighter banks.
- `smash2.sem` is the table of sound effects. Each one plays a sound of a bank by number. A voice
  mod never replaces it.
- A bank file is: four big-endian words (size of the sound table, size of the sample data, number
  of sounds, id of the first sound), the sound table (per sound: channel count, sample rate, then
  0x40 bytes per channel with the ADPCM addresses and coefficients), padding to 32 bytes, the
  ADPCM samples. The English and Japanese banks of one fighter differ (Fox: 50 sounds from id 516,
  or 54 from id 519).
- The game reads a bank in three reads (0x20 bytes, the sound table, the samples) and keeps it in
  audio memory across matches. It budgets each bank's samples from a fixed table
  (`fox.ssm`: 573,216 bytes).

## What a mod zip may contain

| File | Meaning |
|---|---|
| one or more costume files (`.dat`, `.usd`) | the skin, one per costume slot; the file itself says which slot |
| `*.png` | portrait (`csp`, `portrait` in the name) and stock icon (`stock` in the name) |
| one `*.ssm` | the fighter's replacement sound bank; any file name |
| `mod.json` (optional) | `{"name": "Wolf", "slot": "Fox Green", "voice": "sounds/wolf.ssm"}` |

- `name` is the skin's display name. `slot` (a costume by fighter and color, or by file code
  `PlFxGr`) is only needed when the zip has no costume file. `voice` picks one bank when the zip
  holds several. With a costume file in the zip, the costume file decides the slot.
- A zip with several costumes of one fighter and one bank gives every one of them the bank.
- A bank alone (`.ssm`, or a zip with only a bank) is attached to an installed skin: the costume
  named by `mod.json` or by the file name (`Fox Green.ssm`), else the only selected skin of that
  fighter, else the only installed one. With no skin to attach to, it is refused with advice.
- The bank is recognised by its own header (sound count and first id), not by its name.

## Validation (import, and again at every launch against the disc)

A bank is refused, with the reason, unless all of these hold:

1. The header is consistent: the sound table ends where the header says, the samples are inside
   the file, every sound has 1 or 2 channels, every channel is ADPCM, and each start, loop and end
   address lies inside the sample data.
2. It has the same number of sounds and the same first sound id as the game's bank (English or
   Japanese), so every sound number keeps meaning the same thing.
3. Every sound has the same channel count as the game's sound, so the bank takes the same room in
   the audio heap.
4. Its sample data is no larger than the room the game reserves for that bank.
5. The bank belongs to the skin's fighter.

At launch each bank is compared with the disc's own files. It replaces the language file it fits
(usually one of the two) and the other language keeps the disc's sounds, with one log line.

## How a bank is tied to a skin

- Catalog: the skin's record gets a `voice` entry (`stored_path`, `sha256`, `source_member`,
  `bank`); the file is `CosmeticMods/assets/<skin id>/companions/voice.ssm`. An older build
  ignores the entry and still lists the skin.
- The bank is served in place of the disc file through the same file override path as the
  costume (`apply_to_fst` and `read`). The file table carries one length for the bank file for the
  whole session (the longest of the disc's and the installed banks, the rest padded), so its entry
  never moves.
- The choice is made when a match is about to load, from the fighters of that match: for each
  port, the skin selected for that fighter's costume slot. The Ice Climbers also count Nana's
  skin, and Zelda and Sheik each other's of the same color.
- Changing the skin on the character select screen with L / R needs nothing more: the next
  match load reads the selection again.

## Two costumes of one fighter with different voices

The game loads one bank per fighter, so two Foxes cannot have two voices. Rule: **the lowest
port that plays the fighter decides the bank**, also when its choice is the game's own sounds.
Every other port whose skin asks for something else gets one log line
(`cosmetics: voice: port 3's ... is not used: port 1 decides fox.ssm`). The rule depends only on
the match's ports and the saved selection, so it gives the same result every time.

## When a changed bank takes effect

Only at a match load, never during a match. Each engine tells the catalog the fighters of the
match that is about to load (`plan_match_voices`); the catalog swaps the served banks and names
the ones whose content changed. The engine drops those from the game's audio memory (the game's
own `HSD_SynthSFXGroupDataRemove`, then the bank is marked not loaded), and the game's loader reads
them again in that same load.

- Static Recomp: hooks on the three fight scene entries (VS, Sudden Death, Training) read the
  ports from the match's start data (`port/runtime/hle/hle_pad.cpp`).
- Source Port: `lbAudioAx_8002785C` asks the host with command `0xFA` before it requests the banks
  (`lb/lbaudio_ax.c`, `port/app/source_host.cpp`).
- A load already in flight is cancelled and repeated by the game's loader itself (it does that at
  every request), so a bank is never half one voice and half another.
- Screens that play fighter sounds without a fight (Sound Test, trophies) use the last match's
  choice.

## Online and replays

Sound data only. The game asks for the same sounds at the same frames; only the samples served
differ, and sounds are not part of the rollback checksum. Voice banks therefore stay on online
and in replays, on both engines, and nothing is sent to the other player. One exception follows
the skin: a skin that is shown as the standard costume online (its skeleton differs) also keeps
the game's own voice online.

## Mods tab and command line

- A skin with a bank is listed as `name (voice)`, with a `Voice: fox.ssm` line and a
  `Remove voice` button under its details.
- `--import-cosmetic <zip or .ssm>` imports, `--cosmetic-status` prints `| voice fox.ssm` on the
  skin's line.
