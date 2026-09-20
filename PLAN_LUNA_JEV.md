# Source port finish plan: Luna xHigh with Jev

2026-09-20. This is the current execution overlay for the original finish plan at
`C:/Users/Chandler/.claude/plans/no-the-port-is-wise-wave.md`. Read that file for
the full build commands and per-subsystem implementation constraints. Latest dated
handoff sections take precedence over older state descriptions. Do not repeat
completed merges or seed-hook work.

## Ownership and execution loop

- Requested reasoning executor: `gpt-5.6-luna`, reasoning effort `xhigh`.
  This document does not change the running session's model. Select it in the
  client before resuming; never claim that an unverified model switch happened.
- One coding executor, no spawned agents. Luna owns source inspection, hypotheses,
  patches, deterministic tests, review and commits. Keep each patch to one measured
  cause. Record observation, prediction, test and outcome before changing strategy.
- Jev is optional advisory batch classification, not a code writer or test oracle.
  Batch independent coverage judgments and candidate relevance questions before
  detailed review. Supply actual snippets, measured output, relevant requirements,
  source commit, dirty diff hash and executable hashes. Never send game assets,
  binary dumps, credentials or an entire worktree. Include insufficient-evidence
  choices. Keep measurements separate from hypotheses in state.
- Use local tools for file inventories, symbolization, CSV comparison, WAV stats,
  hashes and pass/fail. Jev cannot override a failing gate, authorize mutations,
  suppress a test or justify relaxing a comparator.
- First Jev pilot: review a batch of 0.6.4 feature/call-site/test mappings, with
  explicit source excerpts. Independently inspect every mapping in this first
  batch. Record packet preparation, request, follow-up review and total time,
  token usage, false flags and missed gaps. Retain Jev for task classes where this
  reduces total review effort without missing required coverage. No claimed
  speedup from API latency alone. Do not build a new agent framework for this.
- Run `python tools/jev_review.py --state <packet.json> --questions <questions.json>
  --dry-run` first. Dry-run reports metadata, not packet contents or secrets.
  Remove `--dry-run` only for reviewed text and a locally configured
  `TYPESAFE_API_KEY`. Default pin is `jev-1.13.0`; an unavailable pin is unresolved,
  not permission to silently change models. `--model jev-latest` bypasses cache.
  One request, 15-second socket timeout, no automatic retries; this socket timeout
  is not a strict whole-process deadline. On failure continue local work.
- Helper cache keys include the whole packet, questions and model. Evidence hashes
  must be included in the packet by its preparer. Cached judgments are not new
  tests. Before using a cached answer verify that the source and binary hashes
  still match. The helper checks basic response types, not factual correctness.

## Current state and first actions

Outer branch `sourceport`, committed HEAD `2d8a950`; nested branch `mu/native`,
committed HEAD `d8afcc7f2`. Original THP crash was symbolized and the decomp merge
reverted. Shipping 0.6.4 commit `bf9e8b5` was merged in `5aef2d8`. Do not remerge.
Latest historical sweep gate: 26/26 characters, 27/27 stages, triage clean in
`run-source/sweep-ch3` and `run-source/sweep-st3`. Host test count is now 16, not 13.

1. Check both branch names and dirty diffs. Preserve existing crash files and
   concurrent changes in `port/recomp/emit.py`, `port/runtime/host/host.cpp`,
   `src/melee/gr/grvenom.c` and `src/melee/lb/lb_00B0.c` in the nested repository.
   Do not stage or revert those changes as part of this tooling checkpoint.
2. Rebuild before making new gameplay claims. The old vanilla binary may contain
   removed animation logging, so rebuild the oracle too. Do not equate passing
   existing ctest binaries with validation of current dirty source.
3. Run baseline: native ctest 4/4, host ctest 16/16, 26-character and 27-stage
   sweeps at 2400 retraces and clean frame triage. Use original plan commands with
   unique output paths inside `run-source`, never shared default logs. Record
   source and executable identities. Only then start gameplay patches.
4. Continue M7 below. During an owned long build, prepare the feature inventory
   for the first Jev batch without changing shared game code concurrently.

## Remaining milestone order and exact acceptance

### M7: simulation parity

Saved traces `run-source/m7-native-6100/state.csv` and
`run-source/m7-vanilla-6100/state.csv` first differ at match frame 1:
`p0_anim_frame=41980000` (19.0) versus `41900000` (18.0), retraces 1628 versus
1339. This is a full 1.0 animation tick, not a proven cosmetic offset.

- Rerun both current builds using the existing `port/scripts/parity_vs.txt`,
  `--time-base 1 --rng-seed 305419896 --frames 6100 --fast --state-digest <csv>`.
  Keep menu inputs scene-relative and match inputs match-frame-relative.
- Compare with `python tools/lockstep_compare.py <native.csv> <vanilla.csv>
  --align match`. Seed once at the existing first VS callback. Do not return to
  the failed dispatch hook or pending-VI flush experiments.
- Trace the first idle-animation transition and calls to `ftAnim_8006E9B4`,
  AObj frame advancement, game-process dispatch and digest sampling. Use minimal
  temporary probes in both paths; compare ordering and values, not wall time.
  Jev may rank supplied candidate snippets but cannot establish the cause.
- Fix the measured earliest divergence, remove probes and repeat exact comparison.
  Target at least 3600 exact simulated frames: initial matchup items off, items
  on, then two additional scripted matchups. Report actual counts. If two
  focused instrumented hypotheses fail, record evidence and a concrete next
  experiment, mark M7 open and continue M4 as the original plan permits.

### M4: audio parity, not merely non-silence

