/* ACE 2.0.0's Shadow, native (PlSh.dat, ftDataShadow; m-ex internal 56, external 55, native kind
 * 0x3F = 63).
 *
 * Shadow ships as m-ex PowerPC in PlSh.dat: one "ftFunction" block built without symbols
 * (0x5F4C bytes, 25 exports) and one "itFunction" block with symbols (article 0, the spring).
 * MxDt names one default only (slot 3, Sandbag's state table, which the file's own table
 * replaces), so every slot below is the file's own code. His effect file is EfShData.dat (index
 * 54, his own) and his sound bank 95 (shadow.ssm).
 *
 * The file is the Akaneia Sonic's source built again by another compiler with changes (0 percent
 * of the chunks are the same bytes, run-source/rel09-ace-native/out_clones.txt: 3 percent), so
 * nothing is called from akaneia/sonic: every routine is written again from the listing. The
 * Sonic port was read only to name what a routine is (NOTES.md says where they differ):
 *
 *   neutral  the homing attack: wind up (341, 342), a charge (343), then a dash at the nearest
 *            opponent or break-the-targets target in range (345) or a dive (344), and the
 *            cancel, landing, rebound and hit states (346 to 349)
 *   side     a spin dash with a stored charge (356 to 364)
 *   up       NEW, not Sonic's: a warp (Chaos Control). A short start (350 / 353) in which the
 *            stick can turn him, then 8 frames invisible and intangible moving along the stick
 *            (351 / 354), then an end state (352 / 355) that keeps a part of the speed
 *   down     the spin charge, roll, turn, jump, dive, brake and wall stop (365 to 376)
 *
 * Offsets in the comments (+0B1C) are offsets into the ftFunction block, "it0 +5C" into the
 * itFunction block of article 0, as in run-source/rel09-ace-native/shadow/listing.txt. The
 * ftFunction block has no symbols: those names are this port's. "fp+" is a console offset of the
 * Fighter. FUNCTIONS.md in that folder lists every function of both blocks. */
#ifndef MU_ACE_SHADOW_H
#define MU_ACE_SHADOW_H

#include <Runtime/platform.h>

#include <dolphin/gx/GXStruct.h>
#include <melee/ef/forward.h>
#include <melee/ft/forward.h>
#include <melee/ft/kinds/ftCommon/forward.h>
#include <melee/ft/kinds/ftCommon/types.h>
#include <melee/ft/types.h>
#include <melee/it/forward.h>
#include <melee/it/kinds/forward.h>
#include <melee/it/kinds/types.h>
#include <melee/it/types.h>
#include <sysdolphin/baselib/forward.h>

#include <stddef.h>

#include "../../akaneia/mu_ak_fighter.h"
#include "mu_disc.h"

