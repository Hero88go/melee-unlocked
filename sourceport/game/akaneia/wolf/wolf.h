/* Akaneia's Wolf, native.
 *
 * Wolf ships in PlWf.dat as m-ex PowerPC (the "ftFunction" and "itFunction" roots). Everything here
 * is that code rewritten by hand against the decomp. Wolf is built on Fox: his move table has Fox's
 * exact layout (the same 35 action states, animation ids and flags, see ftFox/forward.h), four of its
 * entries still point at Fox's own callbacks, and his special attributes use Fox's layout
 * (ftFox_DatAttrs). Where Wolf differs, the difference is noted at the function.
 *
 * Console offsets in comments are Fighter / Item offsets on the GameCube, for cross-checking against
 * the PowerPC; the code itself only uses the decomp's field names. */
#ifndef MU_AK_WOLF_H
#define MU_AK_WOLF_H

#include <Runtime/platform.h>

#include <melee/ft/forward.h>
#include <melee/it/forward.h>
#include <sysdolphin/baselib/forward.h>

#include <dolphin/mtx.h>
#include <melee/ft/kinds/ftCommon/types.h>
#include <melee/ft/kinds/ftFox/forward.h>
#include <melee/ft/inlines.h>
#include <melee/ft/types.h>
#include <melee/it/kinds/forward.h>
#include <melee/it/kinds/types.h>
#include <melee/it/types.h>

#include "../mu_ak_fighter.h"
#include "mu_disc.h"

/* ---- Action states ---------------------------------------------------------------------------- *
 * Numbered from ftCo_MS_Count (341) in the same order as Fox's (ftFox_MotionState). The names in
 * brackets are the names Akaneia's own build gives the routines. */
typedef enum ftWolf_MotionState {
    ftWf_MS_SpecialNStart = ftCo_MS_Count, /* 341 blaster, ground (the whole move) */
    ftWf_MS_SpecialNLoop,                  /* 342 Fox's callbacks, never entered by Wolf */
    ftWf_MS_SpecialNEnd,                   /* 343 Fox's callbacks, never entered by Wolf */
    ftWf_MS_SpecialAirNStart,              /* 344 blaster, air */
    ftWf_MS_SpecialAirNLoop,               /* 345 Fox's callbacks, never entered by Wolf */
    ftWf_MS_SpecialAirNEnd,                /* 346 Fox's callbacks, never entered by Wolf */
    ftWf_MS_SpecialSStart,                 /* 347 [SpecialS] Wolf Flash wind-up, ground */
    ftWf_MS_SpecialS,                      /* 348 Fox's Illusion callbacks, never entered by Wolf */
    ftWf_MS_SpecialSEnd,                   /* 349 [SpecialSEnd] unused by Wolf's own transitions */
    ftWf_MS_SpecialAirSStart,              /* 350 [SpecialAirS] Wolf Flash wind-up, air */
    ftWf_MS_SpecialAirS,                   /* 351 [SpecialAirSMid] the dash */
    ftWf_MS_SpecialAirSEnd,                /* 352 [SpecialAirSEnd] the slash */
    ftWf_MS_SpecialHiHold,                 /* 353 [SpecialHi] Fire Wolf charge, ground */
    ftWf_MS_SpecialHiHoldAir,              /* 354 [SpecialAirHi] Fire Wolf charge, air */
    ftWf_MS_SpecialHi,                     /* 355 [SpecialHiLaunch] travel along the ground */
    ftWf_MS_SpecialAirHi,                  /* 356 [SpecialAirHiLaunch] travel through the air */
    ftWf_MS_SpecialHiLanding,              /* 357 */
    ftWf_MS_SpecialHiFall,                 /* 358 */
    ftWf_MS_SpecialHiBound,                /* 359 */
    ftWf_MS_SpecialLwStart,                /* 360 [SpecialLw] */
    ftWf_MS_SpecialLwLoop,                 /* 361 */
    ftWf_MS_SpecialLwHit,                  /* 362 [SpecialLwReflect] */
    ftWf_MS_SpecialLwEnd,                  /* 363 */
    ftWf_MS_SpecialLwTurn,                 /* 364 */
    ftWf_MS_SpecialAirLwStart,             /* 365 [SpecialAirLw] */
    ftWf_MS_SpecialAirLwLoop,              /* 366 */
    ftWf_MS_SpecialAirLwHit,               /* 367 [SpecialAirLwReflect] */
    ftWf_MS_SpecialAirLwEnd,               /* 368 */
    ftWf_MS_SpecialAirLwTurn,              /* 369 */
    ftWf_MS_AppealSStartR,                 /* 370..375 Fox's side taunt callbacks */
    ftWf_MS_AppealSStartL,
    ftWf_MS_AppealSR,
    ftWf_MS_AppealSL,
    ftWf_MS_AppealSEndR,
    ftWf_MS_AppealSEndL,
    ftWf_MS_Count,
    ftWf_MS_SelfCount = ftWf_MS_Count - ftCo_MS_Count,
} ftWolf_MotionState;

