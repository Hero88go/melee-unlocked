# Diddy Kong (Akaneia) native port

Source: `PlDd.dat` on the Akaneia v1.0.1 disc (m-ex internal slot `ftDataDiddy`). The file carries
three code roots, all with debug symbols, which is where the routine names below come from:

| root | contents | size |
| --- | --- | --- |
| `ftFunction` | 23 ftFunction exports, `move_logic` (34 states, 341..374), 246 symbols | 0x6138 bytes, 947 relocs |
| `itFunction` [0] | Popgun article: 3-state table, one null callback | 0x38 |
| `itFunction` [1] | Peanut article: state table + 6 callbacks | 0x264 |
| `itFunction` [2] | Banana article: 6 states, 10 callbacks, trip logic, debug draw | 0x10B0 |

The Kirby copy ability lives in a separate file, `PlKbCpDd.dat` (`kbFunction`, no debug symbols,
7 exports incl. its own move_logic, plus 2 copy articles). It is **not ported** (see the end).

Files:

| file | what |
| --- | --- |
| `ftdiddy.h` | motion state / animation ids, attribute block (`ftDd_DatAttrs`, 0x100 bytes, DISC_STRUCT), fighter/motion/banana var overlays, required hooks, all prototypes |
| `ftdiddy.c` | `mu_ak_diddy`, move_logic table, onLoad and the other ftFunction slots, victory pose, CPU spoof, article spawn helper |
| `ftdiddyspecialn.c` | Peanut Popgun (10 states + gun helpers) |
| `ftdiddyspecials.c` | Monkey Flip (9 Diddy states + 6 victim "Taro" states, throw/break release) |
| `ftdiddyspecialhi.c` | Rocketbarrel Boost (7 states used, 1 unused alias) |
| `ftdiddyspeciallw.c` | Banana Peel, fighter side (2 states) |
| `itdiddy.c` | the three articles as `ItemLogicTable ftDd_ItemLogic[3]` |

All six .c files pass `-fsyntax-only` with the project's gcc line (plus `-Wall -Wextra`, clean for
these files). Nothing has been linked or run.

## Status per routine

"done" = written by hand from the disassembly and cross-checked line by line against it.

### ftFunction slots (MexTK/ftFunction.txt order)

| slot | PlDd.dat symbol | native | status |
| --- | --- | --- | --- |
| onload | onLoad | `ftDd_Init_OnLoad` | done |
| ondeath | onRespawn (sic) | `ftDd_Init_OnDeath` | done (ftParts_80074A4C(gobj,0,0)) |
| onunknown | OnDestroy | `ftDd_Init_OnDestroy` | done (empty on console too) |
| move_logic | move_logic | `ftDd_MotionStateTable[34]` | done |
| specialn / specialairn | SpecialN / SpecialAirN | `ftDd_SpecialN_Enter` / `ftDd_SpecialAirN_Enter` | done |
| specials / specialairs | SpecialS / SpecialAirS | `ftDd_SpecialS_Enter` / `ftDd_SpecialAirS_Enter` | done |
| specialhi / specialairhi | SpecialHi / SpecialAirHi | `ftDd_SpecialHi_Enter` / `ftDd_SpecialAirHi_Enter` | done |
| speciallw / specialairlw | SpecialLw / SpecialAirLw | `ftDd_SpecialLw_Enter` / `ftDd_SpecialAirLw_Enter` | done |
| onitempickup | OnItemPickup | `ftDd_Init_OnItemPickup` | done (= decomp `Fighter_OnItemPickup(gobj, flag, 1, 1)`) |
| onmakeiteminvisible | OnSetItemInvisible | `ftDd_Init_OnItemInvisible` | done |
| onmakeitemvisible | OnSetItemVisible | `ftDd_Init_OnItemVisible` | done |
| onitemdrop | OnItemRelease | `ftDd_Init_OnItemDrop` | done |
| onitemcatch | OnItemCatch | `ftDd_Init_OnItemCatch` | done (calls pickup) |
| onunknownitemrelated | OnUnknownItemRelated | `ftDd_Init_OnItemUnk` | done (calls drop) |
| onhit | EyeTextureDamaged | `ftDd_Init_OnKnockbackEnter` | done (= `Fighter_OnKnockbackEnter(gobj, 1)`) |
| onunknowneyetexturerelated | EyeTextureNormal | `ftDd_Init_OnKnockbackExit` | done |
| onframe | OnFrame | `ftDd_Init_OnFrame` | done (per-motion anim rate table at attrs+0xFC) |
| onrespawn | ResetAttributes | `ftDd_Init_OnRespawn` | done |
| enterdoublejump | EnterDoubleJump | `ftDd_Init_EnterDoubleJump` | done (ftCo_JumpAerial_Enter_Basic) |
| all others | (empty in PlDd.dat) | NULL | n/a |

### move_logic (341..374), all done

Every entry: flags `0x340111` (`ftCo_MF_Special | KeepFastFall | SkipThrowException`), move id in
the top byte, camera `ftCamera_UpdateCameraBox`. 366 (`SpecialAirHiDamage2`, anim 0x147) has the
same callbacks as 365 and nothing in the code enters it.

