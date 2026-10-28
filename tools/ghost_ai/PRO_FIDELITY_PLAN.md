# Pro ghost fidelity and release plan

Owner direction, 2026-10-28: prioritize faithful imitation of each pro. Choose the fifth player by the largest usable, confidently attributed replay collection among established top-player candidates. Use Jev where semantic judgments help. Keep ranked and pro data separate. Do not release an unfinished approximation as a carbon copy.

Roster update: the owner excluded Ginger for insufficient popularity. Use aMSa as the fifth collection candidate, with Yoshi. The surveyed `AMSA#0` profile lists 15,552 Yoshi games; these are availability counts, not downloaded or identity-audited training games. Existing four-player model indices remain unchanged until a new five-player model and runtime are validated together.

## What success means

The bot should reproduce the player's conditional choices, timing, execution, spacing, risk preferences, adaptations and characteristic mistakes across supported matchups and stages. Being stronger than a player is a different objective. Winning more does not establish a faithful imitation.

An exact continuation can be replayed until the opponent deviates. Interactive play needs a learned distribution over plausible next inputs and sequences. Multiple good choices can be consistent with the same pro. We will measure behavioral fidelity on unseen sessions and live interactions instead of promising identical decisions in every possible situation.

## Verified starting point

- The current model is one shared four-player recurrent imitation network. `model.pt` has a four-row player embedding. The existing float32 runtime file is 6,297,068 bytes. No verified Phillip distillation or int8 runtime is present.
- Current extracted games: Mang0 9,172; Zain 9,559; Hungrybox 1,227; Cody Schwab 9,582. Total 29,540, rather than 20,000 to 40,000 per player.
- The existing habit profiles' 10,000-game counts describe a different stats collection. They must not be presented as the neural model's training counts.
- `train.py` currently declares an extra general embedding, while existing checkpoints have four. Export currently declares four rows regardless of checkpoint shape. Future training/export needs one consistent, versioned schema.
- Extraction can infer a player from their character when connect codes are absent. Character uniqueness alone is insufficient proof of identity. Existing files lack enough persisted identity evidence to certify every attribution without revisiting source metadata.
- The general-ranked stream has rate-limit messages; there is no verified completed general-training stage. No fifth-player corpus is present.
- Practice branch `feat/practice-0962` now includes fetched `origin/main` at `285f01ec` via merge `c2ed6787`, including the recent 0.9.63/0.9.64 hotfixes. The merged build still needs verification.
- Earlier 111-test success predates this merge and is not evidence that the current candidate passes.

## 1. Build trusted, separate replay collections

1. Create a manifest per replay: source replay ID, source and extraction version, content hash, verified player and port, identity evidence, character, opponent, stage, date, event/session, duration, validity and exclusion reason. Keep raw metadata so decisions can be audited.
2. Accept exact verified connect codes or tournament participant-to-port evidence. Exclude unresolved same-character games, ambiguous aliases, doubles, CPU games, corrupt/missing-frame games, unsupported versions and handwarmers. Jev may triage ambiguous metadata for review; its guess cannot establish identity.
3. Deduplicate by replay content and shared recording identity across sources. Keep duplicate recordings, tournament sets and consecutive session games within one split. Fit transforms and sampling policies on the training split only.
4. Survey top-player candidates such as aMSa, Jmook, moky and Trif. Count eligible unique games and frames on the supported main character, not search hits. Select the largest verified usable collection and record the comparison. Do not invent the result before inspecting it.
5. Grow toward 20,000 to 40,000 unique games per player where available. Hungrybox is currently much sparser. Do not repeat or relabel games to meet a quota. Stop collection when the source is exhausted; report shortfalls.
6. Keep `data/<pro>/` exclusively that pro's demonstrations, `general/` exclusively anonymous ranked data, `teacher/` exclusively generated teacher targets, and `selfplay/` exclusively simulated games. Store dataset hashes in every checkpoint.

Deliverable: an identity-audited manifest, counts by player/matchup/stage, exclusions, immutable train/development/test splits, and a documented fifth-player choice.

## 2. Measure situation coverage

Represent positions and relative spacing, speeds, facing, action and animation phase, percent, shield, stocks, jumps, hitlag/hitstun, stage/platform state, recovery resources and recent actions of both players. Audit live-versus-replay feature parity before expanding the model. Determine which extra context the model can observe from replays and from the native game without leakage.

