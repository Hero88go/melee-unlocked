# King Dedede (Akaneia) native port: notes

Source: `PlDe.dat` from the Akaneia v1.0.1 disc (internal fighter 32, symbol `ftDataDedede`).
The file has three roots: `ftDataDedede`, `ftFunction` (m-ex MEXFunction: 0x6C7C bytes of code,
1411 relocations, 24 exports, 292 debug symbols) and `itFunction` (4 article slots: 0 empty,
1..3 with code). Every routine below was read from the disassembly and rewritten by hand; none of
it has been run yet (no build, no game launch).

## Files

| File | Contents |
|---|---|
| `ftDe.h` | private header: motion state enum, attributes (`ftDe_DatAttrs`, 0x17C, big-endian), fighter vars, motion vars, article vars/attrs, integration hooks |
| `dedede.c` | `mu_ak_dedede`, load/respawn/destroy, item callbacks, eye textures, OnActionStateChange, double jump |
| `ftDe_MotionStates.c` | `move_logic` (states 341..395) as `MotionState` entries |
| `ftDe_SpecialN.c` | Neutral B (Inhale) states for Dedede |
| `ftDe_SpecialNCapture.c` | the captured fighter / item side of Inhale, the star spit |
| `ftDe_SpecialS.c` | Side B (Gordo Throw) |
| `ftDe_SpecialHi.c` | Up B (Super Dedede Jump) |
| `ftDe_SpecialLw.c` | Down B (Jet Hammer) |
| `itDe_Articles.c` | itFunction[1..3]: spit star, Gordo, Up B landing star, as `ItemLogicTable`s |

Build list for the integration layer's CMake: the eight `.c` files above.

## Routine status

All routines are **done** (written and syntax-checked); runtime behaviour is untested.
m-ex debug names on the left, native names on the right.

### ftFunction exports

| m-ex export slot | m-ex routine | native | status |
|---|---|---|---|
| onload | OnLoad | `ftDe_OnLoad` | done |
| ondeath | OnRespawn | `ftDe_OnDeath` | done |
| onunknown | OnDestroy | `ftDe_OnDestroy` | done |
| move_logic | move_logic | `ftDe_MotionStateTable` (55) | done |
| specialn / specialairn | SpecialN, SpecialAirN | `ftDe_SpecialN` -> `ftDe_SpecialN_Enter` | done |
| specials / specialairs | SpecialS, SpecialAirS | `ftDe_SpecialS` -> `ftDe_SpecialS_Enter` | done |
| specialhi / specialairhi | SpecialHi, SpecialAirHi | `ftDe_SpecialHi` -> `ftDe_SpecialHi_Enter` | done |
| speciallw | SpecialLw | `ftDe_SpecialLw_Enter` | done |
| specialairlw | SpecialAirLw | `ftDe_SpecialAirLw_Enter` | done |
| onitempickup / onitemcatch | OnItemPickup / OnItemCatch | `ftDe_OnItemPickup` | done |
| onmakeiteminvisible / onmakeitemvisible | OnSetItemInvisible / OnSetItemVisible | `ftDe_OnItemVisibility` (both identical in m-ex) | done |
| onitemdrop / onunknownitemrelated | OnItemRelease / OnUnknownItemRelated | `ftDe_OnItemRelease` | done |
| onhit | EyeTextureDamaged | `ftDe_EyeTextureDamaged` | done |
| onunknowneyetexturerelated | EyeTextureNormal | `ftDe_EyeTextureNormal` | done |
| onframe | OnFrame (empty) | `ftDe_OnFrame` | done |
| onactionstatechange | OnActionStateChange | `ftDe_OnActionStateChange` | done |
| onrespawn | ResetAttributes | `ftDe_ResetAttributes` | done |
| enterdoublejump | EnterDoubleJump | `ftDe_EnterDoubleJump` | done |
| (all other slots) | not exported | NULL | n/a |

### move_logic states (Anim / IASA / Phys / Coll each)

341-344 JumpAerialF1..F4 use the common `ftCo_JumpAerialF1_*` (as m-ex and Kirby do).
Every Dedede state below has all four callbacks: done.

- SpecialN: Start, Loop, End, Grab, GrabItem, Eat, EatWait, EatTurn, Spit, SpitItem, EatWalk (355-357), EatJump1, EatJump2, EatLanding; SpecialAirN: Start, Loop, End, Grab, GrabItem, Eat, EatWait, EatTurn, Spit, SpitItem.
- SpecialS, SpecialAirS.
- SpecialHi_Start (373/374), Jump, Loop, Turn (377/378), Landing (379/380), Hit.
- SpecialLwStart, SpecialLw (383/384), Hold (385/386), Turn, Walk, JumpSquat, Jump, Fall, Landing, SpecialAirLwStart, SpecialAirLw (394/395).

