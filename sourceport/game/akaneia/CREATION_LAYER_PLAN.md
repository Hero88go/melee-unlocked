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
| 8 | anim_num | internal | animation count |
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

1. **Character kinds.** Added fighters get native character kinds past the retail ones:
   `MU_AK_CKIND_BASE 0x22` + slot (the same numbers as their fighter kinds). `ftMapping_list` widened;
   the registry fills the added entries at each view change and clears them in the retail view. The
   character select screen translates m-ex external ids to native kinds (`mu_mex_css_icons`), and
   the Slippi recorder translates back (the .slp keeps m-ex's external id, so Dolphin reads it).
2. **Per-character tables** (113 index sites, listed below): each either reads the m-ex table for an
   added kind or returns a safe default; save records are never written for added kinds.
3. **Fighter files**: ftData_803C1F40 (PlXx.dat + symbol), ftData_803C23E4 (PlXxAJ.dat), animation
   count, effect file id, demo tables; `ftData_UnkDemoCallbacks0` must be non-NULL.
4. **Costumes**: mex_prepare_costumes for added kinds (arrays widened to FT_KIND_TABLE_MAX).
5. **PlCo common data**: ftPartsTable / Fighter_804D6540 copies widened, filled from PlCo index
   Ft_Kind_MasterH + n.
6. **Per-kind resets**: loops to Ft_Kind_Max extended to the filled added slots.
7. **Effects**: efSync ids 5000+n -> entry n of the fighter's own effect file (m-ex effect table).
8. **Sounds**: the fighter's SSM bank (index 14) and announcer call (13).
9. **Kirby**: an added kind has no copy ability until kirby_data is native (one check in ftkirby).
10. **Kind checks**: audit `< Ft_Kind_Max` / asserts in pl/, gm/, lb/lbaudio_ax.c, if/.
11. **Items**: `mu_ak_item_create` (Item_8026862C as is) for articles (Wolf NOTES.md item 1).
12. **Results screen**: result file/scale/victory theme/announcer for added kinds.

## Index sites by file (character kind)

- gm/gm_1601.c (57): results data (gm_80160474, 4DC, 564), names and scales (gm_80160980,
  fn_801609E0, gm_80160A60, gm_80160B40, gm_80160C90, fn_80160F58), select kind and unlocks
  (gm_CKindToSelKind, gm_IsCKindUnlocked, gm_80164A0C), costumes (gm_GetNumCostumesForCKind,
  gm_80169264/90/BC).
- gm/gm_181A.c (32): 1P mode records per character (icons, times, scores): skip for added kinds.
- pl/player.c (20): ftMapping_list.
- gm/gm_1A9B.c (3): congratulations movies (m-ex end movie files, index 26).
- lb/lbaudio_ax.c (1): sound bank mask per character (m-ex ssm_files, index 14).
