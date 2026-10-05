# Akaneia Sonic, native

Source studied: `PlSn.dat` from the Akaneia v1.0.1 disc. Root `ftFunction` is an m-ex
`MEXFunction` (0x5778 bytes of PowerPC, 944 relocations, 25 exports, 207 debug symbols), root
`itFunction` holds the spring article (0x500 bytes, 8 exports). The debug symbols give every
routine its original name; the C keeps those names with the `ftSn_` prefix. Every routine was
read and rewritten by hand against the decomp's types and helpers. Nothing was run or
transliterated mechanically.

Attributes: `ftSonic_DatAttrs` (0x148 bytes, `DISC_STRUCT`), named from use, v1.0.1 values in
comments. Fighter vars overlay `fp->u` (`ftSonic_FighterVars`, console 0x222C..0x224B). Motion
vars overlay `fp->mv` (`ftSonic_*Vars`, console 0x2340..0x2357) and keep the console's relative
offsets so leftovers alias the way they did on console.

Every file was syntax-checked alone with the melee_game gcc line plus `-fsyntax-only`. Nothing
has been built, linked or run.

## Files

| file | contents |
|---|---|
| `sonic.h` | states 341..371, effect/sfx ids, attrs, vars, `MuAkSonicHooks`, prototypes |
| `sonic.c` | `mu_ak_sonic`, move_logic table, OnLoad/OnDeath/OnDestroy/ResetAttributes, item events, eye textures, OnFrame, OnActionStateChange, EnterDoubleJump, OnSmashHi, ProcessMouth, CheckWinAudio, DidHeLose, CheckSameTeam, GXLink_Sonic |
| `sonic_effects.c` | spin ball, speed trail, running shoe blur, shoe colors, hitlag effect callbacks |
| `sonic_specialn.c` | homing attack (9 states) + debug target/radius drawing |
| `sonic_specials.c` | spin dash (9 states) |
| `sonic_specialhi.c` | spring jump + Spawn_Spring |
| `sonic_speciallw.c` | spin charge (12 states) + the up/neutral-B check used from RunJump |
| `sonic_spring.c` | the spring article: 3 item states + item logic table |
| `sonic_kirby.c` | the ability Kirby copies (`PlKbCpSn.dat` kbFunction): written 2026-10-04, see `run-source/rel09-b1-wolf/kirby/SONIC.md`. For it `sonic_specialn.c` now has `ftSn_SpecialN_SearchTarget` and a `ftSn_SpecialN_ClampReboundVel` that take the values as an argument, and `sonic_effects.c` has `ftSn_SpawnTrailEffectAt`; Sonic's own routines call them with his attributes and fields |

## Routine status

Everything the PPC exports, everything move_logic points at and every helper they call is
written: 207 of 207 debug symbols (strings and constant pools included). Roughly **100% written,
about 90% expected to behave identically**. The rest depends on the integration hooks and the
uncertain points below.

| group | routines | status |
|---|---|---|
| ftFunction exports | onload, ondeath, onunknown, move_logic, specialn/airn, specials/airs, specialhi/airhi, speciallw/airlw, onitempickup, onmakeiteminvisible, onmakeitemvisible, onitemdrop, onitemcatch, onunknownitemrelated, onhit, onunknowneyetexturerelated, onframe, onactionstatechange, onrespawn, enterdoublejump, onsmashhi | done |
| empty m-ex slots | onabsorb, model flags 1/2, onmodelrender, onshadowrender, onunknownmultijump, ontwoentrytable, enterfloat, entertether, onlanding, onsmashf, onsmashlw, onactionstatechangewhileeyetextureischanged | NULL (game default), as shipped |
| SpecialN (341..349) | Start, Charge, AttackMiss, Attack, Cancel, Landing, Rebound, Hit + SearchTarget, Rebound_OnEnter | done |
| SpecialHi (350) | Enter, Anim, IASA, Phys, Coll, Spawn_Spring | done (spawn needs hook) |
| SpecialS (351..359) | Start, End, AirStart, AirEnd, Hold, S, AirS (+ Max variants), all Trans, OnFinishCharge, SpawnChargeEffect, ApplyFriction, OnHit, GiveDamage | done |
| SpecialLw (360..371) | Start, End, AirStart, AirEnd, Charge, Run, RunTurn, RunJump, Dive, RunBrake, StopWall L/R, all Trans, EnterRun, DecrementLife, AdjustHitboxDamage, Interrupt_StopWall, OnHit, IASACheck_UpSpecial | done |
| effects | GFXTrail, GFXTrailLoop, GFXSpin, GFXSpinAndTrail(+VelocityDirection), UpdateTrailPosAndRot, SpawnTrailEffect, RunEffectCallback, Color_Shoes | done |
| misc | ProcessMouth, Sonic_CheckWinAudio, DidHeLose, Fighter_CheckSameTeam, GXLink_Sonic, DrawTarget, DrawRadius, GX init/end | done |
| spring article | item_state_table, onspawn, ondestroy, ongivedamage, ontakedamage, onreflect, onhitshieldbounce, onhitshielddeterminedestroy, Idle/Fall/Rebound states, Spring_JumpedOn | done |
| Kirby copy ability | `PlKbCpSn.dat` / m-ex kirby functions | written (`sonic_kirby.c`), not built, not run |