Measure coverage separately for neutral, approach, defense, punish, tech chase, ledge, recovery and last-stock decisions, conditioned on matchup, stage and damage bands. Consecutive frames are highly correlated; frame count alone is not independent evidence. Use sequence retrieval from the same pro as a baseline and as evidence for similar situations. Retrieve development/training examples only, never the held-out test collection.

Deliverable: a coverage report identifying missing situations. Direct new collection and training toward those gaps rather than downloading redundant games indefinitely.

## 3. Learn each player's decisions without blending identities

Compare a strictly pro-only policy per player with a frozen shared representation plus player-specific policy/adapters. In the latter, freeze any general base before style specialization; update each player's policy only with that player's demonstrations. A player embedding alone does not guarantee separation in a jointly updated shared policy.

Train on sequences with both players' recent actions and enough history to learn responses and adaptations. Predict joint controller actions and their duration, or otherwise model dependencies between stick, buttons and timing. The current independent heads can produce combinations that were individually likely but never used together. Preserve the distribution of valid alternatives rather than selecting the most common move every time.

Balance important rare situations without silently replacing the real frequency of a player's choices. Keep a direct imitation-only baseline. Winning-moment weighting is an experiment: discard it if it removes the player's actual risk preferences or mistakes. Report both prediction quality and complete-sequence behavior.

Deliverable: separately attributable pro policies, training manifests, held-out conditional action likelihood/calibration, and same-character comparisons that prevent character identity from masquerading as style.

## 4. Use prior work and Phillip with an explicit style budget

Verify the current upstream code/model licenses and observation/action contracts before incorporating assets. Credit and preserve required notices for anything used. Pin teacher model versions and hashes. Do not assume a published character or matchup model is a dedicated clone of any named pro.

Reuse tested infrastructure and representation ideas where useful. Compare pro-only imitation against optional general pretraining, teacher distillation and self-play as separate experiments. Each alternative must improve robustness while preserving the pro-only baseline's measured style. Do not merge teacher or ranked targets into a named pro's demonstration collection.

If a teacher is used, restrict its weight to evidence-poor or broken situations and record that intervention. A teacher's policy can change style even when queried on a pro's replay states. Measure the change rather than assuming distillation transfers only strength. Reject teacher/self-play updates that exceed the style tolerance on development data; leave the final test set untouched until candidate selection.

Deliverable: an ablation table showing provenance, style, robustness and strength for each stage, with a named-pro model selected for fidelity.

## 5. Jev-assisted analysis and review

Use TypeSafe's current documented API to obtain narrow typed judgments over structured replay evidence and short event summaries. Keep the engine and exact measurements in code. Proposed offline jobs:

- Classify an interaction as approach, retreat, bait, defensive reset, punish or insufficient evidence using supplied mechanics definitions and neighboring frames.
- Compare a retrieved set of same-pro examples with a bot continuation and score one behavioral dimension at a time: spacing, option choice, timing, risk or adaptation.
- Triage ambiguous metadata and contradictory labels; never admit a replay on a semantic identity guess alone.
- Rank recurring failure clusters for review and check whether a proposed repair contradicts examples of the pro's actual behavior.
- Audit training/release claims against the actual manifest and measured results.

First calibrate these tasks on a manually reviewed, development-only labeled set, including counterexamples and an insufficient-evidence outcome. Retain prompts, model version, answers, probabilities, tokens and evidence hashes. Cache judgments and batch independent questions. Measure error rates before using labels as training supervision. Do not treat a high model score as proof of identity, gameplay skill or release readiness.

Jev runs in the offline data/evaluation pipeline, outside the gameplay input loop. The shipped local bot must not require an API connection. No API calls or costs are incurred by ordinary base-game play.

Deliverable: a small audited labeling pilot and measured agreement with human/engine ground truth before scaling.

## 6. Prove fidelity, reliability and strength separately

Pre-register tests and tolerances on development data, then freeze them before final testing. Use bootstrap intervals grouped by session/set; avoid treating adjacent frames as independent trials.

