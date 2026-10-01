# 0.9: the creation layer (Akaneia fighters playable on the Source Port)

Goal of the first milestone: Wolf selectable on the character select screen of an Akaneia disc,
playable in an offline VS match on the Source Port, with his own model, costumes, sounds, effects,
name, stock icon and results screen. Retail discs, the retail view online, and replays of retail
games stay byte for byte as they are (every change is behind the mod view or an added kind).

Validation: the same match recorded on the Static Recomp running Akaneia's original code, played back
on the Source Port, per-frame state digests equal. Only after that is a fighter marked MU_AK_READY.

## Data (MxDt.dat `mexData.fighter`, m-ex MexTK/include/mxdt.h)

| index | field | indexed by | used for |
|---|---|---|---|
| 0 | names | external | CSS name, HUD, results |
| 1 | pl_file {name, symbol} | internal | fighter file and its ftData symbol |
| 2 | insignia | external | emblem |
| 3 | ft_kind_desc | external | external -> internal map |
| 4 | costume_info | external | costume count, team colors |
| 5 | costume_file | internal | costume files |
| 6 | ftdemo {result, intro, ending, wait} | internal | demo fighter animation symbols |
| 7 | anim_filenames | internal | PlXxAJ.dat |
| 8 | anim_num {0, count}, 8 bytes each | internal | animation count |
| 9 | effect_index | internal | the fighter's effect file (mexData.effect.files) |
| 10 | result_file | external | results screen file |
| 11 | result_scale | external | results screen scale |
| 12 | victory_theme | external | results music |
| 13 | announcer_call | external | announcer SFX id |
| 14 | ssm_files {ssm_id, ...} | internal | sound bank |
| 15-18 | costume_pointers, ft_archives, walljump, rst_runtime | internal | runtime |
| 19 | item_lookup | internal | articles (done) |
| 20-32 | target test, music, vi files, endings, race, demo params, trophies, ending scale | mixed | 1P modes |

## Steps (in order), each gated to the mod view

Step 1 source edits are present as of 2026-10-01. The isolated experimental
character-kind and record-start fixtures built and passed (2/2). The game has not
been built or run with the step 2 edits. The
`MU_AKANEIA_FIGHTERS` option remains off by default and now defines the same C
preprocessor flag for the experimental creation layer. Added character mappings
are filled and cleared at the content view change. CSS ids use native kinds;
portrait frames and Slippi Game Start, selection and post-frame records keep the
disc's external or internal ids at their respective boundaries. Added fighters
remain locked and no readiness flag was changed.

The source-only audit packet is in `run-source/jev-ak-creation-20261001/`. Its
state, questions, request and response are pinned evidence; do not regenerate
them in place after review. The audit generator can produce a new snapshot in a
different output directory. The packet records the legacy 113-site claim separately
from measured lexical candidates. The claim has no supplied counting method, so
the five-file list is a starting inventory, not proof that every consumer is known.
Preload loops in `lb/lbdvd.c`, CSS restoration limits, Slippi boundaries, indirect
save writes and all added item-kind consumers also need explicit classification.

The `mu_ak_character_kind_test` target is available only in an experimental build.
Its C fixture exercises the production conversions with reordered external ids,
shifted special fighters, missing slots and retail passthrough. The prerequisite
advisory review is complete. Independent source inspection agrees that the index
inventory is incomplete and the safety checks must move earlier. The advisory
response is recorded separately from local judgments in `local-judgments.json`;
it does not replace build, retail replay, online or mod digest gates.

Before experimental gameplay, complete and test this safety inventory. An index
sentinel or a NULL fallback is not sufficient when its caller still indexes or
dereferences it.

- **Kinds and wire boundaries:** run the step 1 fixture before any experimental
  game launch. Verify disc aliases, absent slots, shifted special fighters, None,
  subfighters and retail view clearing with actual disc metadata. Slippi Game
  Start and selection use external ids; post-frame uses internal ids.
- **Preload and archive ownership:** classify `lb/lbdvd.c` character loops and
  `pl/player.c` load/demo paths before extending a loop or enabling a fighter.
  Require common fighter and item data, correct native archive ownership,
  costume resources and non-NULL demo callbacks at every entry/exit transition.
- **CSS restoration:** inventory saved selections, Random, costume changes,
  transformed characters and return from results. Keep added characters locked
  while resources or restoration paths are incomplete; do not merely widen
  `CKind_Playable_Count` checks. Verify mod to retail view clearing and stale ids.
- **Save and record writes:** trace the callers of `gm_CKindToSelKind`, including
  `gmvs`, `gmmultiman`, `gmclassic`, `gmhomerun`, `gm_180A`, `gm_1736` and `gm_17C0`.
  Added characters have no retail save slot. Skip every affected write before
  acquiring a save pointer or indexing a record. One-player modes remain
  unsupported until their admission and record paths are guarded.
- **Kirby:** reject an added victim kind before storing a hat kind or indexing
  copy resource/callback tables. Audit ingest in `ftCo_ThrownKirby`, special-N
  callers, hat setup and ability removal, not only a single ftkirby check.
- **Items:** audit every added-kind read before article creation, including the
  render table in `Item_8026862C`. A missing article must fail before retail
  stage tables are indexed; a later registry lookup cannot protect an earlier
  read. Cover spawn, destroy, reflection, pickup and view teardown.
