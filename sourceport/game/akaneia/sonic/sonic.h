/* Akaneia's Sonic, native.
 *
 * Hand-written from a study of the m-ex ftFunction/itFunction code in PlSn.dat (Akaneia v1.0.1).
 * Routine names follow the m-ex debug symbols that ship in that file (SpecialNStart_Anim, ...),
 * prefixed ftSn_ the way the decomp prefixes its fighters. Console offsets are given where a
 * field has no decomp name. See NOTES.md for status and what the integration layer must supply. */
#ifndef MU_AK_SONIC_H
#define MU_AK_SONIC_H

#include <mu_disc.h>

#include <dolphin/gx/GXStruct.h>
#include <melee/ef/forward.h>
#include <melee/ft/forward.h>
#include <melee/ft/kinds/ftCommon/forward.h>
#include <melee/ft/kinds/ftCommon/types.h>
#include <melee/ft/types.h>
#include <melee/it/forward.h>
#include <melee/it/kinds/types.h>
#include <sysdolphin/baselib/forward.h>

#include "../mu_ak_fighter.h"

/* ---------------------------------------------------------------------------------------------
 * Special action states (move_logic), numbered from ftCo_MS_Count exactly as m-ex numbers them.
 * ------------------------------------------------------------------------------------------- */
typedef enum ftSonic_MotionState {
    ftSn_MS_SpecialNStart = ftCo_MS_Count, /* 341: homing attack windup (ground anim) */
    ftSn_MS_SpecialAirNStart,              /* 342: homing attack windup (air anim) */
    ftSn_MS_SpecialNCharge,                /* 343: curled up, searching for a target */
    ftSn_MS_SpecialNAttackMiss,            /* 344: no target, dives forward and down */
    ftSn_MS_SpecialNAttack,                /* 345: homing dash toward the target */
    ftSn_MS_SpecialNCancel,                /* 346: dash ended without a hit */
    ftSn_MS_SpecialNLanding,               /* 347 */
    ftSn_MS_SpecialNRebound,               /* 348: bounced off a wall/floor/shield */
    ftSn_MS_SpecialNHit,                   /* 349: bounce after hitting the target */
    ftSn_MS_SpecialHi,                     /* 350: spring jump */
    ftSn_MS_SpecialSStart,                 /* 351: spin dash (side B) */
    ftSn_MS_SpecialSEnd,                   /* 352 */
    ftSn_MS_SpecialAirSStart,              /* 353 */
    ftSn_MS_SpecialAirSEnd,                /* 354 */
    ftSn_MS_SpecialSHold,                  /* 355: charging, ground and air */
    ftSn_MS_SpecialS,                      /* 356: the dash */
    ftSn_MS_SpecialAirS,                   /* 357 */
    ftSn_MS_SpecialSMax,                   /* 358: the dash from a stored full charge */
    ftSn_MS_SpecialAirSMax,                /* 359 */
    ftSn_MS_SpecialLwStart,                /* 360: spin charge (down B) */
    ftSn_MS_SpecialLwEnd,                  /* 361 */
    ftSn_MS_SpecialAirLwStart,             /* 362 */
    ftSn_MS_SpecialAirLwEnd,               /* 363 */
    ftSn_MS_SpecialLwCharge,               /* 364: mashing B, ground and air */
    ftSn_MS_SpecialLwRun,                  /* 365: rolling along the ground */
    ftSn_MS_SpecialLwRunTurn,              /* 366 */
    ftSn_MS_SpecialLwRunJump,              /* 367: jumped or rolled off an edge */
    ftSn_MS_SpecialLwDive,                 /* 368: released in the air */
    ftSn_MS_SpecialLwRunBrake,             /* 369: roll ran out (anim is the common RunBrake) */
    ftSn_MS_SpecialLwStopWallL,            /* 370: rolled into a wall facing left */
    ftSn_MS_SpecialLwStopWallR,            /* 371: rolled into a wall facing right */
    ftSn_MS_SelfCount_End
} ftSonic_MotionState;

#define ftSn_MS_SelfCount (ftSn_MS_SelfCount_End - ftCo_MS_Count)

