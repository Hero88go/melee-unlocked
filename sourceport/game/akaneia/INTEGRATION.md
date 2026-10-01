# Akaneia fighters: integration layer

This folder holds the native C for the fighters Akaneia (an m-ex build) adds: Wolf, Diddy Kong,
Charizard, Lucas, Sonic, King Dedede and Tails. Each fighter lives in `<fighter>/` and is
described by one `MuAkFighter` (`mu_ak_fighter.h`). `mu_ak_fighters.c` is the registry between
those fighters, the m-ex data layer (`../shim/mu_mex.c`) and the decomp's per-kind dispatch.
No PowerPC is run or translated anywhere in this folder.

## Kinds

m-ex numbers the added fighters right after Roy (internal 27 to 33 on Akaneia, plus an empty
slot at 34) and moves the special fighters (Master Hand to Sandbag) to 35 to 40. The native game
keeps every retail kind where it is. Each added fighter gets its own kind:

    native kind = MU_AK_KIND_BASE (0x22) + (m-ex internal id - Ft_Kind_MasterH)

On Akaneia: Wolf 0x22, Diddy 0x23, Charizard 0x24, Lucas 0x25, Sonic 0x26, Dedede 0x27, Tails 0x28.
Kind 0x21 (`Ft_Kind_None` / `Ft_Kind_Max`) keeps meaning "no fighter". `MU_AK_KIND(kind)` tests the
range. `mu_ak_kind_from_mex()` and `mu_ak_mex_internal()` convert both ways. Slots are matched by
the fighter file name in MxDt (`PlWf.dat`), so a disc that orders its fighters differently still
works.

The experimental creation layer also reserves native character kinds from
`MU_AK_CKIND_BASE` (0x22), with the same slot offset. `mu_ak_ckind_from_mex()` and
`mu_ak_mex_external()` convert the disc's external character ids at the CSS and
Slippi boundaries. `Player_MuSetAkKind()` fills or clears only added mappings and
the None sentinel; every retail mapping stays unchanged. These character-kind
edits are source only, unbuilt and unrun, behind `MU_AKANEIA_FIGHTERS`. The default
build retains the retail character table size and all added fighters remain locked.

The per-kind tables are sized `FT_KIND_TABLE_MAX` (`ft/forward.h`: `MU_FT_KIND_CAP` = 0x32
natively, `Ft_Kind_Max` on the console). Retail entries are unchanged. The added slots are NULL
until the registry fills them.

## Retail, online and replays

- `mu_ak_apply()` runs at every content view change (`mu_mex_boot`).
- In the retail view, and on a retail disc, it clears every added slot and fills nothing.
- Kinds at or above 0x22 then do not exist, so every hook (`MU_AK_KIND` tests, item kinds past
  `It_Kind_Kyasarin_Egg`) is inert.
- Retail kinds read exactly the table entries they always did.

## Registering a fighter

1. Write `<fighter>/<fighter>.c` defining `const MuAkFighter mu_ak_<fighter> = { ... };` with
   `.name`, `.file` (the disc file, for example `"PlWf.dat"`) and the callbacks you have.
2. CMake picks up every `akaneia/**/*.c`. It defines `MU_AK_HAVE_<FIGHTER>` for
   `mu_ak_fighters.c` only when `<fighter>/<fighter>.c` exists, so the registry names your symbol
   only then. Weak symbols do not work for this in the MinGW DLL link: an undefined weak fails to
   link, and a weak definition overrides the strong one.
3. Leave a field NULL to get the m-ex default for that fighter. That is the retail function MxDt
   names for it, found by comparing it with MxDt's entries for the retail fighters. For example,
   Charizard's MxDt defaults are Bowser's functions and the common double jump. An empty default
   means the m-ex behavior for an empty slot.
4. Set `.flags = MU_AK_READY` once the fighter is complete enough to play. The character select
   screen still keeps every added fighter locked until the creation layer below exists
   (`mu_ak_css_selectable` returns 0).
5. Articles: set `.articles` / `.article_count`, one `ItemLogicTable` per article, in the order of
   the fighter's MxDt item lookup. Get the game's item kind with `mu_ak_item_kind(fp->kind, i)`.
   Hand the article data from your own file to the item code from your onload, with
   `it_8026B3F8(article, kind)` or `mu_ak_article_set`.

Store every callback with its real signature, cast to `MuAkEvent`. Retail tables are written with
the value as is.

## Field to decomp site map

The index is the m-ex ftFunction index: `ReplaceThis` in the fighter file's ftFunction relocation
table, which is also the table index in MxDt `fighter_function`. It was verified against Akaneia's
MxDt and the retail `main.dol` tables, and against the m-ex `Header.s` names. Several MexTK names
are misleading: use the "is really" column.

