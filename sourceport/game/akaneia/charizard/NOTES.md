# Charizard (Akaneia "Lizardon", PlLz.dat), native

Hand-written C from the m-ex code in `PlLz.dat` on the Akaneia v1.0.1 disc. The file carries its
own debug symbol table (132 names), so every routine below is named after its console symbol
(`PlLz <name>` in the comments). Nothing here was built into `melee_game.dll` or run; every file
passes a `-fsyntax-only` check with the game's GCC flags.

What Charizard is made of:

- **Neutral B, Flamethrower**: Bowser's Fire Breath (`ftkoopaspecialn.c`) move for move, with its
  own flame item. The flame is a copy of Bowser's flame item (`itkoopaflame.c`).
- **Side B, Flame Wheel**: a roll with start, roll and end states, ground and air.
- **Up B, Fly**: a special-fall rise with stick drift and one turn-around.
- **Down B, Rock Smash**: a held rock item that breaks into a rock burst item.
- **Two midair jumps**: the multi-jump code Kirby and Jigglypuff use (`fp->can_multijump`,
  `fp->x2D0`).
- **Tail flame**: a GObj proc that spawns the tail flame effect every frame.

## Files

| File | Contents |
|---|---|
| `ftlizardon.h` | Attribute block (0xB0, `DISC_STRUCT`), fighter/motion variable overlays, state and animation enums, motion flags, m-ex service prototypes, all prototypes |
| `charizard.c` | `mu_ak_charizard` (the `MuAkFighter`) and the `move_logic` table (states 341-360) |
| `ftlz_init.c` | onload, ondeath, onunknown, item events, eye texture events, onframe, onrespawn, enterdoublejump, tail flame proc |
| `ftlz_jumpaerial.c` | Midair jump states 341-342 |
| `ftlz_specialn.c` | Flamethrower, states 343-348, the refuel and the flame spawn |
| `ftlz_specials.c` | Flame Wheel, states 353-360 |
| `ftlz_specialhi.c` | Fly, states 349-350 |
| `ftlz_speciallw.c` | Rock Smash, states 351-352 |
| `itlizardon.c` | The three items (Fire, Rock, RockBurst): spawn, state tables, logic tables |

## Routines

The source is 23 `ftFunction` exports, the 20-state `move_logic` (80 callbacks), about 25 internal
helpers, and three item modules from `itFunction`. **All of it is written: roughly 100% of
PlLz.dat.** The one piece of Charizard code missing is Kirby's copy ability, which lives in
another file (see Missing).

### ftFunction exports

| m-ex slot | Console symbol | C function | Status |
|---|---|---|---|
| onload | OnLoad | `ftLz_Init_OnLoad` | done |
| ondeath | OnRespawn | `ftLz_Init_OnDeath` | done |
| onunknown | OnDestroy (empty) | `ftLz_Init_OnUserDataRemove` | done |
| move_logic | move_logic | `ftLz_MotionStateTable` | done |
| specialn / specialairn | SpecialN / SpecialAirN | `ftLz_SpecialN_Enter` / `ftLz_SpecialAirN_Enter` | done |
| specials / specialairs | SpecialS / SpecialAirS | `ftLz_SpecialS_Enter` / `ftLz_SpecialAirS_Enter` | done |
| specialhi / specialairhi | SpecialHi / SpecialAirHi | `ftLz_SpecialHi_Enter` / `ftLz_SpecialAirHi_Enter` | done |
| speciallw / specialairlw | SpecialLw / SpecialAirLw | `ftLz_SpecialLw_Enter` / `ftLz_SpecialAirLw_Enter` | done |
| onitempickup | OnItemPickup | `ftLz_Init_OnItemPickup` (gobj, bool) | done |
| onmakeiteminvisible | OnSetItemInvisible | `ftLz_Init_OnItemInvisible` | done |
| onmakeitemvisible | OnSetItemVisible | `ftLz_Init_OnItemVisible` | done |
| onitemdrop | OnItemRelease | `ftLz_Init_OnItemDrop` (gobj, bool) | done |
| onitemcatch | OnItemCatch | `ftLz_Init_OnItemCatch` (gobj, bool) | done |
| onunknownitemrelated | OnUnknownItemRelated | `ftLz_Init_OnItemUnknown` (gobj, bool) | done |
| onhit | EyeTextureDamaged | `ftLz_Init_OnKnockbackEnter` | done |
| onunknowneyetexturerelated | EyeTextureNormal | `ftLz_Init_OnKnockbackExit` | done |
| onframe | OnFrame (calls RefuelFire) | `ftLz_Init_OnFrame` | done |
| onrespawn | ResetAttributes | `ftLz_Init_LoadSpecialAttrs` | done |
| enterdoublejump | EnterDoubleJump | `ftLz_Init_EnterDoubleJump` | done |