/* ---- Action states (move_logic, +0184 to +0604): 36 rows numbered from 341 ---- */
enum ftSh_MotionState {
    ftSh_MS_SpecialNStart = 341,  /* 0x155: homing wind up, ground animation */
    ftSh_MS_SpecialAirNStart,     /* 0x156: homing wind up, air animation */
    ftSh_MS_SpecialNCharge,       /* 0x157: curled up, counting the charge */
    ftSh_MS_SpecialNAttackMiss,   /* 0x158: no target: the dive */
    ftSh_MS_SpecialNAttack,       /* 0x159: the homing dash */
    ftSh_MS_SpecialNCancel,       /* 0x15A: the dash ended without a hit */
    ftSh_MS_SpecialNLanding,      /* 0x15B */
    ftSh_MS_SpecialNRebound,      /* 0x15C: bounced off a wall, the floor or a shield */
    ftSh_MS_SpecialNHit,          /* 0x15D: bounce after the dash hit (row 8 = row 7) */
    ftSh_MS_SpecialHiStart,       /* 0x15E: warp start, ground */
    ftSh_MS_SpecialHiWarp,        /* 0x15F: warp, ground */
    ftSh_MS_SpecialHiEnd,         /* 0x160: warp end, ground */
    ftSh_MS_SpecialAirHiStart,    /* 0x161: warp start, air */
    ftSh_MS_SpecialAirHiWarp,     /* 0x162: warp, air */
    ftSh_MS_SpecialAirHiEnd,      /* 0x163: warp end, air */
    ftSh_MS_SpecialSStart,        /* 0x164: spin dash start, ground */
    ftSh_MS_SpecialSEnd,          /* 0x165: spin dash end, ground */
    ftSh_MS_SpecialAirSStart,     /* 0x166 */
    ftSh_MS_SpecialAirSEnd,       /* 0x167 */
    ftSh_MS_SpecialSHold,         /* 0x168: charging, ground and air */
    ftSh_MS_SpecialS,             /* 0x169: the dash, ground */
    ftSh_MS_SpecialAirS,          /* 0x16A: the dash, air */
    ftSh_MS_SpecialSMax,          /* 0x16B: the dash from a full charge, ground (row 22 = 20) */
    ftSh_MS_SpecialAirSMax,       /* 0x16C: the same, air (row 23 = row 21) */
    ftSh_MS_SpecialLwStart,       /* 0x16D: spin charge start, ground */
    ftSh_MS_SpecialLwEnd,         /* 0x16E */
    ftSh_MS_SpecialAirLwStart,    /* 0x16F */
    ftSh_MS_SpecialAirLwEnd,      /* 0x170 */
    ftSh_MS_SpecialLwCharge,      /* 0x171: mashing B, ground and air */
    ftSh_MS_SpecialLwRun,         /* 0x172: the roll */
    ftSh_MS_SpecialLwRunTurn,     /* 0x173 */
    ftSh_MS_SpecialLwRunJump,     /* 0x174: jumped or rolled off an edge */
    ftSh_MS_SpecialLwDive,        /* 0x175: released in the air */
    ftSh_MS_SpecialLwRunBrake,    /* 0x176: the roll ran out (animation 14, the common RunBrake) */
    ftSh_MS_SpecialLwStopWallL,   /* 0x177: rolled into a wall, facing left */
    ftSh_MS_SpecialLwStopWallR,   /* 0x178: the same, facing right (row 35 = row 34) */
    ftSh_MS_End
};

#define FTSH_MS_COUNT (ftSh_MS_End - ftSh_MS_SpecialNStart)
_Static_assert(FTSH_MS_COUNT == 36, "36 rows in PlSh.dat's move_logic");
_Static_assert(ftSh_MS_SpecialNStart == ftCo_MS_Count, "m-ex numbers a fighter's rows from 341");

/* ---- Special attributes (ftData->ext_attr, 0x154 bytes, copied big-endian into the game's
 * attribute buffer by [onload] and [onrespawn]: read through this DISC_STRUCT). Names from how
 * the code uses each value; the value on ACE 2.0.0 is noted (data.txt). +118 has no reader. ---- */