| # | MuAkFighter field | is really | decomp table | call site(s) | real signature |
|---|---|---|---|---|---|
| 0 | onload | OnLoad | ftData_OnLoad | fighter.c Fighter_Create, ftdemo.c ftDemo_CreateFighter | void (HSD_GObj*) |
| 1 | ondeath | OnDeath (respawn setup) | ftData_OnDeath | fighter.c Fighter_Spawn | void (HSD_GObj*) |
| 2 | onunknown | OnDestroy / user data remove | ftData_OnUserDataRemove | fighter.c Fighter_Unload_8006DABC | void (HSD_GObj*) |
| 3 | move_logic | action states from 341 | ftData_CharacterStateTables | fighter.c Fighter_Create sets `fp->x20_actionStateList`; read in Fighter_ChangeMotionState (`[msid - fp->x18]`) and ftCo_AppealS.c | MotionState[] |
| 4 | specialn | SpecialN | ftData_SpecialN | ftCo_Attack100.c ftCo_800D6824 | void (HSD_GObj*) |
| 5 | specialairn | SpecialAirN | ftData_SpecialAirN | ftCo_SpecialAir.c ftCo_SpecialAir_CheckInput | void (HSD_GObj*) |
| 6 | specials | SpecialS | ftData_SpecialS | ftCo_SpecialS.c ftCo_SpecialS_CheckInput / doEnter | void (HSD_GObj*) |
| 7 | specialairs | SpecialAirS | ftData_SpecialAirS | ftCo_SpecialAir.c | void (HSD_GObj*) |
| 8 | specialhi | SpecialHi | ftData_SpecialHi | ftCo_Attack100.c ftCo_Attack100_CheckInput, ftCo_800D69C4 (NULL check) | void (HSD_GObj*) |
| 9 | specialairhi | SpecialAirHi | ftData_SpecialAirHi | ftCo_SpecialAir.c, ftCo_Attack100.c ftCo_800D69C4 | void (HSD_GObj*) |
| 10 | speciallw | SpecialLw | ftData_SpecialLw | ftCo_Attack100.c ftCo_800D68C0 | void (HSD_GObj*) |
| 11 | specialairlw | SpecialAirLw | ftData_SpecialAirLw | ftCo_SpecialAir.c | void (HSD_GObj*) |
| 12 | onabsorb | OnAbsorb | ftData_OnAbsorb | fighter.c Fighter_procCollResolve | void (HSD_GObj*) |
| 13 | onitempickup | item pickup (ext) | ftData_OnItemPickupExt | ftpickupitem.c | void (HSD_GObj*, bool) |
| 14 | onmakeiteminvisible | item invisible | ftData_OnItemInvisible | ftcommon.c | void (HSD_GObj*) |
| 15 | onmakeitemvisible | item visible | ftData_OnItemVisible | ftcommon.c | void (HSD_GObj*) |
| 16 | onitemdrop | item release (ext) | ftData_OnItemDropExt | ftcommon.c | void (HSD_GObj*, bool) |
| 17 | onitemcatch | item pickup | ftData_OnItemPickup | ftcommon.c | void (HSD_GObj*, bool) |
| 18 | onunknownitemrelated | item drop | ftData_OnItemDrop | ftcommon.c | void (HSD_GObj*, bool) |
| 19 | onunknowncharactermodelflags1 | onApplyHeadItem | ftData_UnkMotionStates1 | ftcommon.c | void (HSD_GObj*) |
| 20 | onunknowncharactermodelflags2 | onRemoveHeadItem | ftData_UnkMotionStates2 | ftcommon.c | void (HSD_GObj*) |
| 21 | onhit | knockback enter (damaged eye texture) | ftData_OnKnockbackEnter | ftcommon.c | void (HSD_GObj*) |
| 22 | onunknowneyetexturerelated | knockback exit (normal eye texture) | ftData_OnKnockbackExit | ftcommon.c | void (HSD_GObj*) |
| 23 | onframe | OnFrame | ftData_UnkMotionStates3 | fighter.c Fighter_procAnim | void (HSD_GObj*) |
| 24 | onactionstatechange | action state change | ftData_UnkMotionStates4 | ftcolanim.c | void (HSD_GObj*) |
| 25 | onrespawn | onReapplyAttr (retail LoadSpecialAttrs) | ftKindCalcIndiviParamTable | ftchangeparam.c (asserts non-NULL: an empty slot gets a no-op) | void (HSD_GObj*) |
| 26 | onmodelrender | model matrix callback | ftData_UnkMtxFunc0 | ftdrawcommon.c (x2) | void (HSD_GObj*, int, Mtx) |
| 27 | onshadowrender | model group visibility | ftData_UnkIntBoolFunc0.model_events | ftparts.c (x3) | void (Fighter*, int, bool) |
| 28 | onunknownmultijump | getter | ftData_UnkIntBoolFunc0.getter | no consumer in the decomp yet | HSD_JObj* (HSD_GObj*) |
| 29 | onactionstatechangewhileeyetextureischanged | eye texture reset | ftData_UnkCallbackPairs0[].x0 | ftanim.c ftAnim_80070654 | void (HSD_GObj*) |
| 30 | ontwoentrytable | not code: m-ex costume material and visibility lookup data | (none) | ftAnim_80070308 / ftParts_800749CC; native side is mu_mex_parts_costume | leave NULL |
| 31 | enterfloat | onFloat | registry hook MU_AK_HOOK_FLOAT | ftpeachfloat.c ftPe_8011BA54 (stick down + X/Y), ftPe_8011BAD8 (falling + stick up or X/Y) | bool (HSD_GObj*, int); returns "entered" |
| 32 | enterdoublejump | onDoubleJump | MU_AK_HOOK_DOUBLEJUMP | ftCo_JumpAerial.c ftCo_JumpAerial_CheckInput (empty = no double jump, as in m-ex) | void (HSD_GObj*) |
| 33 | entertether | onZair | MU_AK_HOOK_ZAIR | ftCo_AirCatch.c ftCo_800C3B10 (held L/R + A; sets used_tether) | void (HSD_GObj*) |
| 34 | onlanding | onLanding | MU_AK_HOOK_LANDING | fighter.c Fighter_ChangeMotionState, grounded branch (in place of the Peach float reset) | void (HSD_GObj*) |
| 35 | onsmashf | onFSmash | MU_AK_HOOK_FSMASH | ftCo_AttackS4.c decideFighter (empty = common forward smash) | void (HSD_GObj*) |
| 36 | onsmashhi | onUSmash | MU_AK_HOOK_USMASH | ftCo_AttackHi4.c both CheckInput functions | void (HSD_GObj*) |
| 37 | onsmashlw | onDSmash | MU_AK_HOOK_DSMASH | ftCo_AttackLw4.c ftCo_AttackLw4_CheckInput | void (HSD_GObj*) |
| 40 | (MxDt only) | MoveLogicDemo | ftData_UnkMotionStates0 | ftdemo.c (`x20_actionStateList` of demo fighters) | MotionState[]; default only (Tails: Mario's) |

Notes:

- **Action states (#3).** `fp->x18` is 0x155 (341): action state `msid` >= 341 reads
  `move_logic[msid - 341]`. Nothing bounds-checks it, so `move_logic_count` must cover every id
  the fighter enters. The table is only read.
- **#29.** m-ex writes this index into the pair table as if the table were flat, so MxDt has no
  per-fighter default for it. The registry puts your function in the pair's first callback. The
  second callback (`x4`, per texture frame) has no MuAkFighter field. Add one at the end if a
  fighter needs it.
- **#38 and #39.** `onGetExtResultAnim` and `onIndexExtResultAnim` are MxDt-only results-screen
  hooks (ftDemo_GetMotionFileString, ftDemo_CreateFighter). They are empty for all seven Akaneia
  fighters, so they belong to the creation layer.
- **Direct code patches.** An ftFunction relocation entry with bit 31 set in `ReplaceThis` is a
  direct code patch (a branch written into the DOL), not a table slot. Wolf has none. If a
  fighter has any, ask the integration owner for a named hook: never patch code.
- **Attributes.** A fighter's own attributes (`ft_data->ext_attr`, `dat_attrs`) are big-endian
  disc data. Read them through a `DISC_STRUCT` type, as the retail fighters' `*_DatAttrs` do.
  Slot #25 (onReapplyAttr) is where retail fighters copy them.

## Articles (item registry)

- **Item kinds.** m-ex gives each added fighter's articles item kinds past the retail ones (MxDt
  fighter array 19, MEXItemLookup: Wolf 253 to 256, Diddy 257 to 261, Charizard 262 to 265,
  Lucas 266 to 276, Sonic 277, Dedede 278 to 283, Tails 284 and 285).
- **Kind map.** `mu_ak_apply` builds the map from item kind to (fighter slot, local article)
  from that table.
- **`it_8026B3F8`** (`it/it_26B1.c`) stores the article data of kinds past
  `It_Kind_Kyasarin_Egg` in the registry instead of indexing `it_804D6D38`.
- **`Item_80267978`** (`it/item.c`) takes the article data and the `ItemLogicTable` from the
  registry for those kinds. An added kind the registry cannot serve logs a line and falls through
  to the retail stage-item branch, which is out of range. A fighter must register its data before
  it spawns the article.
- **Other item-kind tables.** Other per-item-kind tables the item code reads between spawn and
  `Item_80267978` have not been audited for kinds past 236. Check them when the first article
  spawns.

## What still blocks a fighter from being playable (the creation layer)

The registry and the dispatch hooks are done. Creating an added fighter still needs the following,
all in the mod view only. The widened tables already have the slots; each item below is a fill
from MxDt or PlCo into slot `MU_AK_KIND_BASE + n`, plus an index fix where the game uses a
different id space.

1. **Character kind (CSS id).** m-ex external ids 26 to 32 are the added fighters, but retail
   CharacterKind 26 and up are Master Hand and the other special characters. The native game needs
   character kinds for the added fighters (past the retail count, as for fighter kinds) and a
   translation at the CSS (`mncharsel.c`, `mu_css_mex_setup`), in `pl/player.c` `ftMapping_list`
   (ckind to fighter kind, `ChKind_Max` sized) and everywhere a ckind indexes a table:
   - names, stock icons (`gm_80168BF8`), emblems;
   - announcer and victory audio (`lb/lbaudio_ax`, `unk_arr_803BC4A0[0x21]`);
   - results screen, records and the save file per character;
   - Slippi game info and replays (the ckind is written into .slp; that must stay the m-ex
     external id for Dolphin compatibility).

   This is the largest piece. Online needs nothing extra: added fighters are Direct-only with a
   matching build, like every mod.
2. **Fighter files.** `ftData_803C1F40[kind]` = {`PlWf.dat`, `ftDataWolf`} (MxDt pl_files),
   `ftData_803C23E4[kind]` = `PlWfAJ.dat` (MxDt fighter array 7), `ftData_Table_Unk0[kind].count`
   = animation count (array 8), `ftData_UnkBytePerCharacter[kind]` = effect file id (array 9),
   and `ftData_803C2468` / `ftData_803C24EC` / `ftData_UnkDemoCallbacks0` / `ftData_UnkIntPairs`
   for the demo and results fighters (arrays 6, 10, 18 and the MxDt function tables 38 and 39).
   `ftData_UnkDemoCallbacks0[kind]` is called without a NULL check in `ftDemo_CreateFighter`.
3. **Costumes.** `CostumeListsForeachCharacter[kind]` and `ftData_803C2360[kind]` from MxDt
   costume_files[internal]. The code is in `mu_mex.c` (`mex_prepare_costumes`), but its arrays
   are `[Ft_Kind_Max]` and `mu_mex_parts_costume` bounds by `Ft_Kind_Max`: widen them to
   `FT_KIND_TABLE_MAX`.
4. **PlCo common data.** `ftPartsTable` and `Fighter_804D6540` (fighter.c,
   Fighter_LoadCommonData) are remapped into `[Ft_Kind_Max]` copies. Widen those two copies and
   copy the added fighters' entries from PlCo index `Ft_Kind_MasterH + n`.
5. **Per-kind resets.** `ft_800852B0` and `ft_8008549C` (ftdata.c) loop to `Ft_Kind_Max`, and
   `ftData_Table_Unk1` is looped in fighter.c. Extend these loops to the added slots once those
   slots hold files.
6. **Kirby.** Copy abilities and hats are per victim kind (`ftKb_Init_803C9FC8`,
   `ftKb_Init_803CA9D0`, `ftKb_Init_803CB46C`, `KirbyHatStruct.hats`, all `[Ft_Kind_Max]`).
   Kirby inhaling an added fighter indexes past them. m-ex carries `kirby_data` /
   `kirby_function` for this. Until it is native, Kirby must treat an added kind as having no
   ability. That needs one check in ftkirby, which I have not written.
7. **Kind checks.** Some code compares kinds with `< Ft_Kind_Max` or asserts on them. Audit the
   files that take a FighterKind from outside (`pl/player.c`, `gm/*`, `lb/lbaudio_ax.c`,
   `if/*`) when the first added fighter is created.
8. **Stage select.** Not part of the fighters. Akaneia's new stages carry their own PowerPC
   (grFunction), like the fighters, and the SSS needs MnSlMap's mexMapData. That is separate work.

Order to make one fighter playable: 1, 2, 3, 4 and 5 for that fighter, then turn on
`mu_ak_css_selectable` for fighters with `MU_AK_READY`. Replays stay exact because every step is
data filled at the view change, not per frame.

## Checks to run (main session)

- Full build: `ninja -C build-sourceport-gcc melee_game`. CMake reconfigures automatically for
  the new glob. Expect no undefined symbols: no fighter file is required.
- The 199-replay exact gate, and the M7 online pair with no mod profile. Retail must be byte for
  byte identical: the retail path only gained `MU_AK_KIND` tests that are always false.
- An Akaneia profile boot: the log shows one `[ak] PlXx.dat: kind NN, no native code in this
  build (locked)` line per added fighter, and the CSS is unchanged (the new fighters are locked).
- Once a fighter file lands: reconfigure, then the log line changes to
  `[ak] Wolf (PlWf.dat): kind 34, in progress`.
