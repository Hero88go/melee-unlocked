# Akaneia Lucas, native

Hand-written C for the behavior that PlLc.dat on the Akaneia v1.0.1 disc ships as PowerPC:
the m-ex `ftFunction` blob (root `ftFunction`, 0x7DC8 bytes of code, 233 debug symbols, 28
exports) and the `itFunction` table (10 article slots, slot 2 empty). Every routine below was
read from the disassembly and rewritten against the decomp's own types and functions. Offsets
in the source comments (`code+0x...`, `+0x...` for items) point back into those blobs.

Nothing here has been built into the DLL or run. Every file passes the project gcc line with
`-fsyntax-only -Werror=implicit-function-declaration -Werror=incompatible-pointer-types
-Werror=int-conversion`. Console struct offsets were confirmed with the same headers compiled
`-m32` (`offsetof` probes), not guessed.

## Files

| file | contents |
| --- | --- |
| `lucas.h` | motion state ids, attribute block (0x13C, DISC_STRUCT), rope structs, fighter/motion var overlays, integration hooks, cross-file prototypes |
| `lucas.c` | `mu_ak_lucas`, `move_logic` table (30 states), OnLoad/OnRespawn/OnDestroy, item hooks, knockback eye hooks, double jump, tether entry, intro/taunt/grab exports, GX link |
| `lucas_cpu.c` | m-ex MexCPU: CPU Lucas runs the vanilla CPU with fp->kind spoofed to Ness |
| `lucas_attacks4.c` | forward smash (stick, reflector) |
| `lucas_specialn.c` | PK Freeze, fighter side |
| `lucas_specials.c` | PK Fire, fighter side |
| `lucas_specialhi.c` | PK Thunder, PK Thunder 2, wall/ceiling bounce |
| `lucas_speciallw.c` | PSI Magnet (and the unreachable turn states) |
| `lucas_aircatch.c` | Rope Snake tether: verlet rope, ledge search, hang/reel/climb, rope renderer, grab wall rebound |
| `lucas_it_pkfreeze.c` | article 0, PK Freeze projectile |
| `lucas_it_pkfire.c` | article 1, PK Fire bolt |
| `lucas_it_pkthunder.c` | article 3 head, articles 4-7 tail segments |
| `lucas_it_misc.c` | article 8 stick, article 9 snake, their spawners, `ftLc_ArticleLogic[10]` |

## Routine status

Status: **done** = rewritten in full; **partial** = rewritten with a stated gap;
**not ported** = deliberately left out.

### ftFunction exports

| m-ex slot | disc symbol | C | status |
| --- | --- | --- | --- |
| 0 onload | OnLoad | `ftLc_OnLoad` | done |
| 1 ondeath | OnRespawn | `ftLc_OnRespawn` | done |
| 2 onunknown | OnDestroy | `ftLc_OnDestroy` | done (frees the rope) |
| 3 move_logic | move_logic | `ftLc_MotionStates[30]` | done |
| 4/5 specialn/air | SpecialN/SpecialAirN | `ftLc_SpecialN_Enter`, `ftLc_SpecialAirN_Enter` | done |
| 6/7 specials/air | SpecialS/SpecialAirS | `ftLc_SpecialS_Enter`, `ftLc_SpecialAirS_Enter` | done |
| 8/9 specialhi/air | SpecialHi/SpecialAirHi | `ftLc_SpecialHi_Enter`, `ftLc_SpecialAirHi_Enter` | done |
| 10/11 speciallw/air | SpecialLw/SpecialAirLw | `ftLc_SpecialLw_Enter`, `ftLc_SpecialAirLw_Enter` | done |
| 12 onabsorb | OnAbsorb | `ftLc_SpecialLw_OnAbsorb` | done |
| 13 onitempickup | OnItemPickup | `ftLc_OnItemPickup` | done |
| 14 onmakeiteminvisible | OnSetItemInvisible | `ftLc_OnItemInvisible` | done |
| 15 onmakeitemvisible | OnSetItemVisible | `ftLc_OnItemVisible` | done |
| 16 onitemdrop | OnItemRelease | `ftLc_OnItemDrop` | done |
| 17 onitemcatch | OnItemCatch | `ftLc_OnItemPickup` | done |
| 18 onunknownitemrelated | OnUnknownItemRelated | `ftLc_OnItemDrop` | done |
| 21 onhit | EyeTextureDamaged | `ftLc_OnKnockbackEnter` | done |
| 22 onunknowneyetexturerelated | EyeTextureNormal | `ftLc_OnKnockbackExit` | done |
| 25 onrespawn | ResetAttributes | `ftLc_ResetAttributes` | done |
| 32 enterdoublejump | EnterDoubleJump | `ftLc_EnterDoubleJump` | done |
| 33 entertether | EnterTether | `ftLc_EnterTether` | done |
| 35 onsmashf | OnSmashF | `ftLc_AttackS4_Enter` | done |
| 41 (no field) | OnIntroL | `mu_ak_lucas_on_intro_l` | done, needs a hook |
| 43 (no field) | OnAppeal | `mu_ak_lucas_on_appeal` | done, needs a hook |
| 44 (no field) | OnCatch | `mu_ak_lucas_on_catch` | done, needs a hook |