### Helper routines (all done)

SpecialN_EnterAirOrGround, SpecialNLoop_Enter, SpecialAirNLoop_Enter, SpecialN_OnVictim,
SpecialN_OnItem, SpecialNGrab_Enter, SpecialAirNGrab_Enter, SpecialNEnd_Enter, SpecialAirNEnd_Enter,
ASID_CaputureKirbyWait_InterruptCB, every *_PassLedgeCB / *_CollPassLedge / *_CollTransition /
*_Trans ground-air hand-off, SpecialNEatWait_Enter, SpecialAirNEatWait_Enter,
SpecialNEatWait_CheckSpit, SpecialN_ASWalk, SpecialNEatTurn_Enter, SpecialNEatJump1_Enter,
SpecialNEatJump2_Enter, SpecialNEatLanding_Enter, SpecialNSpit_CheckToSpawnStarSpit,
SpecialNSpitItem_CheckToSpawnStarSpit, SpecialN_GetInhaleOffset, SpecialN_CaptureThinkAccessory,
Fighter_KirbyResetScale, AS_ItemGrabbed, ItemGrab_Accessory, Dedede_CheckIfEatWait,
CaptureWait_CheckJumping, Dedede_CheckEnterSpecialAirN_EatWait, Dedede_UseStopWalkMomentum,
AS_EnterStarSpitState, ItemSpawn_StarSpit, ThrownStar_Phys, ThrownStar_Coll, AS_StarSpitEnd,
Thrown_Phys, StarSpitEnd_AccessoryCB, SpecialS_Enter, SpecialS_SetupStateCallbacks,
SpecialS_GordoThink, Dedede_HeldGordoOnHit, SpecialS_SpawnGordo, Gordo_EnterThrown,
SpecialS_PassLedge, SpecialAirS_TouchGround, SpecialHi_Enter, SpecialHiJump_Enter,
SpecialHiLoop_Enter, SpecialHiTurn_Enter, SpecialHiHit_Enter, SpecialHiLanding_Enter,
SpecialHi_SpawnStar, SpecialLw_SetupStateVars, SpecialLw_InitCallbacks, SpecialLw_SpawnEffect,
SpecialLwHold_Enter, SpecialLwStart_PassLedge, SpecialLwUpdateHitboxData, SpecialLw_PassLedge,
SpecialLwWalk_Enter, SpecialLw_Enter, SpecialLwTurn_Enter, SpecialLwJumpSquat_Enter,
SpecialLw_IncrementCharge, SpecialLwFall_Enter, SpecialLwJump_Enter, SpecialAirLw_Enter,
SpecialLanding_Enter, SpecialAirLwStart_TouchGround, SpecialAirLw_TouchGround,
Fighter_IgnoreGroundIndex (inlined as `mpUpdateFloorSkip`).

The 4-byte m-ex symbols `zz_00dfec8_`, `FighterActionStateChange`, `AS_Walk`,
`Jump_Ground_ShortHopOrFullHop_CheckApply`, `Fighter_CollGround_LandingCheck`,
`Fighter_PlaySFXType3`, `Fighter_GetSomeCollisionFlag`, `ECB_IgnoreGroundIndex` are pointers to
game functions; they are direct calls here (`ftWalkCommon_800DFEC8`, `Fighter_ChangeMotionState`,
`ftWalkCommon_800DFCA4`, `ftCo_800CB110`, `ft_80082D40`, `ft_80088510`, `ftKb_SpecialN_800F597C`,
`mpUpdateFloorSkip`). Absolute calls in the code: `ftWalkCommon_800DFDDC`, `ftWalkCommon_800E0060`,
`ftCo_KneeBend_Check_ShortHop`, `it_8026C220`, `it_8026DFB0`, `it_8027781C`, `it_803F9450` (table).

### itFunction (articles)