/* Motion flags Wolf's transitions pass that Fox's code has no name for. */
/* 0x0C4C508B: the ground/air switch of the reflector (Fox uses the same set without the first and
 * fourth bits, ftFx_MF_SpecialLw_Coll). */
#define ftWf_MF_SpecialLw_Coll                                                \
    (ftCommon_GroundAirColl_MF | Ft_MF_KeepFastFall | Ft_MF_KeepGfx |         \
     Ft_MF_SkipHit)

/* Effect ids Wolf spawns. They are m-ex ids (5000 and up) that resolve to Wolf's own effect file;
 * see NOTES.md, the integration layer has to route them. */
enum {
    ftWf_Ef_ReflectorLoop = 5000,
    ftWf_Ef_ReflectorStart = 5001,
    ftWf_Ef_ReflectorHit = 5002,
    ftWf_Ef_FireWolfCharge = 5003,
    ftWf_Ef_FireWolfLaunch = 5004,
    ftWf_Ef_BlasterMuzzle = 5005,
};

/* Wolf's articles (ftData.x48_items), in m-ex's fighter item order. */
enum {
    ftWf_Article_Laser = 0,
    ftWf_Article_Blaster = 1,
};

/* ---- Special attributes ----------------------------------------------------------------------- *
 * Wolf's m-ex onLoad does not use the attribute block of PlWf.dat: it copies a 0xD4-byte block that
 * is compiled into his code ("param_exts") over fp->dat_attrs, and ResetAttributes copies it again.
 * The layout is Fox's (ftFox_DatAttrs). The block in PlWf.dat holds the same values except the last
 * word. Fields marked "unused" are not read by Wolf's code. */