typedef struct ftSh_DatAttrs {
    /* 0x000 */ ftCollisionBox_BE coll_box;          /* 5, 0, (-2, 2), (2, 2) */
    /* 0x018 */ float specialn_charge_anim_rate;     /* 40 */
    /* 0x01C */ s32 specialn_min_charge_frames;      /* 20 */
    /* 0x020 */ s32 specialn_max_charge_frames;      /* 40 */
    /* 0x024 */ float specialn_charge_vel_y;         /* 0.55 */
    /* 0x028 */ float specialn_attack_anim_rate;     /* 50 */
    /* 0x02C */ float specialn_miss_speed;           /* 3.5 */
    /* 0x030 */ float specialn_search_range;         /* 58 */
    /* 0x034 */ float specialn_target_offset_y;      /* 3 */
    /* 0x038 */ float specialn_attack_speed;         /* 5 */
    /* 0x03C */ s32 specialn_attack_frames;          /* 17 */
    /* 0x040 */ float specialn_aim_stick_x;          /* 5 */
    /* 0x044 */ float specialn_aim_stick_y;          /* 5 */
    /* 0x048 */ float specialn_cancel_air_decel;     /* 0.7 */
    /* 0x04C */ float specialn_rebound_mul_x;        /* 0.7 */
    /* 0x050 */ float specialn_rebound_gravity;      /* 0.185 */
    /* 0x054 */ float specialn_rebound_terminal_vel; /* 1.7 */
    /* 0x058 */ float specialn_rebound_air_decel;    /* 0.1 */
    /* 0x05C */ float specialn_rebound_max_vel_x;    /* 1.5 */
    /* 0x060 */ float specialn_rebound_max_vel_y;    /* 2.25 */
    /* 0x064 */ float specialn_landing_decel;        /* 1.1 */
    /* 0x068 */ float specialhi_air_div_x;           /* 2: the air start divides vel.x by it */
    /* 0x06C */ float specialhi_air_div_y;           /* 2: and vel.y */
    /* 0x070 */ float specialhi_start_gravity;       /* 0.1: the air start's fall */
    /* 0x074 */ float specialhi_start_terminal_vel;  /* 0.75 */
    /* 0x078 */ s32 specialhi_warp_frames;           /* 8 */
    /* 0x07C */ float specialhi_warp_min_frames;     /* 3: frames of the air warp before a landing
                                                      * ends it on the ground */
    /* 0x080 */ float specialhi_stick_min;           /* 0.5: a stick under this warps straight up */
    /* 0x084 */ float specialhi_speed_mul;           /* 2.08 */
    /* 0x088 */ float specialhi_speed_add;           /* 3.2: speed = mul * stick + add */
    /* 0x08C */ float specialhi_end_drift;           /* 0.4, times the air drift max */
    /* 0x090 */ s32 specialhi_bounce_angle;          /* 45: degrees past 90 for a surface hit */
    /* 0x094 */ float specialhi_end_vel_mul;         /* 0.3: speed kept into the end state */
    /* 0x098 */ float specialhi_fall_mobility;       /* 0.57 */
    /* 0x09C */ float specialhi_landing_lag;         /* 20 */
    /* 0x0A0 */ u32 specialhi_turn_frames;           /* 6: the start can turn him before this */
    /* 0x0A4 */ float specialhi_turn_stick;          /* 0.15 */
    /* 0x0A8 */ s32 specials_min_hold_frames;        /* 10 */
    /* 0x0AC */ s32 specials_max_charge;             /* 75 */
    /* 0x0B0 */ s32 specials_dust_interval;          /* 3 */
    /* 0x0B4 */ float specials_min_speed;            /* 4 */
    /* 0x0B8 */ float specials_max_speed;            /* 6 */
    /* 0x0BC */ float specials_air_gravity;          /* 0.005 */
    /* 0x0C0 */ float specials_air_terminal_vel;     /* 0.8 */
    /* 0x0C4 */ float specials_friction;             /* 0.07 */
    /* 0x0C8 */ float specials_hold_gravity;         /* 0.1 */
    /* 0x0CC */ float specials_hold_air_decel;       /* 0.07 */
    /* 0x0D0 */ float specials_hold_terminal_vel;    /* 1 */
    /* 0x0D4 */ float specials_fall_mobility;        /* 0.2 */
    /* 0x0D8 */ float specials_landing_lag;          /* 30 */
    /* 0x0DC */ s32 speciallw_release_frames;        /* 20: no B for this long cancels */
    /* 0x0E0 */ s32 speciallw_charge_interval;       /* 10 */
    /* 0x0E4 */ s32 speciallw_first_charge_delay;    /* 10 */
    /* 0x0E8 */ s32 speciallw_max_charge_level;      /* 2 */
    /* 0x0EC */ s32 speciallw_effect_interval;       /* 6 */
    /* 0x0F0 */ float speciallw_charge_gravity;      /* 0.1 */
    /* 0x0F4 */ float speciallw_charge_air_decel;    /* 0.5 */
    /* 0x0F8 */ float speciallw_charge_terminal_vel; /* 1 */
    /* 0x0FC */ s32 speciallw_sfx_interval;          /* 11 */
    /* 0x100 */ s32 speciallw_run_frames;            /* 100 */
    /* 0x104 */ float speciallw_run_accel;           /* 0.5 */
    /* 0x108 */ float speciallw_run_speed_max;       /* 4 */
    /* 0x10C */ float speciallw_run_speed_min;       /* 2.5 */
    /* 0x110 */ float speciallw_turn_accel;          /* 0.14 */
    /* 0x114 */ float speciallw_turn_exit_speed;     /* 1 */
    /* 0x118 */ s32 x118;                            /* 10, no reader */
    /* 0x11C */ float speciallw_jump_vel_y;          /* 2 */
    /* 0x120 */ s32 speciallw_jump_lockout;          /* 3 */
    /* 0x124 */ float speciallw_air_drift;           /* 0.1, also the dive's fall mobility */
    /* 0x128 */ float speciallw_air_max_vel_x;       /* 2 */
    /* 0x12C */ float speciallw_air_gravity;         /* 0.1 */
    /* 0x130 */ float speciallw_air_terminal_vel;    /* 2.2 */
    /* 0x134 */ float speciallw_brake_decel;         /* 0.1 */
    /* 0x138 */ float speciallw_dive_vel_x;          /* 2 */
    /* 0x13C */ float speciallw_stopwall_anim_rate;  /* 1.5 */
    /* 0x140 */ float speciallw_landing_lag;         /* 15 */
    /* 0x144 */ float speciallw_run_damage_min;      /* 4 */
    /* 0x148 */ float speciallw_run_damage_max;      /* 8 */
    /* 0x14C */ float speciallw_jump_damage_min;     /* 3 */
    /* 0x150 */ float speciallw_jump_damage_max;     /* 5 */
} DISC_STRUCT ftSh_DatAttrs;