/* Effect ids (Sonic's own effect bank, served by the m-ex effect layer) */
enum {
    ftSn_Ef_Spin = 5000,        /* 0x1388: spin ball around bone 1 */
    ftSn_Ef_Trail = 5001,       /* 0x1389: speed trail left behind */
    ftSn_Ef_ChargeDust = 5003,  /* 0x138B: spin dash dust */
    ftSn_Ef_RunShoes = 5006,    /* 0x138E: shoe blur while running */
    ftSn_Ef_SpinLevel0 = 6004,  /* 0x1774..0x1776: spin charge level 0..2 */
};

/* Sound ids (Sonic's own sound bank) */
enum {
    ftSn_Sfx_Trail = 5001,      /* 0x1389: spin charge jump / trail */
    ftSn_Sfx_SpinCharge = 5007, /* 0x138F */
    ftSn_Sfx_Spring = 5019,     /* 0x139B */
    ftSn_Sfx_ChargeFull = 5049, /* 0x13B9 */
};

/* ---------------------------------------------------------------------------------------------
 * The special attributes (ftData->ext_attr, 0x148 bytes, big-endian disc data). Names come
 * from how the code uses each value; the value in Akaneia v1.0.1 is noted.
 * ------------------------------------------------------------------------------------------- */
typedef struct ftSonic_DatAttrs {
    /* 0x000 */ ftCollisionBox_BE coll_box;          /* ECB for the air specials: 5,0,(-2,2),(2,2) */
    /* 0x018 */ float specialn_charge_anim_rate;     /* 60 */
    /* 0x01C */ int specialn_min_charge_frames;      /* 15: B can release from here */
    /* 0x020 */ int specialn_max_charge_frames;      /* 30: auto release */
    /* 0x024 */ float specialn_charge_vel_y;         /* 0.55 */
    /* 0x028 */ float specialn_attack_anim_rate;     /* 50 */
    /* 0x02C */ float specialn_miss_speed;           /* 3 */
    /* 0x030 */ float specialn_search_range;         /* 65 */
    /* 0x034 */ float specialn_target_offset_y;      /* 3 */
    /* 0x038 */ float specialn_attack_speed;         /* 5 */
    /* 0x03C */ int specialn_attack_frames;          /* 13 */
    /* 0x040 */ float specialn_aim_stick_x;          /* 5: stick nudges the aim point */
    /* 0x044 */ float specialn_aim_stick_y;          /* 5 */
    /* 0x048 */ float specialn_cancel_air_decel;     /* 0.7 */
    /* 0x04C */ float specialn_rebound_mul_x;        /* 0.6 */
    /* 0x050 */ float specialn_rebound_gravity;      /* 0.185 */
    /* 0x054 */ float specialn_rebound_terminal_vel; /* 1.7 */
    /* 0x058 */ float specialn_rebound_air_decel;    /* 0.1 */
    /* 0x05C */ float specialn_rebound_max_vel_x;    /* 1.5 */
    /* 0x060 */ float specialn_rebound_max_vel_y;    /* 3 */
    /* 0x064 */ float specialn_landing_decel;        /* 0.4 */
    /* 0x068 */ float specialhi_landing_lag;         /* 15 */
    /* 0x06C */ float specialhi_fall_mobility;       /* 0.2 */
    /* 0x070 */ float specialhi_vel_y;               /* 3.5 */
    /* 0x074 */ float specialhi_ground_vel_y;        /* 3.5 (overwritten by vel_y, see NOTES) */
    /* 0x078 */ float specialhi_drift_accel;         /* 0.1 */
    /* 0x07C */ float specialhi_drift_max;           /* 0.6 */
    /* 0x080 */ float specialhi_gravity;             /* 0.1 */
    /* 0x084 */ float specialhi_terminal_vel;        /* 2 */
    /* 0x088 */ float specialhi_turn_threshold;      /* 0.1 */
    /* 0x08C */ int specials_min_hold_frames;        /* 8 */
    /* 0x090 */ int specials_max_charge;             /* 75 */
    /* 0x094 */ int specials_dust_interval;          /* 3 */
    /* 0x098 */ float specials_min_speed;            /* 4 */
    /* 0x09C */ float specials_max_speed;            /* 6 */
    /* 0x0A0 */ float specials_air_gravity;          /* 0.005 */
    /* 0x0A4 */ float specials_air_terminal_vel;     /* 0.8 */
    /* 0x0A8 */ float specials_friction;             /* 0.07 */
    /* 0x0AC */ float specials_hold_gravity;         /* 0.1 */
    /* 0x0B0 */ float specials_hold_air_decel;       /* 0.07 */
    /* 0x0B4 */ float specials_hold_terminal_vel;    /* 1 */
    /* 0x0B8 */ float specials_fall_mobility;        /* 0.2 */
    /* 0x0BC */ float specials_landing_lag;          /* 30 */
    /* 0x0C0 */ float specials_end_anim_rate;        /* 2 */
    /* 0x0C4 */ int speciallw_release_frames;        /* 20: no B for this long cancels */
    /* 0x0C8 */ int speciallw_charge_interval;       /* 20 */
    /* 0x0CC */ int speciallw_first_charge_delay;    /* 4 */
    /* 0x0D0 */ int speciallw_max_charge_level;      /* 2 */
    /* 0x0D4 */ int x0D4;                            /* 6, unused */
    /* 0x0D8 */ float speciallw_charge_gravity;      /* 0.1 */
    /* 0x0DC */ float speciallw_charge_air_decel;    /* 0.5 */
    /* 0x0E0 */ float speciallw_charge_terminal_vel; /* 1 */
    /* 0x0E4 */ int speciallw_max_hold_frames;       /* 300 */
    /* 0x0E8 */ int speciallw_run_frames;            /* 100 */
    /* 0x0EC */ float speciallw_run_accel;           /* 0.5 */
    /* 0x0F0 */ float speciallw_run_speed_lv2;       /* 4 */
    /* 0x0F4 */ float speciallw_run_speed_lv1;       /* 3.25 */
    /* 0x0F8 */ float speciallw_run_speed_lv0;       /* 2.5 */
    /* 0x0FC */ float speciallw_turn_accel;          /* 0.14 */
    /* 0x100 */ float speciallw_turn_exit_speed;     /* 1 */
    /* 0x104 */ int x104;                            /* 10, unused */
    /* 0x108 */ float speciallw_jump_vel_y;          /* 2 */
    /* 0x10C */ int speciallw_jump_attack_lockout;   /* 3 */
    /* 0x110 */ float speciallw_air_drift;           /* 0.02, also the dive's fall mobility */
    /* 0x114 */ float speciallw_air_max_vel_x;       /* 2 */
    /* 0x118 */ float speciallw_air_gravity;         /* 0.1 */
    /* 0x11C */ float speciallw_air_terminal_vel;    /* 2.2 */
    /* 0x120 */ float speciallw_brake_decel;         /* 0.1 */
    /* 0x124 */ float speciallw_dive_vel_x;          /* 2 */
    /* 0x128 */ int x128;                            /* 0, unused */
    /* 0x12C */ int speciallw_dive_frames;           /* 40 */
    /* 0x130 */ float speciallw_stopwall_anim_rate;  /* 1.5 */
    /* 0x134 */ float speciallw_landing_lag;         /* 15 */
    /* 0x138 */ float speciallw_run_damage_min;      /* 4 */
    /* 0x13C */ float speciallw_run_damage_max;      /* 8 */
    /* 0x140 */ float speciallw_jump_damage_min;     /* 3 */
    /* 0x144 */ float speciallw_jump_damage_max;     /* 5 */
} DISC_STRUCT ftSonic_DatAttrs;