- Fidelity: held-out sequence/action likelihood and calibration; matchup-conditioned option frequencies; spacing and timing distributions; movement, DI, tech, ledge, recovery and punish tendencies; reaction timing and changes after repeated opponent habits. Compare bot-to-pro differences with variation between two independent real-pro sessions. Require the bot's deviation to fit within a predeclared natural-variation envelope on supported, sufficiently sampled situations.
- Identity: blind expert comparisons and same-character control players. Reviewers must distinguish the target's style from generic strong character play. Character recognition alone does not count. Jev is a supplementary reviewer, not the sole judge.
- Live reliability: every supported pro, legal stage, representative opponent archetype and recovery case; no crashes, deadlocks, impossible controller values or repeated avoidable recovery failures. Review low-percent KOs with trajectories rather than classifying every low-percent KO as a self-destruct.
- Strength: sufficient fixed-seed and independent-seed games against vanilla level 9, the previous model and, once integrated, Phillip with its supported delay. Report wins, losses and uncertainty; CPU wins alone do not establish pro fidelity.
- Runtime: trainer/runtime recurrent-state and input parity, resets, savestates, repeated same-frame requests, switching pros, offline-only enforcement, measured frame cost on representative supported CPUs, model loading/fallback and packaging.
- Product: controller entry, all panel looks, Fight a pro character selection, replay takeover, combo-drop termination, Training/search transitions and return/requeue behavior. Live matchmaking requires an authenticated integration check; hidden scripted UI cannot substitute for the actual controller chord.

Deliverable: a per-player scorecard with unsupported or thinly sampled situations explicitly listed. All required rows must pass before labeling the candidate ready for the owner's first playtest.

## 7. Compress the successful model and validate the exact package

Train and evaluate in float first. Distill to a smaller student only after establishing a reliable faithful teacher. Evaluate post-training int8 quantization, then quantization-aware training if needed. Roughly 1.6 MB is a plausible storage target for the existing 1.57-million-weight network at one byte per weight plus metadata; five independent policies may need more. Set the shipping size from measured fidelity, not a fixed promise.

Measure both file size and runtime memory/compute. Dequantizing at load can shrink downloads without reducing runtime memory. Compare compressed and float models on held-out decision distributions and live rollouts; discard compression that meaningfully changes habits or recovery. Never call size reduction evidence of Phillip distillation.

Build a new candidate against the latest fetched hotfix lineage. Rebuild the host and matching native game DLL, regenerate generated layout checks where required, run relevant tests and full suite, and test the actual packaged files with hashes. Prepare accurate release notes and required licenses. Publishing remains a separate owner-authorized action.

## Work order and time reporting

1. Finish source/schema and hotfix integration checks; audit identity and candidate-player availability.
2. Create clean splits, coverage reports, parity checks and the Jev pilot.
3. Train and score one pro-only pilot end to end. Measure games/frames processed per hour, GPU throughput, data-fetch limits and evaluation cost.
4. Use those observed rates to estimate the remaining collection/training/evaluation time. Report an ETA range and update it as bottlenecks change. The earlier two-hour imitation runs do not estimate the full distillation and validation pipeline.
5. Scale the winning recipe to five verified corpora; compare optional teacher/self-play additions, compress, validate the exact release candidate and produce a playtest build.

Current status: plan, hotfix source merge and host build completed. All 112 host tests pass. A real-replay PyTorch/C++ comparison passes across 256 frames with maximum recurrent hidden-state error 0.00001478. Model-file validation and memory-boundary regressions are covered. Five data-pipeline tests pass. The first two aMSa replays are extracted with exact-code evidence and grouped splits. A 100-game sample per legacy pro found nonconsecutive rows in 82 Mang0, 8 Zain, 62 Hungrybox and 100 Cody games; canonical final-revision selection now repairs duplicate/rollback sequences and rejects true gaps. Existing models were trained before this repair and need reevaluation/retraining.

Jev 1.14 reviewed the plan using preserved typed questions and answers under `runs/pro-fidelity-plan`; this is a plan review, not a gameplay-quality benchmark. The current four-pro deployed model remains unchanged. Five-player fidelity, Phillip integration, Jev gameplay-label quality, quantization, the matching rebuilt native game DLL and final release readiness are not yet verified. Do not publish or tell the owner the final ghosts are ready on the strength of existing training logs.

## References

- https://github.com/vladfi1/slippi-ai
- https://docs.typesafe.ai/introduction/coding-agents
- https://docs.typesafe.ai/cookbooks/autoresearch_feature_discovery
- https://docs.typesafe.ai/api/system-one
- https://lunarmelee.com/developers