_Static_assert(sizeof(ftSh_DatAttrs) == 0x154, "PlSh.dat's attribute block is 0x154 bytes");
_Static_assert(__builtin_offsetof(ftSh_DatAttrs, specials_max_charge) == 0xAC, "console offsets");
_Static_assert(__builtin_offsetof(ftSh_DatAttrs, speciallw_jump_damage_max) == 0x150, "console offsets");

static inline ftSh_DatAttrs* ftSh_DA(Fighter* fp)
{
    return (ftSh_DatAttrs*) fp->dat_attrs;
}

/* The collision box the air specials pass to ft_800824A0: the console hands it the address of
 * the attribute block itself; natively the box is converted to host order first. */
static inline ftCollisionBox ftSh_CollBox(Fighter* fp)
{
    return ftCollisionBox_FromDisc(&ftSh_DA(fp)->coll_box);
}

/* "PlySonicColor" in the costume file (the name is Sonic's, kept by the file): the trail tint.
 * The second word (Sonic's shoe color) has no reader in PlSh.dat. */
typedef struct ftSh_ColorData {
    /* +0 */ GXColor trail_diffuse;
    /* +4 */ GXColor x4;
} DISC_STRUCT ftSh_ColorData;

/* Fighter variables. Comments name the console offsets; native pointers have host width. */
typedef struct ftSh_FighterVars {
    /* 222C */ HSD_GObj* spring_gobj;    /* destroyed by [onunknown]; nothing in PlSh.dat sets it */
    /* 2230 */ s32 specials_charge;      /* the stored spin dash charge */
    /* 2234 */ ftSh_ColorData* color;    /* this costume's PlySonicColor, or NULL */
    /* 2238 */ Vec3 trail_pos;           /* bone 2 world position, last trail frame */
    /* 2244 */ float trail_angle;
    /* 2248 */ s32 run_shoes_spawned;    /* the run effect (5006) was spawned in this Run */
} ftSh_FighterVars;