_Static_assert(sizeof(ftSonic_DatAttrs) == 0x148, "Sonic attributes are 0x148 bytes on the disc");

/* "PlySonicColor" in the costume file: the trail tint and the running shoe konst color. */
typedef struct ftSonic_ColorData {
    /* +0 */ GXColor trail_diffuse;
    /* +4 */ GXColor shoe_konst;
} DISC_STRUCT ftSonic_ColorData;

/* ---------------------------------------------------------------------------------------------
 * Per-fighter state in fp->u (console fp+222C) and per-move state in fp->mv (console fp+2340).
 * The motion-var layouts keep the console's relative offsets (every field is 4 bytes or less)
 * so fields left over from one move alias the next move's fields exactly as they did there.
 * ------------------------------------------------------------------------------------------- */
typedef struct ftSonic_FighterVars {
    /* 222C */ HSD_GObj* spring_gobj;
    /* 2230 */ int specials_charge;           /* stored spin dash charge, survives the move */
    /* 2234 */ ftSonic_ColorData* color;      /* this costume's PlySonicColor, or NULL */
    /* 2238 */ Vec3 trail_pos;                /* bone 2 world position last trail frame */
    /* 2244 */ float trail_angle;
    /* 2248 */ int run_shoes_spawned;
} ftSonic_FighterVars;

