# Wolf (Akaneia), native

Hand-written C for Akaneia's Wolf, from the PowerPC in `PlWf.dat` (roots `ftFunction` and
`itFunction`; the m-ex build ships debug symbols for every routine, and those names are kept in
brackets in the code, for example `[SpecialAirSMid]`). Nothing here runs or translates PowerPC.

| File | What |
|---|---|
| `wolf.h` | action state enum, the attribute block type, per-state variables, the laser's item variables, prototypes |
| `wolf.c` | attribute block, load/death/item/knockback events, the move table, `mu_ak_wolf` |
| `wolf_specialn.c` | blaster: gun and laser spawning, states 341 and 344 |
| `wolf_specials.c` | Wolf Flash: states 347, 349 to 352 |
| `wolf_specialhi.c` | Fire Wolf: states 353 to 359 |
| `wolf_speciallw.c` | reflector: states 360 to 369 |
| `wolf_items.c` | the laser and the held blaster (both articles' logic tables) |

Every file passes `gcc -fsyntax-only` with the decomp's own flags. Nothing has been built into the
DLL or run.

## Wolf is Fox

Wolf's move table has Fox's exact layout: 35 states (341 to 375) with Fox's animation ids, motion
flags and move ids, so the code uses `ftFx_SM_*` / `ftFx_MF_*`. Four groups of entries still point
at Fox's own functions in Akaneia and do here too: 342, 343, 345, 346 (blaster loop/end), 348 (Fox's
Illusion) and 370 to 375 (side taunt). His attribute block has Fox's layout (`ftFox_DatAttrs`,
0xD4 bytes), his up and down special variables have Fox's layout, and most of his up and down
special logic is Fox's with other numbers. Where Wolf's code does the same as a decomp function,
the decomp function is called (`Fighter_OnItemPickup`, `Fighter_OnKnockbackEnter`,
`ftCommon_8007F76C` for his `[SpecialHi_OnSpin]`, Fox's laser callbacks as the MxDt defaults).
Every real difference from Fox is marked `Fox:` in a comment.

## Status: ftFunction exports

All exports Akaneia's Wolf has are written. Slots he does not export are left NULL, so the
registry fills them with the MxDt default, as m-ex does.

| # | slot | Akaneia name | status | notes |
|---|---|---|---|---|
| 0 | onload | onLoad | done | copies the attribute block, hands articles 0 and 1 to the item code |
| 1 | ondeath | onRespawn | done | `ftParts_80074A4C(gobj, 0, 0)` only |
| 2 | onunknown | OnDestroy | done | empty in Akaneia, kept as an explicit empty function |
| 3 | move_logic | move_logic | done | 35 states, see below |
| 4-11 | specials | SpecialN ... SpecialAirLw | done | |
| 13 | onitempickup | OnItemPickup | done | `(HSD_GObj*, bool)`, stored cast |
| 16 | onitemdrop | OnItemRelease | done | `(HSD_GObj*, bool)`, stored cast |
| 17 | onitemcatch | OnItemCatch | done | same function as 13 |
| 18 | onunknownitemrelated | OnUnknownItemRelated | done | same function as 16 |
| 21 | onhit | EyeTextureDamaged | done | Fox's knockback enter |
| 22 | onunknowneyetexturerelated | EyeTextureNormal | done | Fox's knockback exit |
| 23 | onframe | OnFrame | done | empty in Akaneia, explicit |
| 25 | onrespawn | ResetAttributes | done | copies the attribute block again |
| 32 | enterdoublejump | EnterDoubleJump | done | `ftCo_JumpAerial_Enter_Basic` |

## Status: move_logic

| state | Akaneia name | status |
|---|---|---|
| 341 SpecialNStart | SpecialNStart | done |
| 342, 343 | (Fox's SpecialNLoop / End) | Fox's callbacks, unreachable for Wolf |
| 344 SpecialAirNStart | SpecialAirNStart | done |
| 345, 346 | (Fox's SpecialAirNLoop / End) | Fox's callbacks, unreachable |
| 347 SpecialSStart | SpecialS | done |
| 348 | (Fox's SpecialS) | Fox's callbacks, unreachable |
| 349 SpecialSEnd | SpecialSEnd | done, unreachable from Wolf's own transitions |
| 350 SpecialAirSStart | SpecialAirS | done |
| 351 SpecialAirS | SpecialAirSMid | done |
| 352 SpecialAirSEnd | SpecialAirSEnd | done |
| 353 to 359 | SpecialHi ... SpecialHiBound | done |
| 360 to 369 | SpecialLw ... SpecialAirLwTurn | done |
| 370 to 375 | (Fox's AppealS) | Fox's callbacks |

Every helper the states call is written too (the `.text` routines named `*_Trans`, `*_GFX`,
`Laser_Spawn`, `Gun_Spawn`, `Gun_Destroy`, `SpecialHiLaunch`, `SpecialLwTurn_Spin` and so on).

## Status: articles (itFunction)

| article | item kind (Akaneia) | exports | status |
|---|---|---|---|
| 0 laser | 253 | state table (one state), OnSpawn, OnGiveDamage, OnTakeDamage, OnReflect, onClank, onHitShieldDetermineDestroy | done |
| 1 blaster | 254 | state table (one state), OnPickup | done |

m-ex builds an article's logic table from MxDt's defaults for its item kind (MxDt item section,
entry `kind - 237`) with the fighter's exports on top. `wolf_items.c` holds the merged result:
the laser keeps MxDt's `absorbed`, `shield_bounced` and `evt_unk`, which are Fox's laser functions
(`itFoxLaser_Logic94_*`); the blaster has no MxDt defaults.

Wolf's MxDt item lookup has four kinds (253 to 256) and his `ftData.x48_items` has three
articles. Kinds 255 (MxDt defaults: a copy of Fox's laser table) and 256 (empty) are Kirby's:
the laser and the blaster of the ability he copies from Wolf (`wolf_kirby.c`). Their data is in
Kirby's hat file `PlKbCpWf.dat` and their code is the same as articles 0 and 1, so
`article_count` is 4 and `itWf_Articles` lists the two tables twice.

## Overall

Roughly 100% of the exported code is written (all 21 ftFunction exports, the callbacks of all 24
of Wolf's own states, all helpers, both item tables). 0% is verified at runtime. Two services
below are missing, so the blaster cannot work correctly and the effects will not show yet; the
rest of the kit (Wolf Flash, Fire Wolf, reflector) depends only on decomp functions.

## What the integration layer must provide

1. **The retail item spawner, as is.** m-ex code calls `Item_8026862C` (static in `it/item.c`)
   with a `SpawnItem` it fills itself, hold kind 8 (character item) and ground/air 0. The public
   wrappers (`Item_80268B18` / `Item_80268B5C`) rerun `Item_802674AC`, which gives kinds past the
   retail ones hold kind 5 (stage projectile, with a spawn limit). Wanted:
   `Item_GObj* mu_ak_item_create(SpawnItem* spawn)` that calls `Item_8026862C(spawn)` unchanged,
   and `MU_AK_HAVE_ITEM_CREATE` defined for the Wolf files (for example in `mu_native.h` next to
   the other `mu_ak_*` declarations). Until then `ftWf_CreateItem` (wolf_specialn.c) falls back
   to `Item_80268B5C`, which compiles and links but counts the laser and gun as stage items.
2. **m-ex effect ids.** Wolf spawns `efSync_Spawn` ids 5000 to 5005, which retail `efSync_Spawn`
   does not know. They belong to Wolf's effect file (MxDt effect table: `EfWfData.dat`,
   `effWolfDataTable`; Wolf's per-fighter effect file index in MxDt fighter array 9 is 22). My
   reading is that m-ex treats 5000 + n as entry n of the fighter's own effect file; I have not
   verified that against m-ex's effect hook. The call shapes Wolf uses:
   - 5000 reflector loop, 5001 reflector start, 5002 reflector hit: attached to a joint
     (`HSD_JObj*` vararg, HipN);
   - 5003 Fire Wolf charge: attached (TransN); 5004 Fire Wolf travel: attached (HipN), and Wolf
     sets the returned effect's `update` to `efLib_Cb_SetRotYZ_FromFighter`, so the hook must
     return the `EF_Effect*`;
   - 5005 blaster muzzle: at a position with a rotation (`Vec3*`, `float*` varargs), and Wolf
     writes `rotate.y` of the returned effect's joint, so it must return the `EF_Effect*`.
   Wolf also spawns retail effect 1030 (Fox's bounce), which works as is.
3. **Item event slots.** Slots 13, 16, 17 and 18 hold `void (*)(HSD_GObj*, bool)` stored through
   `MuAkEvent`; the registry already writes them into `Fighter_ItemEvent` tables, which is right.
4. **Side taunt.** Entries 370 to 375 are Fox's `ftFx_AppealS_*`. Whether a Wolf kind reaches them
   depends on how `ftCo_AppealS.c` picks fighters (it checks for Fox and Falco); m-ex's own path to
   them was not traced.

`flags` is left 0 (not `MU_AK_READY`) because of 1 and 2 and because nothing has run.

## Behavior I was unsure of, or kept although it looks like a slip

- **Attributes come from the code, not the file.** onLoad copies the 0xD4-byte block compiled into
  Wolf's m-ex code ("param_exts") over `fp->dat_attrs`; PlWf.dat's own block is ignored. The two
  are equal except the last word, which only affects the reflect bubble's behavior byte: m-ex's C
  declares it an `int` set to 3, so the byte the game reads is 0. Kept as 0.
- **Wolf Flash wind-up fall cap.** `[SpecialAirS_Phys]` passes attribute +0x60 (0.015, Fire Wolf's
  fall acceleration) as the terminal velocity to `ftCommon_Fall`, where every other state passes
  the common terminal velocity. Kept.
- **The dash ignores the ground.** `[SpecialAirSMid_Coll]` runs the ledge check only while
  airborne and does nothing on landing, so the dash keeps going along the floor. Kept.
- **Fire Wolf bounce rule.** Fox bounces off the floor when his ground-travel count reaches x6C or
  he is not dropping through a platform; Wolf bounces only while the count is under x6C (15) and
  he is not dropping through. The count only grows during ground travel. After sliding along the
  floor Wolf also runs the ledge and wall checks, where Fox stops. Kept.
- **Fire Wolf ground travel always lands** into 357 at frame 13, even if Wolf left the ground on
  the last frame. Kept.
- **Unused attribute fields.** The blaster attributes x0 to x20, Wolf Flash x34/x38, Fire Wolf x64/x80
  and reflector xA0 are never read by Wolf's code.
- **ApplyPartAnim's frame.** m-ex calls `ftAnim_ApplyPartAnim` in the knockback events without the
  float argument, so the console passes whatever f1 held. I pass 0.0 as Fox does.
- **Null checks added.** m-ex does not check that the gun was created before attaching, firing
  from or removing it (the console would crash on a failed spawn). The C checks, and also returns
  when the registry has no item kind for the article.
- **Gun hand joint 67.** `Item_8026AB54(gun, gobj, 67)`: 67 is a raw index into `fp->parts`
  (like `FtPart_109`), Wolf's hand joint; `ftLib_80086630` reads it back unremapped.
- **Laser muzzle joint 5.** `lb_80011E24(gun_jobj, &muzzle, 5, -1)`: the gun model's joint 5.
- **`HSD_JObjSetMtxDirtySub`** is called directly (not the checked inline) in two places, as m-ex
  does; and two effect/laser joints get `rotate.y` written without marking the matrix dirty.
- **`bp()`.** m-ex's `bp` breakpoint stub (it resolves to `grLast_8021B2D8`, which returns 0) is
  called in three places; it has no effect and is left out.
- **Spawn padding.** m-ex leaves the `SpawnItem` on the stack uninitialised apart from the fields
  it sets (it clears only bit 0 of `x44_flag`). The C zero-initialises it first.
- **Float order.** Expressions follow the PowerPC's operation order (float vs double steps, the
  multiply order), but the native build may fuse multiply-adds (`-ffp-contract=on`), so results
  can differ from the console in the last bit.

## Not done

- Runtime testing of any kind (the user is playing; no build was run).
- The creation layer (character kind, fighter and animation files, costumes, PlCo data): that is
  the integration layer's list in `../INTEGRATION.md`, not Wolf code.