static inline ftSh_FighterVars* ftSh_FV(Fighter* fp)
{
    return (ftSh_FighterVars*) &fp->u;
}

/* ---- State variables (fp->mv, console fp+2340), one layout per move. Every field is 4 bytes
 * so the console offsets hold and a word left by one move is the same word in the next. ---- */
typedef struct ftSh_SpecialNVars {
    /* 2340 */ s32 charge_frames;
    /* 2344 */ s32 timer;
    /* 2348 */ float angle;
    /* 234C */ Vec3 target; /* z is never written (the dash's distance reads it) */
} ftSh_SpecialNVars;

typedef struct ftSh_SpecialHiVars {
    /* 2340 */ s32 warp_frames;
    /* 2344 */ float stick_x;    /* written, no reader */
    /* 2348 */ float stick_y;    /* written, no reader */
    /* 234C */ s32 coll_frames;  /* frames of collision in the air warp, from 0 at the entry */
    /* 2350 */ float vel_x;      /* the warp's speed, kept at its end */
    /* 2354 */ float vel_y;
    /* 2358 */ float gr_vel;
} ftSh_SpecialHiVars;

typedef struct ftSh_SpecialSVars {
    /* 2340 */ s32 dust_timer;
    /* 2344 */ s32 hold_frames;
    /* 2348 */ float charge_ratio;
} ftSh_SpecialSVars;

typedef struct ftSh_SpecialLwVars {
    /* 2340 */ s32 release_timer;
    /* 2344 */ s32 charge_timer;
    /* 2348 */ s32 level;          /* -1 until the first charge */
    /* 234C */ s32 run_life;       /* the roll's frames, also the dive's */
    /* 2350 */ float run_speed;
    /* 2354 */ s32 jump_lockout;
    /* 2358 */ s32 x2358;
    /* 235C */ s32 fx_timer;       /* frames to the next charge effect */
    /* 2360 */ s32 sfx_timer;      /* frames to the next charge sound */
    /* 2364 */ s32 left_ground;    /* the jump came from rolling off an edge */
} ftSh_SpecialLwVars;

typedef union ftSh_MotionVars {
    ftSh_SpecialNVars specialn;
    ftSh_SpecialHiVars specialhi;
    ftSh_SpecialSVars specials;
    ftSh_SpecialLwVars speciallw;
} ftSh_MotionVars;

_Static_assert(__builtin_offsetof(ftSh_SpecialLwVars, left_ground) == 0x2364 - 0x2340, "console offsets");
_Static_assert(__builtin_offsetof(ftSh_SpecialHiVars, gr_vel) == 0x2358 - 0x2340, "console offsets");

static inline ftSh_MotionVars* ftSh_MV(Fighter* fp)
{
    return (ftSh_MotionVars*) &fp->mv;
}

/* One 32-bit pointer slot of disc data (ftData->x48_items entries). */
typedef DISC_PTR(void) ftSh_DiscSlot;

/* ---- the console's constant pool (+3AA0 to +3AE0, +4894 to +48A8) ---- */
#define FTSH_FLT_MAX 0x1.ffffdcp+127f        /* +3AB8, the word 0x7F7FFFEE */
#define FTSH_HALF_PI_F 0x1.921fbp+0f          /* +3ABC, 1.570796 as a single */
#define FTSH_TAU 0x1.921fb54442d18p+2         /* +4894, the double 6.283185307179586 */
#define FTSH_DEG2RAD 0x1.1df4663f11168p-6     /* +489C, the double 0.01745328888888889 */
#define FTSH_PI 0x1.921fb54442d18p+1          /* +48A4, the double pi */