Every other slot is empty in PlLz.dat and NULL in `mu_ak_charizard`.

### move_logic (all four callbacks of every state are done)

| State | Name | Anim id | Callbacks |
|---|---|---|---|
| 341, 342 | JumpAerialF1/F2 | 0x127, 0x128 | `ftLz_JumpAerial_*` |
| 343-345 | SpecialNStart / SpecialN / SpecialNEnd | 0x129-0x12B | `ftLz_SpecialN*` |
| 346-348 | SpecialAirNStart / SpecialAirN / SpecialAirNEnd | 0x12C-0x12E | `ftLz_SpecialAirN*` |
| 349, 350 | SpecialHi / SpecialAirHi | 0x12F, 0x130 | `ftLz_SpecialHi_*`, `ftLz_SpecialAirHi_*` |
| 351, 352 | SpecialLw / SpecialAirLw | 0x131, 0x132 | `ftLz_SpecialLw_*`, `ftLz_SpecialAirLw_*` |
| 353-356 | SpecialSStart / SpecialS / SpecialSBlown / SpecialSEnd | 0x133-0x136 | `ftLz_SpecialS*` |
| 357-360 | SpecialAirSStart / SpecialAirS / SpecialAirSBlown / SpecialAirSEnd | 0x137-0x13A | `ftLz_SpecialAirS*` |

### Internal helpers (all done)

SpawnTailFire (`ftLz_Init_TailFireProc`), RefuelFire, SpecialN_Loop, SpecialN_SpawnFire,
SpecialS_UpdateRotation, SpecialS_Start_OnEnter, SpecialS_OnEnter, SpecialS_End_OnEnter,
SpecialS/AirS/End enter helpers, SpecialHi_OnEnter, SpecialHi_OnLand, SpecialLw_DestroyRock,
SpawnItem_Fire (`itLzFire_Spawn`), Flame_Init (`itLzFire_Setup`), Phys (`itLzFire_Phys`),
ClampRotation (the decomp's `Item_ClampAngle`), Flame_ECBUpdatePosition (a pointer to
`it_8026D9A0`, called directly), SpawnItem_Rock (`itLzRock_Spawn`), SpawnItem_RockBurst
(`itLzRockBurst_Spawn`), and the fire effect tables. The effect tables match Bowser's
`ftKp_Init_803CF2A0`.

### Items (itFunction, all done)

