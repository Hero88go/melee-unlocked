# Player-specific Training ghosts

These offline tools build one independent recurrent policy for each named player, used by Fight a
pro in the Practice menu. Each policy trains only on that player's own verified replays: no other
player's games, no anonymous ranked games and no outputs of another AI.

Raw replays, arrays and checkpoints live outside Git. Set `MELEE_GHOST_AI_HOME` to the directory that
holds `data/`, `specialists/` and the run reports. Each accepted replay keeps a source hash, an array
hash, the identity evidence (the player's exact connect code), its split group and its
train/development/test assignment.

For large Lunar Melee bulk downloads, set `MELEE_GHOST_ARCHIVE_HOME` to a temporary archive directory
on a volume with enough free space. Verified arrays still go under `MELEE_GHOST_AI_HOME`; a bulk ZIP is
removed only after its complete identity and extraction pass succeeds.

## Build and verify

From the repository root in PowerShell:

```powershell
$env:MELEE_GHOST_AI_HOME = '<folder with data/ and specialists/>'
py -3.12 tools/ghost_ai/bulk_verified.py --pro mang0 --job <Lunar bulk job id>
py -3.12 tools/ghost_ai/audit_specialist_data.py --pros mang0
py -3.12 tools/ghost_ai/train_specialist.py --pro mang0 --minutes 60 --seed 6301 --output-dir <candidate folder>
py -3.12 tools/ghost_ai/select_specialist_candidates.py --pros mang0 --seeds 6301 6302 6303
py -3.12 tools/ghost_ai/evaluate_sequential.py --pro mang0 --checkpoint <selected model.pt> --split test
py -3.12 tools/ghost_ai/export.py <out>/ghost_ai_mang0.bin <selected model.pt> --quantize --pro mang0
py -3.12 tools/ghost_ai/check_runtime_parity.py --help
py -3.12 tools/ghost_ai/build_release_manifest.py --help
```

The trainer validates every selected game's provenance hash, excludes the final test split, uses one
player embedding row and starts from random weights. Use a unique `--seed` and `--output-dir` for each
candidate. Select candidates on development data and evaluate the chosen checkpoint once on the
untouched test split. Replay likelihood measures action prediction on recorded situations, not live
match strength.

`export.py --quantize --pro <name>` writes the row-wise int8 runtime file. The runtime dequantizes
weights to float32 when loading. Keep the float checkpoint and source manifest for parity and
provenance checks.

The game loads a player's file from beside the executable (`ghost_ai_<name>.bin`). A player whose
file is missing or invalid is not offered in the Practice menu; a named player is never replaced by
another policy. `smoke_preview.py` checks that each policy loads and drives its character in a hidden
Training run. That is a functional check, not a strength test.

Jev reviews ambiguous replay attribution and evidence wording. Its judgments are saved with their
input evidence and never replace replay validation or held-out evaluation. Keep downloaded replays,
generated datasets, credentials and trained model files out of source commits.