typedef struct ftSonic_SpecialNVars {
    /* 2340 */ int charge_frames;
    /* 2344 */ int timer;
    /* 2348 */ float angle;
    /* 234C */ Vec3 target; /* z is never written by the homing code (see NOTES) */
} ftSonic_SpecialNVars;

typedef struct ftSonic_SpecialSVars {
    /* 2340 */ int dust_timer;
    /* 2344 */ int hold_frames;
    /* 2348 */ float charge_ratio;
    /* 234C */ int cancel;
} ftSonic_SpecialSVars;

typedef struct ftSonic_SpecialLwVars {
    /* 2340 */ s8 charge_level; /* -1 until the first charge tick */
    /* 2341 */ u8 left_ground;  /* the jump came from rolling off an edge */
    /* 2342 */ u8 can_jump;     /* a double jump was left on entry */
    /* 2343 */ u8 pad_2343;
    /* 2344 */ s16 release_timer;
    /* 2346 */ s16 charge_timer;
    /* 2348 */ s16 run_life;
    /* 234A */ s16 dive_life;
    /* 234C */ float run_speed;
    /* 2350 */ s16 jump_lockout;
    /* 2352 */ s16 pad_2352;
    /* 2354 */ s16 x2354;       /* written (0, 4), never read */
    /* 2356 */ s16 hold_timer;
} ftSonic_SpecialLwVars;

typedef union ftSonic_MotionVars {
    ftSonic_SpecialNVars specialn;
    ftSonic_SpecialSVars specials;
    ftSonic_SpecialLwVars speciallw;
} ftSonic_MotionVars;

/* The spring's item vars (it->xDD4_itemVar, console item+DD4). */
typedef struct ftSonic_SpringVars {
    /* DD4 */ float bounce_vel; /* launch speed given to the next jumper, decays per bounce */
    /* DD8 */ HSD_GObj* owner;  /* the Sonic that placed it */
} ftSonic_SpringVars;

#define ftSn_SpringVars(ip) ((ftSonic_SpringVars*) &(ip)->xDD4_itemVar)

static inline ftSonic_FighterVars* ftSn_FV(Fighter* fp)
{
    return (ftSonic_FighterVars*) &fp->u;
}

static inline ftSonic_MotionVars* ftSn_MV(Fighter* fp)
{
    return (ftSonic_MotionVars*) &fp->mv;
}

static inline ftSonic_DatAttrs* ftSn_DA(Fighter* fp)
{
    return (ftSonic_DatAttrs*) fp->dat_attrs;
}

/* One 32-bit pointer slot of disc data (ftData->x48_items entries). */
typedef DISC_PTR(void) ftSn_DiscSlot;

/* Vec3_DistSquared */
static inline float ftSn_Vec3_DistSquared(const Vec3* a, const Vec3* b)
{
    float dx = a->x - b->x;
    float dy = a->y - b->y;
    float dz = a->z - b->z;
    return dx * dx + dy * dy + dz * dz;
}

/* The ECB box the air specials collide with, converted to host order for ft_800824A0. */
static inline ftCollisionBox ftSn_CollBox(Fighter* fp)
{
    ftSonic_DatAttrs* da = ftSn_DA(fp);
    ftCollisionBox box;
    box.top = da->coll_box.top;
    box.bottom = da->coll_box.bottom;
    box.left = BE_VEC2(da->coll_box.left);
    box.right = BE_VEC2(da->coll_box.right);
    return box;
}

/* ---------------------------------------------------------------------------------------------
 * Services m-ex provides to fighter code through its runtime (MEX_GetData, MEX_GetFtItemID,
 * MEX_IndexFighterItem). The integration layer fills these in; a NULL entry degrades gracefully
 * (no shoe colors, no spring, no alternate win line). See NOTES.md.
 * ------------------------------------------------------------------------------------------- */