/* The m-ex flags word the in-move ground and air swaps pass (0x0C4C508A / 0x0C4C5080). */
#define FTSH_MF_SWAP_A ((MotionFlags) 0x0C4C508A)
#define FTSH_MF_SWAP_B ((MotionFlags) 0x0C4C5080)

/* m-ex ids of his own files (EfShData.dat, index 54, which his MxDt entry names; bank 95). */
enum {
    ftSh_Ef_Spin = 5000,       /* 0x1388: the spin ball on TransN */
    ftSh_Ef_Trail = 5001,      /* 0x1389: a trail segment */
    ftSh_Ef_Dust = 5003,       /* 0x138B: spin dash dust */
    ftSh_Ef_RunShoes = 5006,   /* 0x138E: the run effect */
    ftSh_Ef_LwLevel0 = 6004,   /* 0x1774 to 0x1776: the spin charge's three levels */
};
enum {
    ftSh_Sfx_Jump = 5001,       /* 0x1389 */
    ftSh_Sfx_LwCharge = 5007,   /* 0x138F */
    ftSh_Sfx_Spring = 5019,     /* 0x139B */
    ftSh_Sfx_SChargeFull = 5049, /* 0x13B9 */
    ftSh_Sfx_LwLoop = 5097,     /* 0x13E9 */
};

/* ---- Articles: MxDt's item lookup has 351, 352, 353; [onload] registers only article 0 (the
 * file's ftData+0x48 list entry 0, the only one with code). ---- */
enum {
    ftSh_Article_Spring = 0,
    ftSh_Article_Count = 1,
};

/* The spring's item variables (ip->xDD4_itemVar, console item+DD4). */
typedef struct itSh_SpringVars {
    /* DD4 */ float bounce_vel;
    /* DD8 */ HSD_GObj* owner;   /* read by its OnDestroy; nothing in PlSh.dat writes it */
} itSh_SpringVars;

/* ---- shadow.c ---- */
extern const MotionState ftSh_Init_MotionStateTable[FTSH_MS_COUNT];
/* +3B84: a branch to efLib_DestroyAll, used as a pointer of its own (take_dmg_cb, death2_cb). */
void ftSh_DestroyAll(HSD_GObj* gobj);
/* +48AC: the stored spin dash charge dropped, then efLib_DestroyAll. */
void ftSh_SpecialS_OnHit(HSD_GObj* gobj);
/* +596C: empty (the spin dash's deal_dmg_cb). */
void ftSh_Nothing(HSD_GObj* gobj);
/* +3AE4 and +58C8 (the same 30 words): the warp's accessory. */
void ftSh_GFXWarp(HSD_GObj* gobj);
/* +3F48 */ void ftSh_GFXSpin(HSD_GObj* gobj);
/* +3C2C */ void ftSh_GFXTrail(HSD_GObj* gobj);
/* +55AC */ void ftSh_UpdateTrailPosAndRot(HSD_GObj* gobj);
/* +5608 */ void ftSh_GFXTrailLoop(HSD_GObj* gobj);
/* +5C90 */ void ftSh_GFXSpinAndTrailVelocityDirection(HSD_GObj* gobj);
/* +5DD4 */ void ftSh_GFXSpinAndTrail(HSD_GObj* gobj);
/* +5A64, over any trail fields: the fighter's (color from the costume) and the Kirby hat's. */
void ftSh_SpawnTrailEffectAt(HSD_GObj* gobj, bool spawn, Vec3* trail_pos, float* trail_angle,
                             const GXColor* color);
/* +3DD8, +3C78 */
void ftSh_GXLink_DrawRadius(HSD_GObj* gobj);
void ftSh_GXLink_DrawTarget(HSD_GObj* gobj);

