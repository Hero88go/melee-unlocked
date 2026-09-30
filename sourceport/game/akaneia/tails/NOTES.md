# Tails (Akaneia) native port notes

Source: `PlTs.dat` on the Akaneia v1.0.1 disc. Root `ftFunction` (m-ex MEXFunction: 0x64DC bytes of
code, 1015 relocations, 26 exports, 214 debug symbols) and root `itFunction` (one article, 0x454
bytes, 9 exports). The debug symbol names are kept as function names (prefix `ftTs_` / `itTs_`)
with the console code offset in a comment above each function, so every routine can be compared
with the disassembly. Internal fighter kind on the disc: 33 (external id 32, "Tails").

Everything was read and rewritten by hand; no PowerPC is run or translated mechanically.

## Files

| File | Contents |
|---|---|
| `ftTs_types.h` | motion state ids, flag words, effect/sfx ids, bone ids, the 0x1A8 attribute block (`DISC_STRUCT`), colour table, fighter vars (`fp->u`), motion vars (`fp->mv`), article attrs/vars, CPU record |
| `ftTs_hooks.h` | the services Tails needs from the integration layer (see below) |
| `ftTs.h` | shared prototypes |
| `tails.c` | ftFunction exports (load, respawn, destroy, items, eyes, landing, double jump), the 33-entry `move_logic` table, `const MuAkFighter mu_ak_tails` |
| `ftTs_specialn.c` | neutral special (energy shot) |
| `itTs_shot.c` | the shot article (state table + `ItemLogicTable`) and the fighter-side spawn |
| `ftTs_specials.c` | side special (tail spin, 5 loop animations, ground and air) |
| `ftTs_specialhi.c` | up special (fuel-based flight, flapping, exhaust) |
| `ftTs_speciallw.c` | down special (spin dash: charge, run, turn, jump, dive, brake, wall bonk) |
| `ftTs_visual.c` | tail ball, spin trail (GX), effect/particle recolour, mouth, result-screen voice |
| `ftTs_cpu.c` | m-ex MexCPU setup, CPU attack tables, CPU special logic |

All eight .c files pass `gcc -fsyntax-only` with the melee_game flags (checked one at a time, also
with `-Wall`: clean).

## Routine status

Status: **done** = full behaviour rewritten; **partial** = written but depends on something not yet
available or knowingly deviates; **missing** = not written.

### ftFunction exports

| m-ex slot | Console symbol | Status |
|---|---|---|
| onload | OnLoad | done (needs hooks: costume symbol, article registration) |
| ondeath | OnRespawn | done |
| onunknown | OnDestroy | done |
| move_logic | move_logic (33 states, 341..373) | done |
| specialn / specialairn | SpecialN / SpecialAirN -> SpecialN_Enter / SpecialAirN_Enter | done |
| specials / specialairs | both -> SpecialS_EnterAirOrGround | done |
| specialhi / specialairhi | both -> SpecialHi_EnterAirOrGround | done |
| speciallw / specialairlw | both -> SpecialLw_EnterAirOrGround | done |
| onitempickup, onitemcatch | OnItemPickup, OnItemCatch | done (take `(gobj, bool)`, see hooks) |
| onmakeiteminvisible / visible | OnSetItemInvisible / OnSetItemVisible | done |
| onitemdrop, onunknownitemrelated | OnItemRelease, OnUnknownItemRelated | done (take `(gobj, bool)`) |
| onhit | EyeTextureDamaged | done |
| onunknowneyetexturerelated | EyeTextureNormal | done |
| onframe, onactionstatechange, onmodelrender | empty on the disc | done (empty functions) |
| onrespawn | ResetAttributes | done |
| enterdoublejump | EnterDoubleJump | done |
| onlanding | OnLanding | done |
| onabsorb, onshadowrender, modelflags, eye-change, twoentry, float, tether, smash f/hi/lw | not exported by Tails | NULL |

### Internal routines (all done unless noted)

- Neutral special: SpecialN_OnEnter, SpecialN_Accessory, SpecialN/AirN Anim/IASA/Phys/Coll,
  SpecialN_Trans, SpecialAirN_Trans, SpecialN_SpawnProjectile.
- Shot article: item_state_table (3 states), OnSpawn (empty), OnDestroy, OnGiveDamage,
  OnTakeDamage, OnReflect, OnClank, OnHitShieldBounce, OnHitShieldDetermineDestroy, Move_Anim/Phys/
  Coll, Die_Anim, Die_Enter, OnHitShieldDetermineDie, SetMovePastLedge, JOBJ_GetAnimFrame.
  OnDestroy is **partial**: the Kirby-owner branch (see below) is not ported.
- Up special: EnterAirOrGround, Start/AirStart Anim/IASA/Phys/Coll, Start_Trans, AirStart_Trans,
  Loop_Enter, Loop Anim/IASA/Phys/Coll, Exhaust_Enter, Exhaust Anim/IASA/Phys/Coll, Cancel
  Anim/IASA/Phys/Coll, RotateToFacingDirection, SpecialHi_OnHit, Fighter_IASACheck_UpSpecial.