typedef struct MuAkSonicHooks {
    /* MEX_GetData(MXDT_FTCOSTUMEARCHIVE)[kind][costume].archive */
    HSD_Archive* (*costume_archive)(FighterKind kind, int costume);
    /* MEX_IndexFighterItem(kind, article, index): registers article `index` of this fighter */
    void (*index_fighter_item)(FighterKind kind, void* article, int index);
    /* MEX_GetFtItemID(fighter, index): the item kind article `index` was given */
    int (*fighter_item_kind)(HSD_GObj* fighter_gobj, int index);
    /* MEX_GetData(MXDT_FTNAME)[external id]: the fighter's display name */
    const char* (*fighter_name)(int ckind);
} MuAkSonicHooks;

extern MuAkSonicHooks mu_ak_sonic_hooks;

/* The spring article's item callbacks (itFunction in PlSn.dat). */
extern ItemStateTable ftSn_Spring_StateTable[3];
extern const ItemLogicTable ftSn_Spring_LogicTable;

/* ---- sonic.c ---- */
void ftSn_Init_OnLoad(HSD_GObj* gobj);
void ftSn_Init_OnDeath(HSD_GObj* gobj);
void ftSn_Init_OnDestroy(HSD_GObj* gobj);
void ftSn_Init_OnItemPickup(HSD_GObj* gobj, bool catch_item);
void ftSn_Init_OnItemInvisible(HSD_GObj* gobj);
void ftSn_Init_OnItemVisible(HSD_GObj* gobj);
void ftSn_Init_OnItemDrop(HSD_GObj* gobj, bool drop_item);
void ftSn_Init_OnItemCatch(HSD_GObj* gobj, bool catch_item);
void ftSn_Init_OnUnknownItemRelated(HSD_GObj* gobj, bool drop_item);
void ftSn_Init_EyeTextureDamaged(HSD_GObj* gobj);
void ftSn_Init_EyeTextureNormal(HSD_GObj* gobj);
void ftSn_Init_OnFrame(HSD_GObj* gobj);
void ftSn_Init_OnActionStateChange(HSD_GObj* gobj);
void ftSn_Init_ResetAttributes(HSD_GObj* gobj);
void ftSn_Init_EnterDoubleJump(HSD_GObj* gobj);
void ftSn_Init_OnSmashHi(HSD_GObj* gobj);
bool ftSn_CheckSameTeam(int team, int slot);

/* ---- sonic_effects.c ---- */
void ftSn_GFXTrail(HSD_GObj* gobj);
void ftSn_GFXTrailLoop(HSD_GObj* gobj);
void ftSn_GFXSpin(HSD_GObj* gobj);
void ftSn_GFXSpinAndTrail(HSD_GObj* gobj);
void ftSn_GFXSpinAndTrailVelocityDirection(HSD_GObj* gobj);
void ftSn_UpdateTrailPosAndRot(HSD_GObj* gobj);
void ftSn_SpawnTrailEffect(HSD_GObj* gobj, bool spawn);
void ftSn_RunEffectCallback(EF_Effect* effect);
void ftSn_ColorShoes(HSD_JObj* jobj, int dobj_index, ftSonic_ColorData* color);
void ftSn_SetEffectCallbacks(Fighter* fp, HSD_GObjEvent on_hit);
void ftSn_GXLink(HSD_GObj* gobj, intptr_t pass);