- **Results:** validate result filenames, scale, victory music, announcer ids,
  demo callbacks, stock/emblem resources and return to CSS. A valid file alone
  does not establish a valid results callback path.

Each inventory entry records its id domain, table extent, safe default or mod
data source, affected callers and a verification gate. Missing callers, assets,
callback signatures, disc metadata or digest evidence mean insufficient
evidence. Keep `MU_AK_READY` and CSS selectability unchanged in that case.

1. **Character kinds.** Added fighters get native character kinds past the retail ones:
   `MU_AK_CKIND_BASE 0x22` + slot (the same numbers as their fighter kinds). `ftMapping_list` widened;
   the registry fills the added entries at each view change and clears them in the retail view. The
   character select screen translates m-ex external ids to native kinds (`mu_mex_css_icons`), and
   the Slippi recorder translates back (the .slp keeps m-ex's external id, so Dolphin reads it).
2. **Per-character tables and safety inventory:** classify the measured candidates
   and all indirect consumers above before gameplay. Each added-kind read uses
   m-ex data or a safe fallback; save writes are skipped before indexing. The
   legacy 113-site list below is not a completeness claim. Current guarded name,
   trophy, stock, selector/unlock, ending-movie and VS record edits are unbuilt.
   Live Stadium, Classic, Home-Run and Training record consumers now have
   incremental added-kind guards. Kirby ingest, preload and callback entry paths
   reject added copy kinds. Added item creation stops before allocation and
   retail render indexing while render dispatch is unmapped; article lookup
   failure cannot fall through to stage tables. These edits remain unbuilt.
   Full mode admission, preload, Kirby teardown, item and result callback
   coverage remains open.
3. **Fighter files**: ftData_803C1F40 (PlXx.dat + symbol), ftData_803C23E4 (PlXxAJ.dat), animation
   count, effect file id, demo tables; `ftData_UnkDemoCallbacks0` must be non-NULL.
4. **Costumes**: mex_prepare_costumes for added kinds (arrays widened to FT_KIND_TABLE_MAX).
5. **PlCo common data**: ftPartsTable / Fighter_804D6540 copies widened, filled from PlCo index
   Ft_Kind_MasterH + n.
6. **Per-kind resets**: loops to Ft_Kind_Max extended to the filled added slots.
7. **Effects**: efSync ids 5000+n -> entry n of the fighter's own effect file (m-ex effect table).
8. **Sounds**: the fighter's SSM bank (index 14) and announcer call (13).
9. **Kirby**: an added kind has no copy ability until kirby_data is native. Apply
   the earlier ingest and table guards before any experimental match with Kirby.
10. **Kind checks**: audit `< Ft_Kind_Max` / asserts in pl/, gm/, lb/lbaudio_ax.c, if/.
11. **Items**: `mu_ak_item_create` for articles (Wolf NOTES.md item 1). Complete
    the earlier preconstructor and missing-article safeguards first.
12. **Results screen**: result file/scale/victory theme/announcer for added kinds.

## Status 2026-10-01 night (experimental build only)

The experimental DLL links with all seven fighters (the m-ex runtime services are in
`common/mu_ak_services.c` and three flagged functions in the decomp). Steps 3, 4, 5 and 6 are
written and compiled behind `MU_AKANEIA_FIGHTERS`: `fill_files` / `clear_files` in
`mu_ak_fighters.c`, `mu_mex_ak_costumes` in `shim/mu_mex.c`, the PlCo copies in
`Fighter_LoadCommonData`, the reset loops in `ft/ftdata.c`. None of it has run on an Akaneia
disc: the release host refuses that disc (`akaneia_disabled` in
`port/runtime/host/source_mod_overlay.cpp`), so the mod view was never entered. A character kind
is mapped only when its fighter file, symbol, animation file, animation count and at least one
costume file are present. Effects stay off (effect file id "none") until step 7. The retail path
of the experimental build was run: see `run-source/rel09-b1-wolf/PROGRESS.md`.

Found while reading, for step 10: the CPU tables of PlCo (`Fighter_804D64FC` x4 to x20, read in
`ft/ftcpuattack.c` and `ftCo_0A01.c`) are indexed by `fp->kind` with no remap, so an added
fighter must not be a CPU player until those reads use the m-ex internal id.

## Legacy index-site list (character kind, incomplete)

The original counts sum to 113 and lack a counting method. The pinned lexical
scan measured 99 matches on 97 lines in these five files. Neither count includes
every indirect save, preload, Kirby, item or result consumer.

- gm/gm_1601.c (57): results data (gm_80160474, 4DC, 564), names and scales (gm_80160980,
  fn_801609E0, gm_80160A60, gm_80160B40, gm_80160C90, fn_80160F58), select kind and unlocks
  (gm_CKindToSelKind, gm_IsCKindUnlocked, gm_80164A0C), costumes (gm_GetNumCostumesForCKind,
  gm_80169264/90/BC).
- gm/gm_181A.c (32): 1P mode records per character (icons, times, scores): skip for added kinds.
- pl/player.c (20): ftMapping_list.
- gm/gm_1A9B.c (3): congratulations movies (m-ex end movie files, index 26).
- lb/lbaudio_ax.c (1): sound bank mask per character (m-ex ssm_files, index 14).