- Reuse `tools/wav_stats.py`. Inspect dump placement and match scene/event windows
  before comparing audio: identical retrace counts do not align menu or VS events.
  Prior WAVs differ substantially; do not attribute this to dump semantics without
  evidence. Prove menu music, announcer and hit sounds separately.
- Extend the LE AX test to actual decoding, mixing and PB write-back, including
  two voices, stream hi/lo words and sample byte order. Verify allocation bounds.
- Investigate the sleeping-voice lifecycle behind `HSD_AudioSFXSetMix` returning
  false for vID -1. A no-crash guard does not prove retail-equivalent behavior.
- Gate: comparable event durations, non-silence, RMS and peak within 5 percent
  for matched deterministic windows, required sounds present and full baseline.
  The 5 percent ceiling makes the original 'few percent' requirement explicit;
  record any failure rather than adjusting it after seeing results.

### M10: save compatibility

Split host-pointer card cores from guest wrappers and connect native card APIs.
Preserve disc-order persistent and nested structs and console bitfields. Always
use `--card-dir` pointing to a new scratch directory. Gate: native round-trip,
both cross-engine load directions and identical GCI payloads after the same
script except documented timestamp fields. Never normalize unexplained differences.

### M8: authored subframes

Read original plan's runtime memory references first. Preserve RenderObserver
layout baked into guest libraries. Extract guest/native data through C POD
interfaces, append host API fields with a version bump, and flush GX at observer
boundaries. Keep existing interpolation policy and renderer behavior.
Gate: legacy five-mode 2400/0 validation unchanged; native unlocked/authored run
baseline clean; corresponding authored and rejection counters within 1 percent;
captures show distinct subframes. Physical feel remains user sign-off.

### M9: built-in toggles and full 0.6.4 feature accounting

Inventory every shipped feature from pinned commit `bf9e8b5` using local git
history, settings and call sites. For each record native/legacy applicability,
implementation location, test and remaining gap. Include upscalers, frame
generation, latency settings, controller profiles, display fixes, texture packs,
Gecko UI and authored subframes. Shared compilation is not compatibility proof.
Arbitrary PPC Gecko patches remain legacy-only. Implement compatible built-ins
as native C options according to the patch specification. Gate per option:
exact digest comparison or focused native test plus explicit compatibility note.
Never silently enable unsupported native options. Keep Legacy the default.

### M11: local package and final audit

Extend packaging with opt-in native executable and DLL inputs. Test a clean
scratch installation: both engines boot, Legacy default, engine selector present,
DLL crash frames symbolize, no Markdown or credentials packaged. No publishing.
Full baseline and milestone evidence audit required. Jev may flag contradictions;
local evidence determines completion. List physical-controller/UI/feel sign-offs
separately from automated results. M12 remains out of scope.

## Non-negotiable operating rules

Local commits only, never push/tag/PR. Only `sourceport` and `mu/native` changes.
Do not touch protected sibling worktrees. No D: paths. Check free space before
large runs; below 15 GB stop. Every game run uses `MELEE_NO_GC_ADAPTER=1`,
`--hidden --volume 0` and its own `--log-file`. Wait on owned processes and record
actual exit codes; only terminate an owned PID if needed, never a process name.
Do not restore, delete or commit crash byproducts. No debug probes, em dashes or
community member names in commits. Commit only owned files after the relevant
green gate; game milestones require the full baseline. Update dated handoff
with honest passed, failed, not-run and blocked states at each stop.

References checked 2026-09-20:
- https://developers.openai.com/api/docs/models/gpt-5.6-luna
- https://docs.typesafe.ai/api
- https://docs.typesafe.ai/patterns/fan-out

## TypeSafe documentation supplied by the user: read on continuation

Do not repeat a blanket claim that live TypeSafe documentation is unavailable.
Some `.md` fetches failed, but the normal API and fan-out pages were successfully
read during this session. Retry the normal page when a Markdown URL fails.

- Skill: `C:/Users/Chandler/.codex/skills/typesafe-ai/SKILL.md`
- Skill source: https://raw.githubusercontent.com/typesafe-ai/skills/main/skills/typesafe-ai/SKILL.md
- Index: https://docs.typesafe.ai/llms.txt
- Quickstart: https://docs.typesafe.ai/introduction/quickstart
- Building guide: https://docs.typesafe.ai/concepts/how-to-build-with-system-one
- Primer: https://docs.typesafe.ai/introduction/machine-learning-primer
- Patterns: https://docs.typesafe.ai/patterns
- API: https://docs.typesafe.ai/api
- Parallel questions: https://docs.typesafe.ai/patterns/fan-out
- Uncertainty: https://docs.typesafe.ai/patterns/confidence-routing
- Composite scoring: https://docs.typesafe.ai/patterns/composite-scoring

Verified HTTP contract: POST `https://api.typesafe.ai/v1/systemone`, Bearer token
from TYPESAFE_API_KEY, JSON body with state, model and questions. Questions use
type, instructions and criteria. Choice criteria is an option map; Score criteria
is an ordered array of 2 to 10 levels; Noul asks a binary probability question.
The response has model, answers keyed by question ID and usage. Choice/Score
include confidence and probabilities; Noul has its probability without separate
confidence. Batch independent questions over shared evidence. Typed output is
not proof of correctness. tools/jev_review.py implements this direct HTTP path;
no LangChain installation is necessary. Live inference remains unverified for
this helper until a replacement credential is locally configured.

The user also supplied article text and illustrative diagrams. Their assumed
latency/cost comparisons are not measured port speedups. Prefer the live API
contract over article snippets when they differ.