Slots 19, 20, 23, 24, 26-31, 34, 36, 37 are empty on the disc (NULL in `mu_ak_lucas`).

### move_logic (341-370)

All 30 states and all 120 callbacks are done: AttackS4 (341), SpecialN Start/Hold/End and air
(342-347), SpecialS/AirS (348-349), SpecialHi Start/Hold/End/PKT2 and air (350-357),
SpecialHiBound (358), SpecialLw Start/Hold/Hit/End/Turn and air (359-368), AirCatch (369),
AirCatchHit (370). Anim ids and the MotionState flag words are the disc's numbers.

### Internal routines (all done)

SpecialN_Init, SpecialS_Enter, Init_SpecialHi, SpecialLw_Init, AirCatch_Enter, SpawnItem_Stick,
SpawnItem_Snake, Common_SpawnSnake, Lucas_RemoveAllArticles, Lucas_CatchAccessory4,
Catch_CheckEnterRebound, Physics_Render, PlayReflectSound, Lucas_RemoveAndDestroyItem, the
SpecialN/SpecialHi `*_Shared` helpers, Lucas_DestroySpecialHiEffects,
SpecialHi_Hold_ProcessAnimTimers, Lucas_CheckForPKTCollision, Fighter_EnterSpecialHi_Grounded /
_Airborne, AS_LucasPKHit, SpecialHi_Anim_CheckSpawnEffect, SpecialHi_CollisionWithAngle,
SpecialHi_CalculateRotationOffWall, ClampRotation, fsign, the twelve SpecialLw_Enter/Switch
helpers and the two CheckChangeState helpers, RetractSnake, Snake_DestroyCB,
Lucas_UpdateTetherPosition, SpecialS_Accessory4_Callback, MassWallCollisionCallback,
Accessory_AirCatch_UpdateSnakePhysics, Accessory_AirCatchHit_UpdateSnakePhysics,
Lucas_RemoveSpecialItemGOBJ, Lucas_RemovePKThunderGOBJ, Lucas_RemoveSmashItemGOBJ,
ItemSpawn_PKFreeze, Init_PKFreeze, SpawnItem_PKThunder, ThunderHead_EnterState,
ThunderHead_GetPosition, Spawn_PKFire, Lucas_UpdateSnakePhysicsModel, Lucas_CheckEnterTether,
Ledge_Find, Enter_AirCatchHit, Lucas_EnableSnakeRagdoll, Physics_SetJOBJRope, Manager_Update,
Manager_UpdateConstrains, Manager_CreateMass, Mass_Update, Spring_Update,
Spring_CalculateLength, Vec3_Distance/DistanceSquared/Length, GXLink_Lucas, Lucas_WinAccessory,
MexCPU_InitSpoofData, MexCPU_InitProc, MexCPU_ProcSpoof, MexCPU_Process (spoof path).

**not ported:** MexCPU_ProcCustom (code+0x5C68) and the custom-table branch of MexCPU_Process.
They only run when `.bss.mexcpu_data` is non-NULL, and nothing in PlLc.dat ever writes it, so
Lucas always takes the spoof path. (They patch `Fighter_804D64FC` entry 32 and call a custom CPU
handler; if another Akaneia fighter sets that table, port it there.)

### Articles (itFunction)