typedef struct ftWolf_DatAttrs {
    /* +00 */ float x0_BLASTER_UNK1;            /* unused (Fox's blaster loop) */
    /* +04 */ float x4_BLASTER_UNK2;            /* unused */
    /* +08 */ float x8_BLASTER_UNK3;            /* unused */
    /* +0C */ float xC_BLASTER_UNK4;            /* unused */
    /* +10 */ float x10_BLASTER_ANGLE;          /* unused */
    /* +14 */ float x14_BLASTER_VEL;            /* unused; the laser's speed is a constant, 2.3 */
    /* +18 */ float x18_BLASTER_LANDING_LAG;    /* unused */
    /* +1C */ s32 x1C_BLASTER_SHOT_ITKIND;      /* unused; m-ex hands out the item kinds */
    /* +20 */ s32 x20_BLASTER_GUN_ITKIND;       /* unused */

    /* +24 */ float x24_FLASH_GRAVITY_DELAY;    /* frames before the wind-up falls */
    /* +28 */ float x28_FLASH_START_VEL_DIV;    /* momentum kept on entry (divided by this) */
    /* +2C */ float x2C_FLASH_START_DECEL_X;
    /* +30 */ float x30_FLASH_START_FALL_ACCEL;
    /* +34 */ float x34_FLASH_UNK1;             /* unused */
    /* +38 */ float x38_FLASH_UNK2;             /* unused */
    /* +3C */ float x3C_FLASH_END_VEL_X;
    /* +40 */ float x40_FLASH_END_DECEL_X;
    /* +44 */ float x44_FLASH_END_GRAVITY_DELAY;
    /* +48 */ float x48_FLASH_END_FALL_ACCEL;
    /* +4C */ float x4C_FLASH_FREEFALL_MOBILITY;
    /* +50 */ float x50_FLASH_LANDING_LAG;

    /* +54 */ float x54_FIREWOLF_GRAVITY_DELAY;
    /* +58 */ float x58_FIREWOLF_START_VEL_DIV;
    /* +5C */ float x5C_FIREWOLF_CHARGE_DECEL_X;
    /* +60 */ float x60_FIREWOLF_FALL_ACCEL;
    /* +64 */ float x64_FIREWOLF_DIRECTION_STICK_MIN; /* unused; Wolf's code tests a literal 0.5 */
    /* +68 */ float x68_FIREWOLF_DURATION;
    /* +6C */ s32 x6C_FIREWOLF_BOUND_FRAMES;
    /* +70 */ float x70_FIREWOLF_DECEL_START;
    /* +74 */ float x74_FIREWOLF_SPEED;
    /* +78 */ float x78_FIREWOLF_DECEL;
    /* +7C */ float x7C_FIREWOLF_LANDING_DECEL;
    /* +80 */ float x80_FIREWOLF_UNK;           /* unused */
    /* +84 */ float x84_FIREWOLF_BOUND_VEL_X;
    /* +88 */ float x88_FIREWOLF_FACING_STICK_MIN;
    /* +8C */ float x8C_FIREWOLF_FREEFALL_MOBILITY;
    /* +90 */ float x90_FIREWOLF_LANDING_LAG;
    /* +94 */ float x94_FIREWOLF_BOUND_ANGLE;

    /* +98 */ float x98_REFLECTOR_RELEASE_LAG;
    /* +9C */ float x9C_REFLECTOR_TURN_FRAMES;
    /* +A0 */ float xA0_REFLECTOR_UNK;          /* unused */
    /* +A4 */ s32 xA4_REFLECTOR_GRAVITY_DELAY;
    /* +A8 */ float xA8_REFLECTOR_AIR_VEL_DIV;
    /* +AC */ float xAC_REFLECTOR_FALL_ACCEL;
    /* +B0 */ ReflectDesc_BE xB0_REFLECTOR_REFLECTION;
} DISC_STRUCT ftWolf_DatAttrs;

_Static_assert(sizeof(ftWolf_DatAttrs) == 0xD4, "Wolf's attribute block is 0xD4 bytes");

static inline ftWolf_DatAttrs* ftWf_Attrs(Fighter* fp)
{
    return fp->dat_attrs;
}

/* ---- Per-state variables (fp->mv, console fp+0x2340) ------------------------------------------ */
typedef struct ftWolf_SpecialNVars {
    /* +0 */ Item_GObj* gun_gobj; /* the blaster Wolf holds for the move */
} ftWolf_SpecialNVars;

typedef struct ftWolf_SpecialSVars {
    /* +0 */ s32 gravity_delay;
    /* +4 */ float x4;      /* cleared on entry, never read */
    /* +8 */ s32 has_hit;   /* set by the slash's hit callback; skips special fall */
} ftWolf_SpecialSVars;

/* Same layout and use as Fox's ftFoxSpecialHi. */
typedef struct ftWolf_SpecialHiVars {
    /* +0 */ s32 gravity_delay;
    /* +4 */ float angle;          /* travel angle, also the model's X rotation */
    /* +8 */ s32 travel_frames;
    /* +C */ s32 launch_frames;    /* frames since the launch, deceleration starts at x70 */
    /* +10 */ s32 ground_frames;   /* frames spent travelling on the ground */
} ftWolf_SpecialHiVars;