/* ---- sonic_specialn.c (homing attack) ---- */
void ftSn_SpecialNStart_Enter(HSD_GObj* gobj);
void ftSn_SpecialNStart_Anim(HSD_GObj* gobj);
void ftSn_SpecialNStart_IASA(HSD_GObj* gobj);
void ftSn_SpecialNStart_Phys(HSD_GObj* gobj);
void ftSn_SpecialNStart_Coll(HSD_GObj* gobj);
void ftSn_SpecialNCharge_Anim(HSD_GObj* gobj);
void ftSn_SpecialNCharge_IASA(HSD_GObj* gobj);
void ftSn_SpecialNCharge_Phys(HSD_GObj* gobj);
void ftSn_SpecialNCharge_Coll(HSD_GObj* gobj);
void ftSn_SpecialNAttackMiss_Anim(HSD_GObj* gobj);
void ftSn_SpecialNAttackMiss_IASA(HSD_GObj* gobj);
void ftSn_SpecialNAttackMiss_Phys(HSD_GObj* gobj);
void ftSn_SpecialNAttackMiss_Coll(HSD_GObj* gobj);
void ftSn_SpecialNAttack_Anim(HSD_GObj* gobj);
void ftSn_SpecialNAttack_IASA(HSD_GObj* gobj);
void ftSn_SpecialNAttack_Phys(HSD_GObj* gobj);
void ftSn_SpecialNAttack_Coll(HSD_GObj* gobj);
void ftSn_SpecialNCancel_Anim(HSD_GObj* gobj);
void ftSn_SpecialNCancel_IASA(HSD_GObj* gobj);
void ftSn_SpecialNCancel_Phys(HSD_GObj* gobj);
void ftSn_SpecialNCancel_Coll(HSD_GObj* gobj);
void ftSn_SpecialNLanding_Anim(HSD_GObj* gobj);
void ftSn_SpecialNLanding_IASA(HSD_GObj* gobj);
void ftSn_SpecialNLanding_Phys(HSD_GObj* gobj);
void ftSn_SpecialNLanding_Coll(HSD_GObj* gobj);
void ftSn_SpecialNRebound_Anim(HSD_GObj* gobj);
void ftSn_SpecialNRebound_IASA(HSD_GObj* gobj);
void ftSn_SpecialNRebound_Phys(HSD_GObj* gobj);
void ftSn_SpecialNRebound_Coll(HSD_GObj* gobj);
void ftSn_GXLink_DrawTarget(HSD_GObj* gobj);
void ftSn_GXLink_DrawRadius(HSD_GObj* gobj);

/* ---- sonic_specials.c (spin dash) ---- */
void ftSn_SpecialS_EnterAirOrGround(HSD_GObj* gobj);
void ftSn_SpecialSStart_Anim(HSD_GObj* gobj);
void ftSn_SpecialSStart_IASA(HSD_GObj* gobj);
void ftSn_SpecialSStart_Phys(HSD_GObj* gobj);
void ftSn_SpecialSStart_Coll(HSD_GObj* gobj);
void ftSn_SpecialSEnd_Anim(HSD_GObj* gobj);
void ftSn_SpecialSEnd_IASA(HSD_GObj* gobj);
void ftSn_SpecialSEnd_Phys(HSD_GObj* gobj);
void ftSn_SpecialSEnd_Coll(HSD_GObj* gobj);
void ftSn_SpecialAirSStart_Anim(HSD_GObj* gobj);
void ftSn_SpecialAirSStart_IASA(HSD_GObj* gobj);
void ftSn_SpecialAirSStart_Phys(HSD_GObj* gobj);
void ftSn_SpecialAirSStart_Coll(HSD_GObj* gobj);
void ftSn_SpecialAirSEnd_Anim(HSD_GObj* gobj);
void ftSn_SpecialAirSEnd_IASA(HSD_GObj* gobj);
void ftSn_SpecialAirSEnd_Phys(HSD_GObj* gobj);
void ftSn_SpecialAirSEnd_Coll(HSD_GObj* gobj);
void ftSn_SpecialSHold_Anim(HSD_GObj* gobj);
void ftSn_SpecialSHold_IASA(HSD_GObj* gobj);
void ftSn_SpecialSHold_Phys(HSD_GObj* gobj);
void ftSn_SpecialSHold_Coll(HSD_GObj* gobj);
void ftSn_SpecialS_Anim(HSD_GObj* gobj);
void ftSn_SpecialS_IASA(HSD_GObj* gobj);
void ftSn_SpecialS_Phys(HSD_GObj* gobj);
void ftSn_SpecialS_Coll(HSD_GObj* gobj);
void ftSn_SpecialAirS_Anim(HSD_GObj* gobj);
void ftSn_SpecialAirS_IASA(HSD_GObj* gobj);
void ftSn_SpecialAirS_Phys(HSD_GObj* gobj);
void ftSn_SpecialAirS_Coll(HSD_GObj* gobj);