- Side special: EnterAirOrGround, Start/End/Loop and Air Start/End/Loop Anim/IASA/Phys/Coll,
  Loop_Enter, AirLoop_Enter, End_Enter, AirEnd_Enter, all six Trans functions.
- Down special: EnterAirOrGround, Start/End/AirStart/AirEnd (x4 callbacks + Trans + Enter),
  Charge (Enter + 4), Run (Enter + Trans + 4), RunTurn (Trans + 4), RunJump (Trans + 4), Dive
  (Enter + 4), RunBrake (Enter + 4), StopWall (Enter + 4), SpecialLw_EnterRun,
  SpecialLwInterrupt_StopWall, SpecialLw_SpawnParticle, SpecialLwRun_DecrementLife,
  SpecialLw_SpawnRunningParticles, SpecialLw_AdjustHitboxDamage, SpecialLw_OnHit.
- Visual: ProcessTail, Tails_EnableTailBall, Tails_DisableTailBall, slerp, ProcessTrail,
  IsTailTrailProcess, Vec3_Add, Tails_GXCallback, Tails_DrawAfterImage, ProcessMouth,
  SpawnBallEffect, SpawnHelicopterEffect, Effect_Recolor, OnSpawnParticle, InitDashTailAnim,
  MetalColor, Tails_CheckWinAudio, DidHeLose.
- CPU: CPU_Tails, CPU_SpecialLw, CPU_SpecialS, CPU_SpecialHi, the 7 attack tables + SpecialSAttacks,
  cpu_data, MexCPU_InitCustomData, MexCPU_InitProc, MexCPU_ProcCustom, MexCPU_ProcSpoof, and the
  custom step of MexCPU_Process. **partial**: the pipeline itself is a hook (see below) and the
  table records are in disc order while the port's reader currently reads host order.

Rough coverage: every routine in the file set has C (about 190 functions); counting the partial
ones as half, about 97%. Not covered at all: Kirby's copy of Tails's neutral special
(`PlKbCpTs.dat`, its own `kbFunction` + `itFunction`), which belongs to the Kirby copy layer.

## Shared with Sonic

The debug symbols show Tails's down special is the same source as Sonic's spin dash (every
`SpecialLw*` name matches `PlSn.dat`), and `DidHeLose`, `ProcessMouth`, `OnRespawn`,
`ResetAttributes`, `Fighter_IASACheck_UpSpecial` and the MexCPU plumbing are shared too. Tails keeps
its own copy here (attribute offsets are per file); once both ports exist the spin dash could be
unified behind an attribute struct. `sonic/` was not touched.

## Behaviour worth knowing (read off the disc, kept as is)

- Only one shot at a time: SpecialN/SpecialAirN do nothing while `fv.shot_gobj` is set; the shot's
  OnDestroy clears it. The shot is not destroyed by hitting someone (dmg_dealt returns false).
- Flight fuel (`fv.fuel`) is refilled on landing (OnLanding) and respawn (ResetAttributes), set to
  at least `hi_min_fuel` when the flight starts, and cut to `hi_fuel_on_hit` when hit while flying
  (take_dmg_cb and death2_cb).
- The air tail spin's upward boost fires once per airtime and only when the whole spin stayed
  airborne (`mv.all_airborne` is cleared by any ground loop).
- Spin dash: charging without ever reaching level 0 ends the move; the idle timeout is refreshed
  by every B press; the charge also auto-releases. The hitbox damage of the roll and the roll jump
  scale linearly with the level.

## Uncertain or deliberately different

1. **SpecialS_Start_Anim / SpecialAirS_Start_Anim call Loop_Enter without its float arguments.**
   On console the call has no prototype (`crclr cr1eq`, two integer zeros in r4/r5), so the frame
   and blend were whatever f1/f2 held after `ftAnim_IsFramesRemaining`. The port passes 0, 0,
   which is what the source intended. If the first loop visibly starts mid-animation on console,
   this is why.
2. **RunJump aerial lock does nothing.** Both branches of SpecialLwRunJump_IASA call
   `ftCo_AttackAir_CheckItemThrowInput` (through the pointer at code+0x4E6C); the locked branch
   just ignores the result. Kept exactly.
3. **SpecialHi_Cancel (347)** is never entered by the code. Kept in the table.
4. **SpecialLwEnd_Trans / SpecialAirLwEnd_Trans** change state without the ground/air helper call
   (every other Trans has one). Kept.
5. **Missing costume colour table.** Console reads through a NULL `PlyTailsColor` pointer (low
   memory). The port skips the recolour / trail draw when there is no table (metal still works).
6. **Floating point.** Where the console multiplies and adds with separate instructions (no
   fmadd), the C uses separate statements so `-ffp-contract=on` cannot fuse them (SpecialS loop
   speed, hitbox damage scaling, RunJump drift, RunTurn accel, wall push-out, trail distance,
   slerp, tail ball rotation, CPU look-ahead). Double-precision steps (pi constants, the trail
   distance, the CPU random threshold) are kept in double.
7. **Trail with one sample**: the console divides 0/0 for the alpha (NaN -> 0); the port uses 0
   directly.