/* Same layout and use as Fox's ftFoxSpecialLw. */
typedef struct ftWolf_SpecialLwVars {
    /* +0 */ s32 release_lag;
    /* +4 */ s32 turn_frames;
    /* +8 */ s32 is_release;
    /* +C */ s32 gravity_delay;
} ftWolf_SpecialLwVars;

typedef union ftWolf_MotionVars {
    ftWolf_SpecialNVars SpecialN;
    ftWolf_SpecialSVars SpecialS;
    ftWolf_SpecialHiVars SpecialHi;
    ftWolf_SpecialLwVars SpecialLw;
} ftWolf_MotionVars;

_Static_assert(sizeof(ftWolf_MotionVars) <= sizeof(((Fighter*) 0)->mv),
               "Wolf's state variables fit in fp->mv");

static inline ftWolf_MotionVars* ftWf_MV(Fighter* fp)
{
    return (ftWolf_MotionVars*) &fp->mv;
}

/* ---- The laser's item variables (ip->xDD4_itemVar, console ip+0xDD4) -------------------------- */
typedef struct itWolfLaser_ItemVars {
    /* +DD4 */ float xDD4;     /* cleared at spawn, never read */
    /* +DD8 */ float angle;    /* direction of travel, radians; the reflect callback turns it */
    /* +DDC */ float speed;
    /* +DE0 */ Vec3 prev_pos;  /* last frame's position, the start of the wall test */
} itWolfLaser_ItemVars;

static inline itWolfLaser_ItemVars* itWfLaser_Vars(Item* ip)
{
    return (itWolfLaser_ItemVars*) &ip->xDD4_itemVar;
}

/* ---- Item services ---------------------------------------------------------------------------- *
 * Wolf's PowerPC calls three m-ex services for his articles. Two are the integration layer's
 * (mu_ak_fighter.h): m-ex MEX_IndexFighterItem (0x803D7058) is mu_ak_article_set with
 * mu_ak_item_kind, and MEX_GetFtItemID (0x803D7088) is mu_ak_item_kind. The third is the retail
 * spawner itself: m-ex code calls Item_8026862C, which is static in it/item.c, with its own hold
 * kind. ftWf_CreateItem (wolf_specialn.c) wraps it; see NOTES.md for the hook it wants. */
Item_GObj* ftWf_CreateItem(SpawnItem* spawn);

/* ---- wolf.c ----------------------------------------------------------------------------------- */
extern const ftWolf_DatAttrs ftWf_Init_Attrs;
extern const MotionState ftWf_Init_MotionStateTable[ftWf_MS_SelfCount];

/* The accessory callback every Wolf effect uses (accessory4_cb, console fp+0x21BC): spawn the effect
 * once per state (x2219_b0 guards it), let it pause with hitlag, then remove itself. Fox's effect
 * callbacks are the same shape. */
static inline void ftWf_EffectDone(Fighter* fp)
{
    Fighter_SetEffectHitlagCallbacks(fp);
    fp->accessory4_cb = NULL;
}

/* ---- wolf_specialn.c -------------------------------------------------------------------------- */
void ftWf_SpecialN_Enter(HSD_GObj* gobj);
void ftWf_SpecialAirN_Enter(HSD_GObj* gobj);
void ftWf_SpecialNStart_Anim(HSD_GObj* gobj);
void ftWf_SpecialNStart_IASA(HSD_GObj* gobj);
void ftWf_SpecialNStart_Phys(HSD_GObj* gobj);
void ftWf_SpecialNStart_Coll(HSD_GObj* gobj);
void ftWf_SpecialAirNStart_Anim(HSD_GObj* gobj);
void ftWf_SpecialAirNStart_IASA(HSD_GObj* gobj);
void ftWf_SpecialAirNStart_Phys(HSD_GObj* gobj);
void ftWf_SpecialAirNStart_Coll(HSD_GObj* gobj);
void ftWf_SpecialN_DestroyGun(HSD_GObj* gobj);