| Article | m-ex routines | native | status |
|---|---|---|---|
| 0 star model | none (model only, used as the spat fighter's accessory) | n/a | n/a |
| 1 spit star | item_state_table (3 states), OnUnk3, State0 Anim/Phys/Coll, State1 Anim/Phys/Coll | `itDe_SpitStar_*`, `itDe_SpitStar_Logic` | done |
| 2 Gordo | item_state_table (3 states), OnDestroy, OnGiveDamage, OnTakeDamage, OnReflect, OnHitShieldBounce, OnHitShieldDetermineDestroy, OnUnk3, GordoSpawn_AnimCB, State0 Anim/Phys/Coll, StateDeath Anim/Phys/Coll, Gordo_OnDestroy, StepValue, Dedede_RemoveHeldGordo | `itDe_Gordo_*`, `itDe_Gordo_Logic`, `ftDe_SpecialS_ClearHeldGordo` | done |
| 3 Up B star | item_state_table (1 state), OnGiveDamage, OnReflect, OnHitShieldBounce, OnHitShieldDetermineDestroy, OnUnk3, State0 Anim/Phys/Coll | `itDe_HiStar_*`, `itDe_HiStar_Logic` | done |

Rough total: 100% of the exported and internal routines written, 0% run.

## What the integration layer must provide (requests; `mu_ak_fighter.h` untouched)

1. **m-ex services** declared in `ftDe.h`:
   - `void mu_ak_index_article(FighterKind kind, Article* article, int index)` = m-ex
     `0x803D7058`, called from OnLoad for `ftData->x48_items[0..3]`.
   - `ItemKind mu_ak_article_kind(HSD_GObj* fighter_gobj, int index)` = m-ex `0x803D7088`, the item
     kind article `index` of this fighter was registered as (used for articles 1, 2, 3).
   - m-ex `0x803D706C` (zeroed alloc) is done locally with `HSD_MemAlloc` + `memset`.
2. **Article item code**: `ItemLogicTable* const ftDe_ArticleLogic[4]` (index 0 NULL). The kinds
   from (1) must dispatch to these tables (the m-ex itFunction equivalent). Suggest adding an
   `articles` / `article_count` pair to `MuAkFighter` so every fighter can hand these over.
3. **Item callback signatures**: `onitempickup`, `onitemcatch`, `onitemdrop`,
   `onunknownitemrelated` are `void (HSD_GObj*, bool)` cast to `MuAkEvent`; call them with the
   game's bool like the ftData item tables.
4. **Slot meanings**: m-ex put OnRespawn in `ondeath`, OnDestroy in `onunknown` and
   ResetAttributes in `onrespawn`; wire each slot to the same game hook m-ex does.
5. **Attribute backup buffer**: `fp->dat_attrs_backup` must hold 0x17C bytes (OnLoad copies the
   disc block there and points `dat_attrs` and `x2D0` (+0x148) into it).
6. **Fighter/motion var space**: `ftDe_FighterVars` lives in `fp->u`, `ftDe_MotionVars` in
   `fp->mv` (static asserts check the size). Nothing outside this folder reads them.
7. **Data from the disc**: animations 295..341 for the move_logic states and 305..307 via
   `ftData_80085E50` (EatWalk lengths); `ftData->x48_items[4]` is a color-overlay table
   (`ftDe_ColAnims`) used by OnActionStateChange and the Down B charge; article 0's model joint is
   the spat-fighter star accessory.
8. **Effects and sounds** the code spawns by id: effect 0x1770 (inhale, `efSync_Spawn`), 0x1777 and
   0x1778 (jet hammer flame / full charge, `efAsync_Spawn`), 0x49E (Up B ceiling bonk); sound
   0x13A1 (`ft_80088510`). 0x1770+ are m-ex fighter effect ids, so the m-ex effect bank for
   Dedede must be loaded.
9. **Kirby copying Dedede** is `dedede_kirby.c` (the kbFunction of `PlKbCpDe.dat`). The other
   side of his inhale is this folder's `ftDe_SpecialNCapture.c`, which asks who inhaled
   (`ftDe_CaptorAttrs`): the hat file carries those routines again with the values read from
   the hat data. Articles 4 and 5 (`ftDe_ArticleLogic`) are the hat file's. See
   `run-source/rel09-b1-wolf/kirby/DEDEDE.md`.

## Deviations from the m-ex code (deliberate, all small)

- `AS_ItemGrabbed` writes the Dedede gobj to item+0x4 (the item's own `entity` back pointer).
  Retail Kirby (`it_802F23EC`) writes `atk_victim` (+0xD04); native follows retail.
- `AS_ItemGrabbed` stores the item's scale at item+0xFC8..0xFD3, 8 bytes past the end of the
  0xFCC-byte Item, and the hold time at +0xFC4. Native keeps the scale in `kirby2f23.x1E8` and the
  time in `kirby2f23.x1F4`; `ItemGrab_Accessory` reads them from there.
- `Gordo_OnDestroy` reads the owning Dedede from item+0xFCC, also past the end of the Item; the
  Dedede code never writes it, so the m-ex runtime must. Native stores the Dedede gobj in the
  Gordo's item vars (`itDe_GordoVars.dedede`, ip+DDC) at spawn. **Verify** against a running
  Akaneia build that +0xFCC really is the spawning fighter.
- `SpawnItem.hold_kind` and `x10` are left uninitialised by m-ex; zeroed here.
- `SpecialHiHit_Enter` calls `ft_80082D40` through a one-argument pointer, so its float argument
  is whatever was in f1; the callee ignores it and native passes 0.
- m-ex stores `ftCommon_GrabMash`'s result as a float (1.0/0.0) in the captured fighter's
  `capturekirby.x18`; native stores the bool.
- JObj scale / rotation writes go through the decomp inlines (`HSD_JObjSetScale`,
  `HSD_JObjSetRotationY`, `HSD_JObjSetTranslate`), which skip the dirty call for
  `JOBJ_MTX_INDEP_SRT` joints where m-ex always calls `HSD_JObjSetMtxDirtySub`.
- SpawnGordo checks the spawned item for NULL (m-ex does not).

## Uncertain behaviour kept as found (check in Dolphin with the Akaneia disc)

- **SpecialLw_Coll** (the grounded swing) uses the air collision `ft_80082C74` with a "fall"
  callback that enters SpecialAirLw; SpecialAirLw lands back into SpecialLw. On flat ground this
  may flip between the two every frame. Kept as found.
- **SpecialAirNSpitItem landing** names state 370 (itself) instead of 354, so it stays in the air
  version on the ground.
- **SpecialNStart / SpecialNLoop falling** both go to SpecialAirNStart at the current frame (Loop
  does not go to SpecialAirNLoop) and re-arm the grab with the *ground* grab callback;
  SpecialNGrab_Coll corrects that a frame later.
- **Gordo_EnterThrown** picks the toss from `input.lstick[1].y` (the buffered previous stick),
  not `lstick[0]`.
- **SpecialHiHit_Enter** forces `facing_dir = +1`.
- **SpecialHi_Enter** calls `ftCommon_8007D5D4` (goes airborne) even from the ground; the two
  Start states are the facing-left / facing-right versions, not ground / air.
- **Item_8026AB54** gets `da+108` (65) as the Fighter_Part to hold the Gordo on, a raw parts
  index as m-ex passes it.
- The two HSD_MemAlloc buffers at fp+2234 / fp+2238 (0x30 and 0xC0 bytes) are allocated at load
  and freed at destroy but never used.
- `AS_StarSpitEnd` runs Kirby's hat-loss routines when the spat fighter is Kirby with
  `thrownkirby.x18_b1` set; it does not check `u.kb.hat.x8_b0` like the retail version.

## Attribute map (da+ offsets, values from the disc)

Inhale: +000 min loop frames (40), +004/+008 mouth offset (5.4, 3.4), +00C/+010 shrink distance
and amount (7.4, 0.45), +014/+018 max pull per frame (0.9, 1.2), +01C grab range (1),
+020 mash (12), +024 hold decay (1), +028 hold time (250), +02C/+030/+034 star mash, decay, time
(12, 1, 30), +03C star-end time (4), +040 turn stick (0.2), +048 walk speed (0.6), +04C jump
height (0.6), +050 struggle speed (0.6), +054 star speed (4), +058 star decel (0.13),
+068/+06C star-end velocity, +080..+094 star collision box.
Up B: +098..+0A0 start drift, +0A4 start vel y, +0A8 stick range, +0AC/+0B0 leap velocity,
+0B4 rise decay, +0B8/+0BC fall terminal/gravity, +0C0 rise min, +0C4 min frames before dive,
+0C8..+0D4 dive fall and drift, +0D8 fall mobility, +0DC landing lag, +0FC landing star offset.
Side B: +108 Gordo bone (65), +10C smash window (3 frames).
Down B: +110 turn stick, +118/+11C walk accel/max, +120..+12C air drift, +138 charge frames (120),
+13C damage per charge frame, +140 self damage (1.0), +144 self damage interval (30).
+148: multi-jump stats (`Fighter_x2D0_t`, 4 jumps from state 341).
Gordo article: lifetime 100, toss speeds/angles forward (2.1, 55 deg), down (1.65, 50 deg),
up (2.4, 80 deg), cruise 0.25, smash x1.25, spin 65..150 deg/frame.