| slot | article | callbacks | status |
| --- | --- | --- | --- |
| 0 | PK Freeze | 3 states, ondestroy, onreflect | done |
| 1 | PK Fire | 2 states, ondestroy, ongivedamage, onreflect, onhitshieldbounce, onhitshielddeterminedestroy | done |
| 2 | (empty) | none | n/a |
| 3 | PK Thunder head | 1 state, ondestroy, ongivedamage, onreflect, onhitshieldbounce, onhitshielddeterminedestroy, tail spawner | done |
| 4-7 | PK Thunder tails | 1 state: Ness's trail anim/coll (the disc points at 0x802AC62C / 0x802AC8A0) + Lucas phys | done |
| 8 | stick | 1 state, onpickup | done |
| 9 | Rope Snake | 8 states, onpickup | done |

Rough total: every routine in both blobs is rewritten (about 100% of the shipped code paths);
the only code not carried over is the unreachable MexCPU custom-table path.

## Behavior kept exactly as shipped (looks like bugs, do not "fix" without a decision)

- **Double jump drift seed** (code+0xCC0): the disc truncates `stick_x * air_jump_h_multiplier`
  to an int and stores its bits in the float `mv.co.jumpaerial.init_h_vel`, which Ness's aerial
  jump physics then reads. So Lucas's double jump drift seed is 0, a denormal, or (for -1) NaN
  bits. Reproduced bit for bit with memcpy.
- **OnSetItemVisible** calls `ftAnim_80070CC4` (the invisible call), not `ftAnim_80070C48`.
- **Lucas_RemovePKThunderGOBJ** cleans up effects for motion ids 359-367 (PSI Magnet) instead of
  350-358 (PK Thunder).
- **SpecialAirLw_Start landing** re-enters the *aerial* start state (ftCommon_8007D5D4 + 364), so
  landing during the aerial start keeps him airborne until the start animation ends.
- **SpecialLw Turn (363/368)** is only entered from its own transitions: dead states. Kept.
- **SpecialAirLw_End landing** (code+0x5354) passes the GObj instead of the Fighter to
  `ftCommon_ClampAirDrift`. On console that clamps a float next to the GObj, never Lucas's
  velocity; natively the call is skipped (Lucas-visible behavior identical, the stray write is
  not reproduced).
- **PK Thunder OnReflect** wraps the reversed angle with an upper bound of pi/2 (its
  `.rodata.cst8+0x10`), not 2pi, so it can come out negative.
- **PK Thunder 2 ground deceleration** keeps the old speed once the step would cross DECEL.
- **PK Thunder head "reflected"** flag: the disc stores int 1 into a float slot and compares it
  with 0.0f. Natively an int. (Only differs if the console ran with denormals flushed.)
- **Physics_SetJOBJRope** allows 22 masses before its assert (the 0x598 block holds 21). Natively
  the arrays have headroom, and the assert (console halt) becomes an OSReport and return.
- **Lucas_EnableSnakeRagdoll** with zero masses writes `masses[-1].gravity`, i.e. the iteration
  count; reproduced as `rope->iterations = 0`.
- **Lucas_UpdateSnakePhysicsModel** walks masses `0..count` inclusive; the extra slot's joint is
  NULL and skipped (arrays sized for it).
- **Common_SpawnSnake** calls `it_80272CC0` on the spawned item even when the spawn failed;
  natively guarded.
- SpawnItem structs are zero-filled natively; the console leaves `hold_kind`, `x10`, `x3E` as stack
  garbage. Believed unused by `Item_80268B18` for these kinds.

## Uncertain behavior

- **Slots 41/43/44** (OnIntroL, OnAppeal, OnCatch): m-ex function indices beyond
  `MexTK/ftFunction.txt`. Names come from the disc's symbols; which game event calls each slot
  (entrance, taunt start, grab state entry) must match what the integration layer hooks. OnCatch
  expects to run on entering Catch (212) / CatchDash (214); OnAppeal on taunt start; OnIntroL on
  the entrance (it stores the snake in `fp->item_gobj`).
- **parts[139]**: Common_SpawnSnake overwrites `fp->parts[139].joint` with the snake head joint.
  This assumes Lucas's skeleton has at least 140 bones (the console does the same unchecked).
- `efSync_Spawn(0x406, ...)` in the PKT2 bounce passes a rotation vector; only `.x` is set on
  console (rest stack garbage), natively zeroed.
- Attribute field names are inferred from use; offsets are exact.
- `fp->allow_sdi` (console 0x2218 bit 0x2000) gates the rope simulation; the disc uses it as "in
  hitlag". Named by the decomp field, not re-verified at runtime.

## What the integration layer must provide

1. **Article registration** (m-ex 0x803D7058), called by OnLoad for i = 0..9:
   `void mu_ak_register_article(FighterKind kind, void* article_desc, int index);`
   (the console arguments are `(fp->kind, ft_data->x48_items[i], i)`).