| ids | move | notes |
| --- | --- | --- |
| 341-350 | Special N ground/air Start, Charge, Danger, Blow, Shoot | gun spawn on cmd_vars[0], gun destroy on cmd_vars[1]==1, charge counts while B held |
| 351-359 | Special S Start, Stick, StickAttack, StickAttack2, StickJump, StickJump2, AirStart, AirSJump, AirSKick | grab armed in AirSJump via ftCommon_8007E2D0 |
| 360-366 | Special Hi Start, Charge, AirStart, AirCharge, AirJump, AirDamage(x2) | |
| 367-368 | Special Lw ground/air | |
| 369-374 | Taro (victim) states: StickWait, StickJump, StickAttack, ground/air | run on the caught fighter |

### Internal routines

Special N: `SpecialN*_Enter/_Trans` (all), `Gun_Spawn`, `Gun_Destroy`, `Gun_ChangeModel`,
`Gun_Shoot` - done.
Special S: `SpecialS*_Enter/_Trans`, `SpecialAirSFall_Enter`, `SpecialSStick_OnVictimGrabbed`,
`SpecialSStick_UpdatePos`, `SpecialS_ThrowInit`, `SpecialS_ThrowDetach`,
`SpecialS_BreakDetach`, `SpecialS_ThrowGroundCorrect`, `Fighter_TaroStateChange`, all six Taro
`_Enter`/`_Trans` - done.
Special Hi: `SpecialHi_Init`, `SpecialHi*_Enter/_Trans`, `Blend_ChargeAnimation`,
`SpecialAirJump_ApplyDrift`, `SpecialHi_OnLand`, `SpecialAirCharge_Damaged` - done.
Special Lw: `Banana_Spawn`, `Banana_Release`, `Banana_OnHit`, `SpecialLw*_Trans` - done.
Victory pose: `Diddy_WinAccessory`, `DiddyWin1_AnimCB`, `DiddyWin_SpawnGun`,
`DiddyWin1_Shoot` - done.
CPU: `MexCPU_InitSpoofData`, `MexCPU_InitProc`, `MexCPU_ProcSpoof`, `MexCPU_Process` - done
(spoof path). `MexCPU_ProcCustom` - **not ported, unreachable**: it only runs when the file's
`.bss mexcpu_data` pointer is non-NULL, and nothing in PlDd.dat writes it.
`Fighter_ApplyAnimation2`, `Fighter_ResetAnimationTransforms`, `Fighter_BlendAnimation` are not
code but three function pointer words holding ftAnim_8006EDD0 / ftAnim_8006FF74 /
ftAnim_8006FE9C; called directly.

### Articles (itdiddy.c), all done