## What the integration layer must provide

1. **Registration.** `mu_ak_sonic` (declared in `mu_ak_fighter.h`), states from `ftCo_MS_Count`
   (31 entries, `ftSn_MS_SelfCount`). The table's third word is `FtMoveId_Special* << 24`, the
   same convention as the decomp tables.
2. **Item events take a bool.** `onitempickup`, `onitemdrop`, `onitemcatch` and
   `onunknownitemrelated` are `void (HSD_GObj*, bool)` (the decomp's `Fighter_ItemEvent`),
   stored in `MuAkEvent` slots with a cast. They must be called with the flag, like
   `ftData_OnItemPickupExt` does. **Request:** give these slots a `Fighter_ItemEvent` type in
   `mu_ak_fighter.h`.
3. **`mu_ak_sonic_hooks`** (in `sonic.h`, zero until filled in). These stand in for m-ex runtime
   calls. Each one falls back safely when NULL:
   - `costume_archive(kind, costume)`: m-ex `MEX_GetData(MXDT_FTCOSTUMEARCHIVE)[kind]`, entry
     `costume`, `+0x14` archive (the widened `CostumeListsForeachCharacter`). Used to find
     `PlySonicColor` (trail tint, shoe color). NULL means default colors.
   - `index_fighter_item(kind, article, index)`: `MEX_IndexFighterItem`, called from OnLoad
     with `ft_data->x48_items[0]` (the spring's `Article`).
   - `fighter_item_kind(fighter_gobj, index)`: `MEX_GetFtItemID`, the item kind of article 0.
     NULL means no spring, and SpecialHi still launches.
   - `fighter_name(ckind)`: `MEX_GetData(MXDT_FTNAME)[ckind]`. The result screen uses it to find
     "Tails" for the alternate win line. NULL means the normal line.

   If the layer ends up with generic m-ex services, point these at them.
4. **Spring item.** Register `ftSn_Spring_LogicTable` (states `ftSn_Spring_StateTable[3]`, anim
   ids 1..3) for the item kind that `fighter_item_kind(…, 0)` returns. The article's `Article`
   data (common attrs, special attrs, model, state anims) comes from `x48_items[0]` on disc.
   Item vars use `xDD4_itemVar` as `{ float bounce_vel; HSD_GObj* owner; }`.
5. **Effects** (Sonic's own bank): 5000 spin ball, 5001 trail, 5003 spin-dash dust, 5006 running
   shoe blur, 6004..6006 spin-charge levels. They are spawned through `efSync_Spawn`, so the m-ex
   effect-file mapping for the Akaneia kind must be in place.
6. **Sounds** (Sonic's SSM): 5001, 5007, 5019, 5049, plus the win lines that the result-screen
   animation scripts put in `cmd_vars[0/1]`.
7. **Fighter data for the kind**: `ft_data->ext_attr` (0x148 bytes), `x48_items[0]` (spring
   article), `x48_items[1]` (the full-charge color overlay passed to `lb_800144C8`), and the
   action table with anim ids 295..320 plus the common 14. The result-screen check needs
   `fp->x1C_actionStateList == ftData_803C52A0` for demo fighters, as in retail.
8. **Unions**: `ftSonic_FighterVars` needs 0x30 bytes of `fp->u` natively (pointers are 8
   bytes), and the motion vars need 0x18 bytes of `fp->mv`. Both fit today.

## Uncertain behavior and as-shipped quirks (kept on purpose)

- **B-reverse from RunJump** (`IASACheck_UpSpecial`). The m-ex code reads the int
  `p_ftCommonData->x224` (20) with `lfs`, which gives the denormal 2.8e-44. I reproduce that bit
  for bit. On console, if the Gekko FPSCR flushes denormals (NI mode), the reverse never
  happens. With IEEE denormals, as on x86 SSE without FTZ, it only happens on the frame the
  stick moved (duration 0). This needs checking against console.
- **OnSetItemVisible** calls the hide function (`ftAnim_80070CC4`), exactly like
  OnSetItemInvisible. It looks like a bug in the shipped code, but it is kept.
- **SpecialHi on the ground**: `self_vel.y = attr 0x74` is overwritten by `attr 0x70` a few
  lines later. Both are 3.5.
- **Spin dash landing/leaving ground** (`SpecialS_Trans`, `SpecialAirS_Trans`) always switches
  to the uncharged 356/357 states, even from the full-power 358/359.
- **Down B End transitions** (`SpecialLwEnd_Trans`, `SpecialAirLwEnd_Trans`) change the state
  without `ftCommon_8007D7FC` / `ftCommon_8007D5D4`. Only the Start ones flip ground/air.
- **SpecialLwCharge_Anim** keeps checking the release timer after the hold limit already
  entered Run/Dive and can enter End on the same frame. `SpecialNAttack_Anim` can enter Cancel
  twice in one frame. `SpecialSHold_IASA` increments `hold_frames` after leaving. All of these
  are as shipped.
- **Homing target z** (console fp+0x2354) is never written by SpecialN. The distance check
  uses whatever the previous state left there. After Sonic's own moves that is SpecialLw's two
  s16 fields, which read as a tiny denormal, so effectively 0. The layout keeps the same
  position, but a host-order read of two s16 fields gives a different tiny value. It only
  matters if a common state left a real float there.
- **SpecialN anim rates**: Charge plays at attr 0x18 = 60× and Attack/Miss at attr 0x28 = 50×.
  These are harmless because those states never test the animation end.
- **RunEffectCallback**: MexTK's `JOBJ_PauseOnFrame` calls `HSD_AObjSetRate` through arg kind 6
  with two zero words. It is written as `AOBJ_ARG_AF, 0.0f`, which is the evident intent
  (freeze the texture animation).
- **Run effect jobj check**: the PPC compares the effect jobj to the address of an empty
  function, which never matches, so the shoes are always recolored. It is written as
  `jobj != NULL`.
- **Color_Shoes** reads `PlySonicColor` without a NULL check in m-ex. Here a NULL color leaves
  the shoes alone. The "dobj not found!" assert is kept (`HSD_ASSERTMSG`).
- **Spawn_Spring**: m-ex calls the item creator `Item_8026862C` directly, but that function is
  static in the decomp. The public `Item_80268B5C` / `Item_80268B18` wrappers are used,
  picked by `ground_or_air`, which is what the spawn struct carries. They recompute `hold_kind`
  (8 for a fighter article, same as m-ex) and zero `x10`, which m-ex also sets. The spawn
  struct starts zeroed; m-ex left `x44..x47` apart from `x44.b0` as stack garbage.
- **Spring sound owner**: m-ex plays spring sounds on the fighter in its item extension
  (console item+0xFCC). `ip->owner` is used instead. It is the same gobj unless the spring is
  reflected, which changes the owner.
- **Spring Idle_Coll** passes NULL as the `it_8026D8A4` callback. That would crash on console if
  the callback ever fired. A no-op is passed instead.
- **DidHeLose** compares `match_end.player_standings[i]` byte +5 (`is_big_loser` in the decomp)
  as a rank, exactly as the PPC does.
- **Fighter_IASA_AirAttack** is a data slot holding `ftCo_AttackAir_CheckItemThrowInput`
  (0x8008CD68). It is called directly.
- **FLT_MAX** in the target search is the m-ex constant 0x7F7FFFEE, not the true FLT_MAX.