/* ---- sonic_specialhi.c (spring jump) ---- */
void ftSn_SpecialHi_Enter(HSD_GObj* gobj);
void ftSn_SpecialHi_Anim(HSD_GObj* gobj);
void ftSn_SpecialHi_IASA(HSD_GObj* gobj);
void ftSn_SpecialHi_Phys(HSD_GObj* gobj);
void ftSn_SpecialHi_Coll(HSD_GObj* gobj);
void ftSn_SpawnSpring(HSD_GObj* gobj);

/* ---- sonic_speciallw.c (spin charge) ---- */
void ftSn_SpecialLw_EnterAirOrGround(HSD_GObj* gobj);
void ftSn_SpecialLwStart_Anim(HSD_GObj* gobj);
void ftSn_SpecialLwStart_IASA(HSD_GObj* gobj);
void ftSn_SpecialLwStart_Phys(HSD_GObj* gobj);
void ftSn_SpecialLwStart_Coll(HSD_GObj* gobj);
void ftSn_SpecialLwEnd_Anim(HSD_GObj* gobj);
void ftSn_SpecialLwEnd_IASA(HSD_GObj* gobj);
void ftSn_SpecialLwEnd_Phys(HSD_GObj* gobj);
void ftSn_SpecialLwEnd_Coll(HSD_GObj* gobj);
void ftSn_SpecialAirLwStart_Anim(HSD_GObj* gobj);
void ftSn_SpecialAirLwStart_IASA(HSD_GObj* gobj);
void ftSn_SpecialAirLwStart_Phys(HSD_GObj* gobj);
void ftSn_SpecialAirLwStart_Coll(HSD_GObj* gobj);
void ftSn_SpecialAirLwEnd_Anim(HSD_GObj* gobj);
void ftSn_SpecialAirLwEnd_IASA(HSD_GObj* gobj);
void ftSn_SpecialAirLwEnd_Phys(HSD_GObj* gobj);
void ftSn_SpecialAirLwEnd_Coll(HSD_GObj* gobj);
void ftSn_SpecialLwCharge_Anim(HSD_GObj* gobj);
void ftSn_SpecialLwCharge_IASA(HSD_GObj* gobj);
void ftSn_SpecialLwCharge_Phys(HSD_GObj* gobj);
void ftSn_SpecialLwCharge_Coll(HSD_GObj* gobj);
void ftSn_SpecialLwRun_Anim(HSD_GObj* gobj);
void ftSn_SpecialLwRun_IASA(HSD_GObj* gobj);
void ftSn_SpecialLwRun_Phys(HSD_GObj* gobj);
void ftSn_SpecialLwRun_Coll(HSD_GObj* gobj);
void ftSn_SpecialLwRunTurn_Anim(HSD_GObj* gobj);
void ftSn_SpecialLwRunTurn_IASA(HSD_GObj* gobj);
void ftSn_SpecialLwRunTurn_Phys(HSD_GObj* gobj);
void ftSn_SpecialLwRunTurn_Coll(HSD_GObj* gobj);
void ftSn_SpecialLwRunJump_Anim(HSD_GObj* gobj);
void ftSn_SpecialLwRunJump_IASA(HSD_GObj* gobj);
void ftSn_SpecialLwRunJump_Phys(HSD_GObj* gobj);
void ftSn_SpecialLwRunJump_Coll(HSD_GObj* gobj);
void ftSn_SpecialLwDive_Anim(HSD_GObj* gobj);
void ftSn_SpecialLwDive_IASA(HSD_GObj* gobj);
void ftSn_SpecialLwDive_Phys(HSD_GObj* gobj);
void ftSn_SpecialLwDive_Coll(HSD_GObj* gobj);
void ftSn_SpecialLwRunBrake_Anim(HSD_GObj* gobj);
void ftSn_SpecialLwRunBrake_IASA(HSD_GObj* gobj);
void ftSn_SpecialLwRunBrake_Phys(HSD_GObj* gobj);
void ftSn_SpecialLwRunBrake_Coll(HSD_GObj* gobj);
void ftSn_SpecialLwStopWall_Anim(HSD_GObj* gobj);
void ftSn_SpecialLwStopWall_IASA(HSD_GObj* gobj);
void ftSn_SpecialLwStopWall_Phys(HSD_GObj* gobj);
void ftSn_SpecialLwStopWall_Coll(HSD_GObj* gobj);

#endif