Popgun: 3 states with `Null_Func` callbacks. Peanut: `OnSpawn`, `OnGiveDamage`, `OnTakeDamage`,
`OnReflect`, `onClank`, `onHitShieldDetermineDestroy`, `Nut_Anim/Phys/Coll`. Banana: `OnSpawn`,
`OnDestroy`, `OnPickup`, `OnDrop`, `OnThrow`, `OnGiveDamage`, `OnTakeDamage`, `OnReflect`,
`OnHitShieldBounce`, `OnHitShieldDetermineDestroy`, the 6 state triples, `SpawnThrown_Enter`,
`SpawnThrown_OnLand`, `Fall_Enter`, `Wait_Enter`, `Thrown_OnLand`, `FlyUp_Enter`,
`Trip_Check`, `Fighter_CanTrip`, `Trip_Grabbed`, `Trip_Enter`, `Trip_Anim/IASA/Phys/Coll`,
`Trip_CorrectModel`, `Banana_GX` (model draw plus the develop-mode trip-area overlay, drawn with
the decomp's GX calls).

Rough completion: fighter 100% of PlDd.dat's reachable code, articles 100%, Kirby copy 0%.
Overall including the Kirby file about 90% by routine count.

## Behavior notes and deliberate choices

1. **Grab mask calls do nothing (faithful).** Every PlDd.dat call to ftCommon_8007E2F4
   (MexTK `Fighter_SetGrabbableFlag`) passes the GObj, not the Fighter (MexTK's prototype
   error). On console the 16-bit store lands at gobj+0x1A6A, outside the fighter, so the fighter's
   `x1A6A` never changes. The port keeps that (`ftDd_SpecialS_SetGrabMask` is a no-op);
   `-DMU_AK_DIDDY_FIX_GRAB_MASK` applies the intended `fp->x1A6A = val`. Note the peel's trip
   check does read `x1A6A`, so the fix would change gameplay.
2. **Taro states.** Victims enter Diddy's states by temporarily overwriting common row
   `ftCo_MS_CaptureCaptain` (275) of the shared `ftData_MotionStateList` with Diddy's row, calling
   `Fighter_ChangeMotionState(victim, 275, ..., thrower)` and restoring it. The victim's
   motion_id reads 275 while in them. The frame/speed/blend the Trans callers pass are ignored on
   console (always 0/1/0) and in the port. Needs `x20_actionStateList` of Diddy to be this
   table and `x18` = 341 (the integration layer's normal setup).
3. **Trip DownBound.** Trip_Anim calls `ftCo_8009794C`, which is `static` in the decomp. The port
   calls its public wrapper `ftCo_80097D88`, identical except for a Sandbag being tripped (the
   wrapper would take the sandbag path).
4. **CPU.** The spoof runs `ftCo_800B3900` with `fp->kind = Ft_Kind_Mario`. m-ex's copy skips
   `ftCo_800B0AF4` (Ice Climbers partner sync), a no-op for a lone fighter.
5. **SpawnItem** fields x45..x47 are stack garbage on console; the port zeroes the struct.
6. **NULL owner guards** added in `Trip_Check` and `Banana_GX` (console would fault).
7. **Ceiling bounce effect height** reads coll_data+0xA0 (fp+790), which the decomp names
   `desired_ecb.left.y`. Kept as on console.
8. Floating point: double-precision steps of the console (0.4, 0.8, pi constants, the flight
   speed squared) are kept as double in C.
9. `SpecialSStick_Anim` calls `ftAnim_IsFramesRemaining` and ignores the result (console too).
10. `SpecialNShoot_Anim` also calls m-ex `bp()` (empty debugger hook at grLast_8021B2D8); dropped.

## Assets the port needs (data, loaded by the m-ex data layer)

- Articles: `ftData x48` of PlDd.dat, 3 entries (popgun, peanut, banana). They must be indexed
  as m-ex item kinds (see hooks). Hold kinds passed at spawn: 8 (popgun, peanut), 7 (banana).
- Effects: **6002** (peanut pop, from `EfDdData.dat`, an m-ex effect id; vanilla efSync_Spawn
  has no case for it), 219 (0xDB, `attrs.specialhi_gfx_id`, barrel trail) and 234 (0xEA,
  ceiling bonk) are vanilla generators (< 0x250), 0x406 via efAsync (vanilla).
- Sounds: 0x13A1 (banana pull, Diddy's m-ex SSM `audio/us/diddy.ssm`), 0x10A and the fighter's
  own `x4C_sfx->x18` for the trip (vanilla). Other sounds are in the animation scripts.
- No native code is needed for effects or sounds beyond the m-ex id mapping.

## Hooks the integration layer must provide

Declared in `ftdiddy.h` (not defined here; the link needs them):

```c
/* MEX_IndexFighterItem (console 803D7058). onLoad registers the 3 articles. */
void mu_ak_index_fighter_item(FighterKind kind, void* article, int index);
/* MEX_GetFtItemID (console 803D7088): ItemKind of article `index` of this fighter. */
ItemKind mu_ak_fighter_item_kind(HSD_GObj* fighter_gobj, int index);
/* The decomp's static Item_8026862C (create without Item_802674AC's hold-kind rewrite),
 * extended to m-ex article kinds (render func + ItemLogicTable lookup). */
Item_GObj* mu_ak_item_create(SpawnItem* spawn);
```

Also required from the integration layer:

1. **Item logic registration**: `ItemLogicTable ftDd_ItemLogic[3]` (ftdiddy.h) must become the
   `xB8_itemLogicTable` / state table of the three article kinds. `MuAkFighter` has no field for
   it; suggested interface addition: `const ItemLogicTable* items; int item_count;`.
2. **Item callback signature**: `onitempickup`, `onitemdrop`, `onitemcatch`,
   `onunknownitemrelated` are `void (*)(HSD_GObj*, bool)` in the decomp (ftData_OnItem* tables)
   but `MuAkEvent` has one argument. `mu_ak_diddy` stores them cast to `MuAkEvent`; the layer must
   call them through the two-argument type (or the header should grow a `MuAkItemEvent` type).
3. **Fighter/motion var room**: `ftDd_FighterVars` (with a pointer) and `ftDd_MotionVars` are
   overlaid on `fp->u` / `fp->mv`; static asserts check they fit.
4. **Effect id 6002** (EfDdData.dat) must resolve through efSync_Spawn.
5. **Result screen**: onLoad detects the demo fighter by `fp->x18 == 14`, replaces
   `x1C_actionStateList` with a heap copy of the 14 demo rows (row 0 anim callback =
   victory-pose shooting) and sets `fp->x21EC` (accessory callback that spawns the two guns
   when motion 0 starts). The demo path must call onload for Diddy.
6. **CPU**: onLoad installs a proc that swaps the fighter's `Fighter_procCpu` for the Mario
   spoof. If the layer handles AI for all Akaneia kinds itself, this is still harmless (it only
   swaps the proc whose callback is `Fighter_procCpu`).

## Not done

- **Kirby copy ability** (`PlKbCpDd.dat`: onkirbyswallow, onkirbyloseability, kirbyspecialn,
  kirbyspecialairn, onkirbyhurt, initcopyitems, move_logic, 2 article tables). No debug symbols;
  MuAkFighter has no Kirby fields either. Swallowing Diddy with Kirby needs this or a fallback.
- `MexCPU_ProcCustom` (unreachable, see above).
