# Offline pro-ghost development tools

See [PRO_FIDELITY_PLAN.md](PRO_FIDELITY_PLAN.md) for the roster, evidence requirements, training experiments and release gates. These tools do not run during ordinary gameplay. The current deployed model still covers four players; aMSa is the fifth collection candidate.

Dependencies: Python 3.12, NumPy, PyTorch and peppi-py. The legacy trainer expects a CUDA-capable GPU. Jev review uses the HTTP API and the `TYPESAFE_API_KEY` environment variable; no credential is stored here.

Set `MELEE_GHOST_AI_HOME` to the directory holding external datasets, checkpoints and run artifacts. If unset, these files live beside the tools. Do not commit downloaded replays, extracted datasets, credentials or trained checkpoints with source changes.

Example in PowerShell from the repository root:

```powershell
$env:MELEE_GHOST_AI_HOME = 'C:\Users\Chandler\NEW project\ghost-ai'
py -3.12 tools/ghost_ai/test_data_pipeline.py
py -3.12 tools/ghost_ai/collect_verified.py --pro amsa --games 40000 --max-downloads 100
py -3.12 tools/ghost_ai/check_runtime_parity.py --test-exe build-avx2/port/Release/port_ghost_ai_test.exe --binary "$env:MELEE_GHOST_AI_HOME/runs/pro-fidelity-plan/export-check.bin"
```

Collection is bounded by `--max-downloads` and resumable. It only admits raw replays whose exact registered connect code and selected character match. It writes hashes, metadata, identity evidence and a session-grouped split to `data/<pro>/verified/`. Uncertain tournament identities are excluded rather than guessed from character choice. Full collection still needs a bulk implementation to avoid excessive individual requests.

The new training data loader reads verified train/development games and excludes the final test split. Legacy arrays are retained for audit and reference parity, but cannot silently serve as named-pro training truth. The old jointly trained model remains a baseline; the independent pro-policy recipe in the plan is not yet implemented. Do not start a large training run until the corpus audit and pilot are complete.

`export.py` accepts an output path and optional checkpoint path. It derives the number of player embeddings from the checkpoint, checks finite weights, and writes atomically. The native loader checks dimensions, exact file length and finite weights. Model files are float32; quantized export and inference are not yet implemented.