| # | Item | State callbacks | Logic slots |
|---|---|---|---|
| 0 | Fire | anim `itLzFire_Anim`, phys `itLzFire_Phys`, coll `itKoopaFlame_UnkMotion0_Coll` (Bowser's own function, which the console table points at) | dmg_dealt returns false, hit_shield returns false |
| 1 | Rock | anim: destroy unless the owner is in state 351/352, empty phys, coll returns false | destroyed clears the owner's rock, picked_up re-enters state 0 |
| 2 | RockBurst | anim `it_80273130`, empty phys, coll returns false | destroyed spawns effect 0x177B, hit_shield returns true |

`mu_ak_charizard_item_logic[3]` holds these as `ItemLogicTable`s, in m-ex itFunction order.

## What the integration layer must provide

1. **`mu_ak_mex_index_fighter_item(FighterKind kind, void* article, int index)`**: the native
   version of m-ex `MEX_IndexFighterItem` (console 803D7058). OnLoad calls it for the three
   articles in `ftData->x48_items`. It is m-ex's version of vanilla's
   `it_8026B3F8(article, It_Kind_X)`.
2. **`int mu_ak_mex_get_ft_item_id(HSD_GObj* fighter_gobj, int index)`**: the native version of
   m-ex `MEX_GetFtItemID` (console 803D7088). It returns the `ItemKind` given to item `index` (0
   Fire, 1 Rock, 2 RockBurst). Both functions are declared in `ftlizardon.h`. If the shared layer
   picks other names, rename them there.
3. **Item kinds**: register `mu_ak_charizard_item_logic[i]` as the `ItemLogicTable` for the kind
   handed out for item `i`. `Item_80268B18` must accept those kinds (hold kind, article lookup).
4. **Typed item callbacks**: `onitempickup`, `onitemdrop`, `onitemcatch` and
   `onunknownitemrelated` are `void (*)(HSD_GObj*, bool)` (`Fighter_ItemEvent`). They are stored
   cast to `MuAkEvent` and must be cast back and called with the bool. **Interface change
   request:** give these four slots the `Fighter_ItemEvent` type in `mu_ak_fighter.h`.
5. **Slot mapping**: I assume `ondeath` goes to `ftData_OnDeath`, `onunknown` to
   `ftData_OnUserDataRemove`, `onrespawn` to the per-kind "load special attributes" table, and
   `onhit` / `onunknowneyetexturerelated` to the knockback enter/exit pair. These are m-ex's
   names. The layer's mapping decides this.
6. **Effects (EfLzData.dat)**: 0x1775-0x1778 (flame, plus variant 0..3), 0x1779 (tail flame, from
   attribute +04), and 0x177B (rock burst on destroy). These are m-ex effect ids, passed straight
   to `efSync_Spawn`.
7. **Sounds**: 0x13EF / 0x13F2 / 0x13F5 (flame, high / mid / low reserve) through `ft_80088478`.
   Kirby's copies are 0x50910 / 0x50913 / 0x50916. These are m-ex SSM ids.
8. **Animations**: the states use animation ids 0x127-0x13A of the fighter's own m-ex animation
   table.
9. **Fighter variables**: `ftLz_FighterVars` overlays `fp->u` and `ftLz_MotionVars` overlays
   `fp->mv`; static asserts check both fit. `fv->rock` must be NULL before the first Down B.
   `ondeath` sets it to NULL, so this holds if the layer calls ondeath at spawn, as vanilla does.

## Uncertain or odd behavior (kept as the console does it)

- **Flame spawn flag**: Flame_Init clears console bit 0x8000 of the item word at ip+DC8. Bowser
  clears `flags.x13` (0x1000) and also calls `it_8026B3A8` (`flags.x15`). Charizard does neither.
  In the decomp's `flag32` that bit sits inside the 4-bit field `xF` (value bit 4), and it is
  cleared as such. The bit's meaning is unknown; it could be an m-ex header naming slip.
- **Rock spawn position**: SpecialLw_Enter passes an uninitialised stack `Vec3` as the rock's
  spawn position. `Item_8026AB54` puts the rock in Charizard's hand right after, so the native
  code passes `fp->cur_pos`.
- **Flame angle and speed order**: the flame draws its angle before its speed (Bowser does the
  reverse), so the RNG call order differs from Bowser's. This is kept.
- **Sound cycle**: SpawnFire's `(sfx_cycle + 1) % 12` has an extra `(int) fire_speed >> 31` term in
  the console code. It is 0 because the reserve never goes below `fire_speed_min` (40). It is
  written as a plain `% 12`.
- **Jump drift threshold**: the jump physics drift only when |stick x| is strictly above the
  threshold. Vanilla `ft_80084E1C` uses `>=`. This is kept.
- **Unused Flame Wheel pieces**:
  - The "Blown" states 355 and 359 are in the table, but nothing in the code enters them. Their
    anim callback enters special fall with 30 frames of landing lag.
  - `mv.specials.charged` is only ever 0 (Start's OnEnter clears it), so the colour 0x44 /
    0.75 rate / two-thirds speed variant never runs.
- **Air roll landing**: SpecialAirS's collision ignores the landing result, so the air roll never
  lands by itself. Its end state (360) does land.
- **Fire item reactions**: the fire item has no reflected / clanked / absorbed / shield_bounced /
  evt_unk handlers. Bowser's flame has all five. Those slots are NULL, and the item code has to
  cope with NULL there.
- **Tail flame**: the tail flame proc spawns its effect every visible frame. It skips frames when
  `fp->invisible` or `fp->x2226_b4` is set. Its 5..0 countdown is never read.
- **Attributes pointer**: SpecialHi reads attributes through `fp->dat_attrs_backup` (console
  0x2D8) where the rest uses `dat_attrs`. OnLoad makes them the same pointer, so the C always uses
  `dat_attrs`.

## Missing

- **Kirby's Charizard copy ability**: `PlKbCpLz.dat` carries its own m-ex `kbFunction` and
  `itFunction` code (no debug symbols). `MuAkFighter` has no Kirby slots, so it is not ported. The
  flame code already has the Kirby sound branch (`fp->kind == Ft_Kind_Kirby`), and the copy's
  code will probably reuse this file's flame spawn.
