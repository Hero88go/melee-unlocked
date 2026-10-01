# Replay validation

`tools/replay_compare.py` runs one recording through either engine and compares
every reference frame and player/follower in eight Slippi post-frame fields:
action state, position X/Y, facing, percent, shield, stocks and internal character
id. Float comparisons use their recorded bits, including signed zero and NaN
payloads. This does not establish equality of the game's complete state.

`tools/replay_batch.py` requires exactly `--count` selected recordings to finish
and pass. Empty or short input selections, low disk skips, missing output,
timeouts, nonzero game/comparison exits, malformed event payloads, reference
frame gaps and missing frames or players fail the gate. A mismatch before frame
zero also fails unless the explicit baseline option below applies. Source can
write a terminal frame after the reference ends; output outside the requested
reference interval is counted separately and is outside this comparison's scope.
Unclosed recordings with a zero raw-block length retain complete available
events up to a cut-off final known event. Its incomplete frame is trimmed;
frame-bookend events also prevent unfinished terminal frames from entering the
reference coverage. Finalized payload truncation and unknown events still fail.
Malformed files considered for the requested corpus fail selection instead of
being silently filtered out.

## Engines and mod discs

`--backend d3d11|d3d12` selects graphics. `--engine source|static` selects execution.
Single comparisons default to Static; batches default to Source for compatibility.
The existing single-comparison `--source` flag remains an alias.

```powershell
py -3.12 tools/replay_batch.py --src recordings/retail --count 20 --out run-source/b5/retail --iso melee.iso --exe build-release-080/port/Release/melee_source.exe --engine source
py -3.12 tools/replay_batch.py --src recordings/akaneia --count 3 --out run-source/b5/akaneia --iso akaneia.iso --mod-base-iso melee.iso --exe build-release-080/port/Release/melee_port_playback.exe --engine static --include-mods
```

Default batches exclude added external character ids. `--include-mods` opts into
selecting them; it does not identify a mod, verify recording provenance or choose
the correct disc. Use a corpus tied to the exact disc/version and code settings
used by the recorder. `--mod-base-iso` explicitly forwards Static's vanilla base
disc. For a single recording, use those engine and disc flags without the batch
selection flags.

Runs use hidden windows, zero volume, disabled music and isolated card, settings,
user and shader-cache directories. `REPLAY_COMPARE_EXTRA` preserves the existing
whitespace-separated extra-argument convention.

## Explicit preframe baseline

The default gate requires exact values. `--allow-preframe-ulp` permits only these
measured original-to-playback float32 Y bit pairs, each exactly one ULP apart:

| Frame | Player | Follower | Original bits | Playback bits |
|---|---|---|---|---|
| -118 | 0 | 0 | `41D51E94` | `41D51E93` |
| -113 | 1 | 0 | `41D51E94` | `41D51E93` |
| -113 | 1 | 0 | `41E056E4` | `41E056E3` |
| -113 | 1 | 0 | `41E07070` | `41E07071` |
| -113 | 1 | 0 | `41E05C00` | `41E05BFF` |
| -108 | 2 | 0 | `41E07070` | `41E07071` |

The -113/-108 cases are recorded in the 0.8.63 handoff and prior retail validation
logs. The -118 case was independently reproduced on the packaged 0.8.62 Source
engine: 14,487 reference frames and 28,974 player-frames, zero coverage or in-play
differences, and exactly two negative-frame differences. They are the -118 and
-113 `41D51E94 -> 41D51E93` and `41E056E4 -> 41E056E3` rows above. Evidence is
`run-source/rel0863/baseline-0862-preframes.log` (SHA-256
`dd46ac84ae9921f2b83171f1929627f16233261635b667c5c8d7029d2f0f83f0`), its
`result.json` (`45261366b21784ee773e3de9f93baf6cccc5af408d9e33f0e743714f9f855c07`)
and retained playback recording
`baseline-0862-preframes/Game_20261001T020259.slp`
(`08639450242dd6d9492e3d0167d2c8480a4e7c8879f76fcf9959aea431628bc8`).