/* ---- wolf_specials.c -------------------------------------------------------------------------- */
void ftWf_SpecialS_Enter(HSD_GObj* gobj);
void ftWf_SpecialAirS_Enter(HSD_GObj* gobj);
void ftWf_SpecialSStart_Anim(HSD_GObj* gobj);
void ftWf_SpecialSStart_IASA(HSD_GObj* gobj);
void ftWf_SpecialSStart_Phys(HSD_GObj* gobj);
void ftWf_SpecialSStart_Coll(HSD_GObj* gobj);
void ftWf_SpecialSEnd_Anim(HSD_GObj* gobj);
void ftWf_SpecialSEnd_IASA(HSD_GObj* gobj);
void ftWf_SpecialSEnd_Phys(HSD_GObj* gobj);
void ftWf_SpecialSEnd_Coll(HSD_GObj* gobj);
void ftWf_SpecialAirSStart_Anim(HSD_GObj* gobj);
void ftWf_SpecialAirSStart_IASA(HSD_GObj* gobj);
void ftWf_SpecialAirSStart_Phys(HSD_GObj* gobj);
void ftWf_SpecialAirSStart_Coll(HSD_GObj* gobj);
void ftWf_SpecialAirS_Anim(HSD_GObj* gobj);
void ftWf_SpecialAirS_IASA(HSD_GObj* gobj);
void ftWf_SpecialAirS_Phys(HSD_GObj* gobj);
void ftWf_SpecialAirS_Coll(HSD_GObj* gobj);
void ftWf_SpecialAirSEnd_Anim(HSD_GObj* gobj);
void ftWf_SpecialAirSEnd_IASA(HSD_GObj* gobj);
void ftWf_SpecialAirSEnd_Phys(HSD_GObj* gobj);
void ftWf_SpecialAirSEnd_Coll(HSD_GObj* gobj);

/* ---- wolf_specialhi.c ------------------------------------------------------------------------- */
void ftWf_SpecialHi_Enter(HSD_GObj* gobj);
void ftWf_SpecialAirHi_Enter(HSD_GObj* gobj);
void ftWf_SpecialHiHold_Anim(HSD_GObj* gobj);
void ftWf_SpecialHiHold_IASA(HSD_GObj* gobj);
void ftWf_SpecialHiHold_Phys(HSD_GObj* gobj);
void ftWf_SpecialHiHold_Coll(HSD_GObj* gobj);
void ftWf_SpecialHiHoldAir_Anim(HSD_GObj* gobj);
void ftWf_SpecialHiHoldAir_IASA(HSD_GObj* gobj);
void ftWf_SpecialHiHoldAir_Phys(HSD_GObj* gobj);
void ftWf_SpecialHiHoldAir_Coll(HSD_GObj* gobj);
void ftWf_SpecialHi_Anim(HSD_GObj* gobj);
void ftWf_SpecialHi_IASA(HSD_GObj* gobj);
void ftWf_SpecialHi_Phys(HSD_GObj* gobj);
void ftWf_SpecialHi_Coll(HSD_GObj* gobj);
void ftWf_SpecialAirHi_Anim(HSD_GObj* gobj);
void ftWf_SpecialAirHi_IASA(HSD_GObj* gobj);
void ftWf_SpecialAirHi_Phys(HSD_GObj* gobj);
void ftWf_SpecialAirHi_Coll(HSD_GObj* gobj);
void ftWf_SpecialHiLanding_Anim(HSD_GObj* gobj);
void ftWf_SpecialHiLanding_IASA(HSD_GObj* gobj);
void ftWf_SpecialHiLanding_Phys(HSD_GObj* gobj);
void ftWf_SpecialHiLanding_Coll(HSD_GObj* gobj);
void ftWf_SpecialHiFall_Anim(HSD_GObj* gobj);
void ftWf_SpecialHiFall_IASA(HSD_GObj* gobj);
void ftWf_SpecialHiFall_Phys(HSD_GObj* gobj);
void ftWf_SpecialHiFall_Coll(HSD_GObj* gobj);
void ftWf_SpecialHiBound_Anim(HSD_GObj* gobj);
void ftWf_SpecialHiBound_IASA(HSD_GObj* gobj);
void ftWf_SpecialHiBound_Phys(HSD_GObj* gobj);
void ftWf_SpecialHiBound_Coll(HSD_GObj* gobj);