8. **CPU kind spoof.** MexCPU makes Tails pose as kind 0x20 for the whole CPU frame and borrows
   that kind's PlCo.dat table slots (restored afterwards), exactly as on console. On the Akaneia
   PlCo.dat slot 0x20 is another fighter's; this only works because it is restored each frame.
9. **CPU table byte order.** `ftCo_800B4AB0` in the port reads attack records as host-order
   structs, but the PlCo.dat tables it normally gets are big-endian disc data. Tails's tables are
   written in disc order (`ftTails_CpuAttack` is `DISC_STRUCT`) to match PlCo.dat, so they will
   read correctly once that reader honours byte order (it is wrong for the vanilla tables too).
10. **Shot OnDestroy with a Kirby owner**: console clears Kirby's slot at fp+0x596C (m-ex's
    extended Kirby ability area). Not ported; the Tails owner path is.
11. `ftTs_OnSpawnParticle` finds its fighter through the generator's `userfunc` pointer
    (`container_of` on `ftTails_ParticleHook`) instead of the console's fixed offsets, so the
    native layout of `HSD_PSUserFunc` does not matter.

## What the integration layer must provide

1. **`ftTs_hooks.h` services** (one implementation for all Akaneia fighters):
   - `mu_ak_costume_symbol(fp, "PlyTailsColor")`: public symbol of the costume's archive
     (m-ex MXDT_FTCOSTUMEARCHIVE runtime, `[kind].runtimes[costume_id].archive`), or NULL.
   - `mu_ak_register_article(kind, ft_data->x48_items[0], 0)`: m-ex MEX_IndexFighterItem.
   - `mu_ak_article_kind(gobj, 0)`: the item kind of that article (spawn uses it).
   - `mu_ak_fighter_name(ckind)`: MXDT_FTNAME by external id (result screen, "Sonic").
   - `mu_ak_cpu_process(gobj, custom)`: ftCo_800B3900's pipeline with a callback after
     ftCo_800B2AFC (ftCo_800B33B0/2AFC/2790 are `static` in ftCo_0A01.c, so it has to live there
     or those must be exported). m-ex's copy leaves out ftCo_800B0AF4 and bumps `fp->cpu.x7C`.
2. **Callback signatures.** `onitempickup`, `onitemcatch`, `onitemdrop` and
   `onunknownitemrelated` are `Fighter_ItemEvent` `(gobj, bool)` and are stored through
   `MuAkEvent` casts; cast them back before calling (as ftData_OnItemPickupExt etc. do).
3. **The article.** `itTs_Shot_Logic` (`ItemLogicTable`, with `itTs_Shot_StateTable`) is the
   logic for the item kind returned by `mu_ak_article_kind(..., 0)`; the item logic dispatch must
   be widened for it. Item vars live in `ip->xDD4_itemVar` (`itTails_ShotVars`), attributes in the
   article's `x4_specialAttributes` (`itTails_ShotAttrs`, disc order).
4. **Effects and sounds** (m-ex ids, from `EfTsData.dat` and Tails's sound bank):
   - model effects attached to a joint (`efSync_Spawn(id, gobj, HSD_JObj*)`, returns
     `EF_Effect*`): 5000 ball (jump/dive), 5002 ball (charge/roll), 5003 (AttackHi4),
     5004 (AttackAirN/AttackDash), 5005 helicopter (neutral special), 5006 helicopter (Run and
     flight);
   - particle generators at a point (`efSync_Spawn(id, gobj, Vec3*)`, returns `HSD_Generator*`,
     Tails sets its `userfunc`): 6001 roll dust, 6004..6006 charge levels 0..2;
   - sounds through `ft_80088478`: 5001 (roll jump), 5007 (rev); voice ids come from the win-pose
     scripts via `cmd_vars` (ft_800881D8).
5. **Motion states** 341..373 installed from `mu_ak_tails.move_logic`; animation ids 0x127..0x148
   index PlTs.dat's own action table (357 uses the common RunBrake animation 0xE).
6. **Data expectations**: `ft_data->x1C[3]` is the tail part-animation slot (InitDashTailAnim);
   the demo/result model is recognised by `fp->x1C_actionStateList == ftData_803C52A0`, so the
   result screen must build Tails with that table as vanilla fighters are.
7. **Fighter variables** fit `fp->u` and `fp->mv` (static asserts in `tails.c`); nothing else is
   needed from the Fighter struct beyond the decomp's fields.

## Console layout cross-reference

Fighter vars (fp+0x222C): shot gobj 0x222C, trail gobj 0x2230, ball angle 0x2234, ball matrix
0x2238 (translation y at 0x2254), trail last pos 0x2278, trail count 0x2284, trail buffers
0x2288 / 0x228C (0xC0 each), particle user func 0x2290 (colours 0x229C, fp 0x22A0), air spin
boost 0x22A4, fuel 0x22A8.

Motion vars (fp+0x2340): see the three `ftTails_Special*Vars` structs in `ftTs_types.h`.

The attribute offsets are the `/* 0xNNN */` comments on `ftTails_DatAttrs`.