2. **Article kind lookup** (m-ex 0x803D7088):
   `ItemKind mu_ak_article_kind(HSD_GObj* fighter_gobj, int index);`
3. **Article logic**: register `ftLc_ArticleLogic[10]` (`lucas.h`) as the ItemLogicTable for
   Lucas's article kinds 0..9 (slot 2 empty). Tails 4-7 share one state table.
4. **mplib's jointListStart** (static at console 0x804D64C4) for the tether ledge search:
   `CollJoint* mu_ak_mp_joint_list(void);`
5. **Three extra ftFunction slots** (41/43/44) wired to the exported `mu_ak_lucas_on_intro_l`,
   `mu_ak_lucas_on_appeal`, `mu_ak_lucas_on_catch`, or `MuAkFighter` fields for them
   (requested change to `mu_ak_fighter.h`: `onintrol`, `onappeal`, `oncatch`).
6. **Item hook signatures**: `onitempickup`/`onitemcatch` and `onitemdrop`/
   `onunknownitemrelated` are `(HSD_GObj*, bool)` functions stored through a `MuAkEvent` cast;
   the dispatcher must call them as `Fighter_ItemEvent`.
7. **The rope allocation**: OnLoad allocates `sizeof(LucasRope)` with `HSD_MemAlloc` (m-ex used
   its calloc at 0x803D706C) and OnDestroy (slot 2) frees it, so slot 2 must run on user-data
   removal.
8. **Effects and sounds**: Lucas's own effect ids 0x1388 (5000, PK Thunder start), 0x1389 (5001,
   PK Thunder 2 body) and 0x138A (5002, PSI Magnet) come from EfLcData.dat through m-ex's effect
   table; sound ids 0x13D9 (magnet hum) and 0x13F7 (stick reflect) from Lucas's SSM. Vanilla ids
   used: effects 10, 0x406, 0x41C, 0x421; sounds 4, 0x86, 0x11B.
9. **CPU**: nothing extra; `ftLc_InitCpuSpoof` swaps the fighter's `Fighter_procCpu` for a proc
   that runs `ftCo_800B3900` with `fp->kind` = Ness (8). Requires `Fighter_procCpu`,
   `ftCo_800B3900`, `HSD_GObjProc_RemoveProc` to stay exported (they are today).
10. **Data**: attributes, animations (anim ids 295-323 for the specials), hitboxes and articles
    load from the player's disc through the existing m-ex data layer; `ResetAttributes` copies
    0x13C bytes from `ft_data->ext_attr`.

## Decomp functions called that are static there

- `ftCo_8009EE30` (StopWall entry, static in ftCo_StopWall.c) is reimplemented locally in
  `lucas_aircatch.c` (`ftLc_EnterStopWall`), copied from the decomp's body.
- `ftNs_JumpAerial_Phys_Cb` (static) is reached by calling the public `ftNs_JumpAerial_Enter`,
  which the disc's double jump inlines step for step.
- `ftCo_800B2790 / 800B2AFC / 800B33B0` (static) are reached through the public `ftCo_800B3900`.

## Kirby's copy ability

`lucas_kirby.c` is the m-ex `kbFunction` of `PlKbCpLc.dat` (no debug symbols; written from
`run-source/rel09-b1-wolf/kirby/listings/PlKbCpLc.listing.txt`). It is PK Freeze again with the
parameters read from the hat data, the held freeze kept in Kirby's fighter variables
(`ftKbLc_PKFreeze`, console fp+0x2270), article 10 for the projectile and Kirby's part 4. It
reuses `ftLc_PKFreeze_Spawn` and the state callbacks whose instructions are the same.

Article 10 (item kind 276 on Akaneia, `ftLc_Art_KirbyPKFreeze`) is the eleventh entry of
`ftLc_ArticleLogic`. Its code, in the hat file's `itFunction`, is article 0's with the holder's
word at fp+0x2270: `lucas_it_pkfreeze.c` picks the word by the holder's kind
(`ftLc_PKFreeze_HeldSlot`). `ftLc_Art_Count` stays 10: it is the number of articles OnLoad
registers from `ft_data->x48_items`; the table and `article_count` use `ftLc_Art_TableCount`.

Function table, what is proven and the test: `run-source/rel09-b1-wolf/kirby/LUCAS.md`. Written,
not compiled, not run.