/* ---- wolf_speciallw.c ------------------------------------------------------------------------- */
void ftWf_SpecialLw_Enter(HSD_GObj* gobj);
void ftWf_SpecialAirLw_Enter(HSD_GObj* gobj);
void ftWf_SpecialLwStart_Anim(HSD_GObj* gobj);
void ftWf_SpecialLwStart_IASA(HSD_GObj* gobj);
void ftWf_SpecialLwStart_Phys(HSD_GObj* gobj);
void ftWf_SpecialLwStart_Coll(HSD_GObj* gobj);
void ftWf_SpecialLwLoop_Anim(HSD_GObj* gobj);
void ftWf_SpecialLwLoop_IASA(HSD_GObj* gobj);
void ftWf_SpecialLwLoop_Phys(HSD_GObj* gobj);
void ftWf_SpecialLwLoop_Coll(HSD_GObj* gobj);
void ftWf_SpecialLwHit_Anim(HSD_GObj* gobj);
void ftWf_SpecialLwHit_IASA(HSD_GObj* gobj);
void ftWf_SpecialLwHit_Phys(HSD_GObj* gobj);
void ftWf_SpecialLwHit_Coll(HSD_GObj* gobj);
void ftWf_SpecialLwEnd_Anim(HSD_GObj* gobj);
void ftWf_SpecialLwEnd_IASA(HSD_GObj* gobj);
void ftWf_SpecialLwEnd_Phys(HSD_GObj* gobj);
void ftWf_SpecialLwEnd_Coll(HSD_GObj* gobj);
void ftWf_SpecialLwTurn_Anim(HSD_GObj* gobj);
void ftWf_SpecialLwTurn_IASA(HSD_GObj* gobj);
void ftWf_SpecialLwTurn_Phys(HSD_GObj* gobj);
void ftWf_SpecialLwTurn_Coll(HSD_GObj* gobj);
void ftWf_SpecialAirLwStart_Anim(HSD_GObj* gobj);
void ftWf_SpecialAirLwStart_IASA(HSD_GObj* gobj);
void ftWf_SpecialAirLwStart_Phys(HSD_GObj* gobj);
void ftWf_SpecialAirLwStart_Coll(HSD_GObj* gobj);
void ftWf_SpecialAirLwLoop_Anim(HSD_GObj* gobj);
void ftWf_SpecialAirLwLoop_IASA(HSD_GObj* gobj);
void ftWf_SpecialAirLwLoop_Phys(HSD_GObj* gobj);
void ftWf_SpecialAirLwLoop_Coll(HSD_GObj* gobj);
void ftWf_SpecialAirLwHit_Anim(HSD_GObj* gobj);
void ftWf_SpecialAirLwHit_IASA(HSD_GObj* gobj);
void ftWf_SpecialAirLwHit_Phys(HSD_GObj* gobj);
void ftWf_SpecialAirLwHit_Coll(HSD_GObj* gobj);
void ftWf_SpecialAirLwEnd_Anim(HSD_GObj* gobj);
void ftWf_SpecialAirLwEnd_IASA(HSD_GObj* gobj);
void ftWf_SpecialAirLwEnd_Phys(HSD_GObj* gobj);
void ftWf_SpecialAirLwEnd_Coll(HSD_GObj* gobj);
void ftWf_SpecialAirLwTurn_Anim(HSD_GObj* gobj);
void ftWf_SpecialAirLwTurn_IASA(HSD_GObj* gobj);
void ftWf_SpecialAirLwTurn_Phys(HSD_GObj* gobj);
void ftWf_SpecialAirLwTurn_Coll(HSD_GObj* gobj);

/* ---- wolf_items.c: the laser (article 0) and the blaster Wolf holds (article 1) --------------- *
 * One m-ex itFunction export table per article, rewritten, in article order (MuAkFighter.articles). */
extern const ItemLogicTable itWf_Articles[2];

#endif