Any other changed field, a larger difference, an unknown value pair, frame
or player, missing coverage, or any in-play difference still fails. Counts for
exact mismatches and permitted baseline differences remain visible separately.
Passing all negative frames
because frame zero onward is equal is not an accepted gate.

## Evidence and reuse

Each comparison writes `result.json`, its console output, process stdout/stderr
and the game log. A batch writes `summary.json` and `summary.txt`, returning zero
only when every requested comparison passes. Comparison exits are 0 for a pass,
1 for a state/coverage failure and 2 for an input or process error.

Batch reuse requires SHA-256 identities for the replay, executable, adjacent
DLLs, native game DLL and available debug/snapshot sidecars, both supplied discs,
comparison tools and the engine's system files, plus engine, graphics,
timeout, baseline policy and relevant environment options. The result artifacts
must also retain their recorded hashes. Legacy `result.txt` files and failed
results are never accepted as cached passes. Same-named recordings in different
directories get distinct case directories. A cache miss creates a fresh attempt,
so previous settings and saves cannot affect it. `--no-cache` forces a new run.

## Fighter core digest v1

`tools/fighter_digest.py` compares explicitly declared scalar fields from the
existing `MELEE_DUMP_FIGHTERS` post-frame records. It emits canonical SHA-256
digests per frame plus the first named field difference, using the exact element
width and one declared endian conversion. Floats remain their original bits;
there is no numerical tolerance or alternate-endian fallback.

```powershell
py -3.12 tools/fighter_digest.py run-source/b5/console.bin run-source/b5/native.bin --schema tools/fieldmap/fighter_core_v1.json --native-debug build-sourceport-gcc/melee_game.dbg --field-map run-source/rel0863/fighter_map-current.json --expected-replay recordings/retail/reference.slp --first 0 --last 120 --out run-source/b5/core-result.json
```

The capture range and expected replay interval must cover exactly the same
frame/player/follower keys and include frame zero. Truncated headers/payloads,
duplicates, unknown ports/followers, unexpected entities, empty records, absent
frame zero, wrong record sizes and player/header identity disagreements fail.
Known bone companions are structurally validated, including their sizes and
parent Fighter record, but bone values are outside core coverage. Extra capture
frames also fail, so set `MELEE_DUMP_FIGHTERS=0:120` for the example above.

The shipped v1 schema compares **fifteen fields**: player index, motion id,
facing, self velocity XYZ, position XYZ, pressed/released buttons, animation
frame, percent, shield health and ledge cooldown. It deliberately excludes
pointers, padding, bitfields, active fighter unions, raw character-kind numbers,
bones, the remaining Fighter fields, RNG, projectiles/articles/items and stage
state. Each exclusion's reason is recorded in the schema and result. A pass
establishes only these fields' equality; it cannot mark a fighter `MU_AK_READY`.

The schema binds both the native debug artifact and generated native field map
by SHA-256. The supplied map must also prove every native field offset, width
and scalar type. Rebuilding or changing the debug artifact requires a reviewed
schema update. This schema expects actual console records of 9,196 bytes and
native records of 11,904 bytes. The current debugger's modeled console size is
9,520 bytes because native unions expanded; that model is not used for console
dump sizing or unverified late union offsets. The fifteen console offsets were
checked against the historical retail layout instead.

The framework accepts reviewed `--extension-schema` files for active fighter
variables. An extension must share the core's provenance and record sizes,
declare explicit console/native kind selectors, and supply typed, nonoverlapping
scalar fields with verified native offsets and separately verified console
offsets. Its selector chooses an active view; it never changes field values.
Different active extension coverage between engines fails. No production
fighter extension ships in this first slice. Extended m-ex storage, identity
mapping, articles and stage capture still need separate schemas and evidence
before full B1/B2 validation.

Independent Dolphin Direct tests, the original TE save comparison and complete
gameplay-state coverage remain separate B5 acceptance work.