/* ---- shadow_specialn.c ---- */
void ftSh_SpecialN_Enter(HSD_GObj* gobj);
void ftSh_SpecialNStart_Anim(HSD_GObj* gobj);
void ftSh_SpecialN_Empty(HSD_GObj* gobj);
void ftSh_SpecialNStart_Coll(HSD_GObj* gobj);
void ftSh_SpecialNCharge_Anim(HSD_GObj* gobj);
void ftSh_SpecialNCharge_Coll(HSD_GObj* gobj);
void ftSh_SpecialNAttackMiss_Anim(HSD_GObj* gobj);
void ftSh_SpecialNDash_Coll(HSD_GObj* gobj);
void ftSh_SpecialNAttack_Anim(HSD_GObj* gobj);
void ftSh_SpecialNCancel_Anim(HSD_GObj* gobj);
void ftSh_SpecialNCancel_IASA(HSD_GObj* gobj);
void ftSh_SpecialNCancel_Phys(HSD_GObj* gobj);
void ftSh_SpecialNCancel_Coll(HSD_GObj* gobj);
void ftSh_SpecialNLanding_Anim(HSD_GObj* gobj);
void ftSh_SpecialNLanding_IASA(HSD_GObj* gobj);
void ftSh_SpecialNLanding_Phys(HSD_GObj* gobj);
void ftSh_SpecialNLanding_Coll(HSD_GObj* gobj);
void ftSh_SpecialNRebound_Anim(HSD_GObj* gobj);
void ftSh_SpecialNRebound_IASA(HSD_GObj* gobj);
void ftSh_SpecialNRebound_Phys(HSD_GObj* gobj);
void ftSh_SpecialNRebound_Coll(HSD_GObj* gobj);
/* +3FF0's search with the values of `da` (Shadow's attributes or the Kirby hat's, the same
 * first 0x68 bytes): true and the point when an opponent (or a break-the-targets target) is in
 * range. */
bool ftSh_SpecialN_SearchTarget(HSD_GObj* gobj, float search_range, float target_offset_y,
                                float* out_x, float* out_y);

/* ---- shadow_specialhi.c ---- */
void ftSh_SpecialHi_Enter(HSD_GObj* gobj);
void ftSh_SpecialAirHi_Enter(HSD_GObj* gobj);
void ftSh_SpecialHiStart_Anim(HSD_GObj* gobj);
void ftSh_SpecialHiStart_Coll(HSD_GObj* gobj);
void ftSh_SpecialHiWarp_Anim(HSD_GObj* gobj);
void ftSh_SpecialHiWarp_Coll(HSD_GObj* gobj);
void ftSh_SpecialHiEnd_Anim(HSD_GObj* gobj);
void ftSh_SpecialHiEnd_Coll(HSD_GObj* gobj);
void ftSh_SpecialAirHiStart_Anim(HSD_GObj* gobj);
void ftSh_SpecialAirHiStart_Phys(HSD_GObj* gobj);
void ftSh_SpecialAirHiStart_Coll(HSD_GObj* gobj);
void ftSh_SpecialAirHiWarp_Anim(HSD_GObj* gobj);
void ftSh_SpecialAirHiWarp_Coll(HSD_GObj* gobj);
void ftSh_SpecialAirHiEnd_Anim(HSD_GObj* gobj);
void ftSh_SpecialAirHiEnd_Phys(HSD_GObj* gobj);
void ftSh_SpecialAirHiEnd_Coll(HSD_GObj* gobj);

/* ---- shadow_specials.c ---- */
void ftSh_SpecialS_Enter(HSD_GObj* gobj);
void ftSh_SpecialSStart_Anim(HSD_GObj* gobj);
void ftSh_SpecialSStart_Coll(HSD_GObj* gobj);
void ftSh_SpecialSEnd_Anim(HSD_GObj* gobj);
void ftSh_SpecialSEnd_Coll(HSD_GObj* gobj);
void ftSh_SpecialAirSStart_Coll(HSD_GObj* gobj);
void ftSh_SpecialAirSEnd_Anim(HSD_GObj* gobj);
void ftSh_SpecialAirSEnd_Phys(HSD_GObj* gobj);
void ftSh_SpecialAirSEnd_Coll(HSD_GObj* gobj);
void ftSh_SpecialSHold_Anim(HSD_GObj* gobj);
void ftSh_SpecialSHold_IASA(HSD_GObj* gobj);
void ftSh_SpecialSHold_Phys(HSD_GObj* gobj);
void ftSh_SpecialSHold_Coll(HSD_GObj* gobj);
void ftSh_SpecialS_Anim(HSD_GObj* gobj);
void ftSh_SpecialS_Phys(HSD_GObj* gobj);
void ftSh_SpecialS_Coll(HSD_GObj* gobj);
void ftSh_SpecialAirS_Anim(HSD_GObj* gobj);
void ftSh_SpecialAirS_Phys(HSD_GObj* gobj);
void ftSh_SpecialAirS_Coll(HSD_GObj* gobj);

/* ---- shadow_speciallw.c ---- */
void ftSh_SpecialLw_Enter(HSD_GObj* gobj);
void ftSh_SpecialLwStart_Anim(HSD_GObj* gobj);
void ftSh_SpecialLwStart_Coll(HSD_GObj* gobj);
void ftSh_SpecialLwEnd_Anim(HSD_GObj* gobj);
void ftSh_SpecialLwEnd_Coll(HSD_GObj* gobj);
void ftSh_SpecialAirLwStart_Anim(HSD_GObj* gobj);
void ftSh_SpecialAirLwStart_Coll(HSD_GObj* gobj);
void ftSh_SpecialAirLwEnd_Anim(HSD_GObj* gobj);
void ftSh_SpecialAirLwEnd_Coll(HSD_GObj* gobj);
void ftSh_SpecialLwCharge_Anim(HSD_GObj* gobj);
void ftSh_SpecialLwCharge_IASA(HSD_GObj* gobj);
void ftSh_SpecialLwCharge_Phys(HSD_GObj* gobj);
void ftSh_SpecialLwCharge_Coll(HSD_GObj* gobj);
void ftSh_SpecialLwRun_Anim(HSD_GObj* gobj);
void ftSh_SpecialLwRun_IASA(HSD_GObj* gobj);
void ftSh_SpecialLwRun_Phys(HSD_GObj* gobj);
void ftSh_SpecialLwRun_Coll(HSD_GObj* gobj);
void ftSh_SpecialLwRunTurn_IASA(HSD_GObj* gobj);
void ftSh_SpecialLwRunTurn_Phys(HSD_GObj* gobj);
void ftSh_SpecialLwRunTurn_Coll(HSD_GObj* gobj);
void ftSh_SpecialLwRunJump_IASA(HSD_GObj* gobj);
void ftSh_SpecialLwRunJump_Phys(HSD_GObj* gobj);
void ftSh_SpecialLwRunJump_Coll(HSD_GObj* gobj);
void ftSh_SpecialLwDive_Anim(HSD_GObj* gobj);
void ftSh_SpecialLwDive_Phys(HSD_GObj* gobj);
void ftSh_SpecialLwDive_Coll(HSD_GObj* gobj);
void ftSh_SpecialLwRunBrake_Anim(HSD_GObj* gobj);
void ftSh_SpecialLwRunBrake_Phys(HSD_GObj* gobj);
void ftSh_SpecialLwRunBrake_Coll(HSD_GObj* gobj);
void ftSh_SpecialLwStopWall_Anim(HSD_GObj* gobj);
void ftSh_SpecialLwStopWall_Coll(HSD_GObj* gobj);

/* ---- shadow_items.c ---- */
extern const ItemLogicTable itSh_Spring_LogicTable;

/* ---- shadow_kirby.c ---- */
#include "shadow_kirby.h"

#endif
