/* Akaneia's Diddy Kong, native.
 *
 * Hand-written C for the behavior Akaneia ships as PowerPC in PlDd.dat (the m-ex "ftFunction" and
 * "itFunction" roots). Names follow the debug symbols PlDd.dat carries for its own routines, with
 * the decomp's ftXx_ prefix convention (ftDd_). Console offsets are given in comments wherever a
 * value lives in a slot the decomp has no name for.
 *
 * Layout on the console:
 *   fp->u  (fp+222C) fighter vars : ftDd_FighterVars (overlaid, see ftDd_FV)
 *   fp->mv (fp+2340) motion vars  : ftDd_MotionVars  (overlaid, see ftDd_MV)
 *   fp->dat_attrs                  : ftDd_DatAttrs    (disc data, big-endian, 0x100 bytes)
 *   ip->xDD4_itemVar               : ftDd_BananaVars  (banana article only)
 */
#ifndef MU_AK_DIDDY_FTDIDDY_H
#define MU_AK_DIDDY_FTDIDDY_H

#include <Runtime/platform.h>

#include <melee/ft/forward.h>
#include <melee/ft/kinds/ftCommon/forward.h>
#include <melee/it/forward.h>
#include <sysdolphin/baselib/forward.h>

#include <melee/ft/types.h>
#include <melee/ft/inlines.h>
#include <melee/it/inlines.h>
#include <melee/it/types.h>
#include <melee/it/kinds/types.h>

#include <mu_disc.h>

#include "../common/mu_ak_kirby.h"

/* ------------------------------------------------------------------------------------------ */
/* Motion states (m-ex move_logic, numbered from ftCo_MS_Count = 341)                          */
/* ------------------------------------------------------------------------------------------ */

typedef enum ftDiddy_MotionState {
    ftDd_MS_SpecialNStart = ftCo_MS_Count, /* 341 */
    ftDd_MS_SpecialNCharge,                /* 342 */
    ftDd_MS_SpecialNDanger,                /* 343 popgun overheating */
    ftDd_MS_SpecialNBlow,                  /* 344 popgun explodes */
    ftDd_MS_SpecialNShoot,                 /* 345 */
    ftDd_MS_SpecialAirNStart,              /* 346 */
    ftDd_MS_SpecialAirNCharge,             /* 347 */
    ftDd_MS_SpecialAirNDanger,             /* 348 */
    ftDd_MS_SpecialAirNBlow,               /* 349 */
    ftDd_MS_SpecialAirNShoot,              /* 350 */
    ftDd_MS_SpecialSStart,                 /* 351 Monkey Flip, ground start */
    ftDd_MS_SpecialSStick,                 /* 352 latched onto a victim */
    ftDd_MS_SpecialSStickAttack,           /* 353 */
    ftDd_MS_SpecialSStickAttack2,          /* 354 */
    ftDd_MS_SpecialSStickJump,             /* 355 */
    ftDd_MS_SpecialSStickJump2,            /* 356 */
    ftDd_MS_SpecialAirSStart,              /* 357 */
    ftDd_MS_SpecialAirSJump,               /* 358 the flip itself (ground and air) */
    ftDd_MS_SpecialAirSKick,               /* 359 */
    ftDd_MS_SpecialHiStart,                /* 360 Rocketbarrel Boost */
    ftDd_MS_SpecialHiCharge,               /* 361 */
    ftDd_MS_SpecialAirHiStart,             /* 362 */
    ftDd_MS_SpecialAirHiCharge,            /* 363 */
    ftDd_MS_SpecialAirHiJump,              /* 364 flying */
    ftDd_MS_SpecialAirHiDamage,            /* 365 hit a wall or ceiling */
    ftDd_MS_SpecialAirHiDamage2,           /* 366 same callbacks, second animation, never entered */
    ftDd_MS_SpecialLw,                     /* 367 Banana Peel */
    ftDd_MS_SpecialAirLw,                  /* 368 */
    /* The "Taro" states are entered by the VICTIM of Monkey Flip (see ftDd_SpecialS_TaroChange). */
    ftDd_MS_SpecialSStickWaitTaro,         /* 369 */
    ftDd_MS_SpecialAirSStickWaitTaro,      /* 370 */
    ftDd_MS_SpecialSStickJumpTaro,         /* 371 */
    ftDd_MS_SpecialAirSStickJumpTaro,      /* 372 */
    ftDd_MS_SpecialSStickAttackTaro,       /* 373 */
    ftDd_MS_SpecialAirSStickAttackTaro,    /* 374 */
    ftDd_MS_End
} ftDiddy_MotionState;

#define ftDd_MS_SelfCount (ftDd_MS_End - ftCo_MS_Count)

/* Animation (submotion) ids of the move_logic table, from PlDd.dat. */
enum {
    ftDd_SM_SpecialNStart = 0x127,
    ftDd_SM_SpecialNCharge = 0x128,
    ftDd_SM_SpecialNDanger = 0x129,
    ftDd_SM_SpecialNBlow = 0x12A,
    ftDd_SM_SpecialNShoot = 0x12B,
    ftDd_SM_SpecialAirNStart = 0x12C,
    ftDd_SM_SpecialAirNCharge = 0x12D,
    ftDd_SM_SpecialAirNDanger = 0x12E,
    ftDd_SM_SpecialAirNBlow = 0x12F,
    ftDd_SM_SpecialAirNShoot = 0x130,
    ftDd_SM_SpecialSStart = 0x131,
    ftDd_SM_SpecialSStick = 0x134,
    ftDd_SM_SpecialSStickAttack = 0x135,
    ftDd_SM_SpecialSStickAttack2 = 0x136,
    ftDd_SM_SpecialSStickJump = 0x137,
    ftDd_SM_SpecialSStickJump2 = 0x138,
    ftDd_SM_SpecialAirSStart = 0x139,
    ftDd_SM_SpecialAirSJump = 0x13A,
    ftDd_SM_SpecialAirSKick = 0x13B,
    ftDd_SM_SpecialHiStart = 0x13D,
    ftDd_SM_SpecialHiCharge = 0x13E,
    ftDd_SM_SpecialHiChargeF = 0x13F, /* blend targets of the charge tilt */
    ftDd_SM_SpecialHiChargeB = 0x140,
    ftDd_SM_SpecialAirHiStart = 0x141,
    ftDd_SM_SpecialAirHiCharge = 0x142,
    ftDd_SM_SpecialAirHiChargeF = 0x143,
    ftDd_SM_SpecialAirHiChargeB = 0x144,
    ftDd_SM_SpecialAirHiJump = 0x145,
    ftDd_SM_SpecialAirHiDamage = 0x146,
    ftDd_SM_SpecialAirHiDamage2 = 0x147,
    ftDd_SM_SpecialLw = 0x148,
    ftDd_SM_SpecialAirLw = 0x149,
    ftDd_SM_SpecialSStickWaitTaro = 0x14A,
    ftDd_SM_SpecialSStickAttackTaro = 0x14B,
    ftDd_SM_SpecialSStickJumpTaro = 0x14C,
};

/* x4 flags every move_logic entry carries (console 0x00340111). */
static MotionFlags const ftDd_MF_Special =
    ftCo_MF_Special | Ft_MF_KeepFastFall | Ft_MF_SkipThrowException;

/* Flags of the ground/air swap transitions (console 0x4202 and 0x4002). */
static MotionFlags const ftDd_MF_SwapN =
    Ft_MF_KeepGfx | Ft_MF_KeepSfx | Ft_MF_UpdateCmd;
static MotionFlags const ftDd_MF_Swap = Ft_MF_KeepGfx | Ft_MF_UpdateCmd;

/* Articles (the ftData x48 item list, m-ex fighter item indices). */
enum {
    ftDd_Article_Popgun = 0,
    ftDd_Article_Peanut = 1,
    ftDd_Article_Banana = 2,
};

/* Bones Diddy's code asks ftParts_GetBoneIndex for. */
enum {
    ftDd_Part_GunHand = 0x1F,  /* right hand: the popgun */
    ftDd_Part_GunHand2 = 0x31, /* left hand: the second victory-pose popgun */
};

/* ------------------------------------------------------------------------------------------ */
/* Attributes: the 0x100-byte ext_attr block of PlDd.dat (disc data)                          */
/* ------------------------------------------------------------------------------------------ */

typedef struct ftDd_AnimRate {
    s32 motion_id; /* -1 ends the table */
    float frame;   /* from this frame on ... */
    float rate;    /* ... play at this rate */
} DISC_STRUCT ftDd_AnimRate;

typedef struct ftDd_DatAttrs {
    /* +00 */ float specialn_angle_charge; /* fraction of the full charge that reaches min angle */
    /* +04 */ float specialn_speed_min;
    /* +08 */ float specialn_speed_max;
    /* +0C */ float specialn_angle_min_charge; /* radians, uncharged */
    /* +10 */ float specialn_angle_max_charge; /* radians, charged */
    /* +14 */ float specialn_dmg_min;
    /* +18 */ float specialn_dmg_max;
    /* +1C */ float specialn_bkb_min;
    /* +20 */ float specialn_bkb_max;
    /* +24 */ float specialn_kbg_min;
    /* +28 */ float specialn_kbg_max;
    /* +2C */ float specialn_recoil_min;
    /* +30 */ float specialn_recoil_max;
    /* +34 */ Vec2_BE specials_jump2_vel;
    /* +3C */ Vec2_BE specials_attack2_vel;
    /* +44 */ float specials_landing_lag;
    /* +48 */ float specials_grab_timer_max;
    /* +4C */ float specials_taro_gravity; /* victim fall while held in the air */
    /* +50 */ float specials_taro_terminal_vel;
    /* +54 */ Vec2_BE specials_flip_vel;
    /* +5C */ float speciallw_throw_speed;
    /* +60 */ float speciallw_throw_angle; /* radians */
    /* +64 */ float specialhi_turn_stick;
    /* +68 */ float specialhi_charge_min;
    /* +6C */ float specialhi_charge_max;
    /* +70 */ float specialhi_air_start_gravity;
    /* +74 */ float specialhi_air_start_terminal_vel;
    /* +78 */ float specialhi_tilt_step;
    /* +7C */ float specialhi_tilt_max;
    /* +80 */ float x80; /* not read by the code */
    /* +84 */ float specialhi_drift_accel;
    /* +88 */ float specialhi_drift_max;
    /* +8C */ float specialhi_drift_friction;
    /* +90 */ float specialhi_speed_min;
    /* +94 */ float specialhi_speed_max;
    /* +98 */ float specialhi_launch_dmg_min;
    /* +9C */ float specialhi_launch_dmg_max;
    /* +A0 */ float specialhi_launch_bkb_min;
    /* +A4 */ float specialhi_launch_bkb_max;
    /* +A8 */ float specialhi_launch_kbg_min;
    /* +AC */ float specialhi_launch_kbg_max;
    /* +B0 */ float specialhi_fly_dmg_min;
    /* +B4 */ float specialhi_fly_dmg_max;
    /* +B8 */ float specialhi_fly_bkb_min;
    /* +BC */ float specialhi_fly_bkb_max;
    /* +C0 */ float specialhi_angle_min; /* degrees above horizontal at full tilt */
    /* +C4 */ s32 specialhi_gfx_interval;
    /* +C8 */ float specialhi_turn_max; /* radians per frame */
    /* +CC */ float specialhi_level_angle;
    /* +D0 */ float specialhi_fall_mobility;
    /* +D4 */ float specialhi_fall_landing_lag;
    /* +D8 */ float xD8; /* not read by the code */
    /* +DC */ s32 specialhi_gfx_bone;
    /* +E0 */ s32 specialhi_gfx_id;
    /* +E4 */ Vec3_BE specialhi_gfx_offset0;
    /* +F0 */ Vec3_BE specialhi_gfx_offset1;
    /* +FC */ DISC_PTR(ftDd_AnimRate) anim_rates;
} DISC_STRUCT ftDd_DatAttrs;

#define ftDd_DatAttrs_Size 0x100

/* The thirteen values of a popgun shot, in host order: the first thirteen fields of the attribute
 * block, under the same names. Diddy fills one from his attributes; the ability Kirby copies has
 * its own, compiled into its code (diddy_kirby.c). */
typedef struct ftDd_PeanutParams {
    float specialn_angle_charge;
    float specialn_speed_min;
    float specialn_speed_max;
    float specialn_angle_min_charge;
    float specialn_angle_max_charge;
    float specialn_dmg_min;
    float specialn_dmg_max;
    float specialn_bkb_min;
    float specialn_bkb_max;
    float specialn_kbg_min;
    float specialn_kbg_max;
    float specialn_recoil_min;
    float specialn_recoil_max;
} ftDd_PeanutParams;

/* ------------------------------------------------------------------------------------------ */
/* Fighter and motion vars                                                                    */
/* ------------------------------------------------------------------------------------------ */

/* fp+222C. These are Diddy's own, so native layout (the decomp's union is not widened; the
 * integration layer only has to keep the union at least this big, see ftDd_FV). */
typedef struct ftDd_FighterVars {
    /* 222C */ float peanut_charge_frames; /* SpecialNCharge + SpecialNDanger lengths */
    /* 2230 */ Item_GObj* banana;          /* the peel on stage, one at a time */
    /* 2234 */ float hi_charge_frames;     /* SpecialHiCharge length */
    /* 2238 */ float hi_jump_frames;       /* SpecialAirHiJump length */
} ftDd_FighterVars;

/* fp+2340, per move. The Taro and Trip members live in the VICTIM's motion vars. */
typedef union ftDd_MotionVars {
    struct {
        /* 2340 */ Item_GObj* gun;
        /* 2344 */ int charge;
    } specialn;
    struct {
        /* 2340 */ Item_GObj* gun_left;  /* ftDd_Part_GunHand2 */
        /* 2344 */ Item_GObj* gun_right; /* ftDd_Part_GunHand */
    } win;
    struct {
        /* 2340 */ int jumps_used; /* x1968 when the move started */
    } specials;
    struct {
        /* 2340 */ int mash_lock;
    } taro;
    struct {
        /* 2340 */ float blend;       /* weight of the tilted charge animation */
        /* 2344 */ float tilt;        /* smoothed stick x */
        /* 2348 */ int charge;
        /* 234C */ s16 gfx_timer;
        /* 234E */ s16 fly_hit_off;
        /* 2350 */ int leveling;      /* set by the script (cmd_vars[2]) near the end */
        /* 2354 */ float level_frame;
        /* 2358 */ float level_frames;
        /* 235C */ float level_start_rot;
    } specialhi;
    struct {
        /* 2340 */ Item_GObj* banana;
    } speciallw;
    struct {
        /* 2340 */ int frame;
    } trip;
} ftDd_MotionVars;

#define ftDd_FV(fp) ((ftDd_FighterVars*) &(fp)->u)
#define ftDd_MV(fp) ((ftDd_MotionVars*) &(fp)->mv)
#define ftDd_Attrs(fp) ((ftDd_DatAttrs*) (fp)->dat_attrs)

_Static_assert(sizeof(ftDd_FighterVars) <= sizeof(union Fighter_FighterVars),
               "Diddy fighter vars must fit the decomp's union");
_Static_assert(sizeof(ftDd_MotionVars) <= sizeof(union Fighter_MotionVars),
               "Diddy motion vars must fit the decomp's union");

/* Banana item vars (ip+DD4). */
typedef struct ftDd_BananaVars {
    /* DD4 */ HSD_GObj* thrower; /* the Diddy that pulled it, never changes */
    /* DD8 */ int destroy;       /* returned by the collision callbacks */
    /* DDC */ int flew_up;       /* tripped someone: vanish on the next landing */
    /* DE0 */ int bounces;
} ftDd_BananaVars;

#define ftDd_BananaVarsOf(ip) ((ftDd_BananaVars*) &(ip)->xDD4_itemVar)

/* ------------------------------------------------------------------------------------------ */
/* Hooks the integration layer provides (m-ex runtime services the console code calls)        */
/* ------------------------------------------------------------------------------------------ */

/* MEX_IndexFighterItem (console 803D7058): register article @p index of fighter @p kind. */
void mu_ak_register_article(FighterKind kind, void* article, int index);
/* MEX_GetFtItemID (console 803D7088): the ItemKind m-ex gave article @p index of this fighter. */
ItemKind mu_ak_article_kind(HSD_GObj* fighter_gobj, int index);
/* The item creator without Item_802674AC's hold-kind rewrite (the decomp's static
 * Item_8026862C), able to create m-ex article kinds. */
Item_GObj* mu_ak_item_create(SpawnItem* spawn);

/* ------------------------------------------------------------------------------------------ */
/* ftdiddy.c                                                                                  */
/* ------------------------------------------------------------------------------------------ */

extern MotionState ftDd_MotionStateTable[ftDd_MS_SelfCount];
/* Diddy's three articles, then the two of the ability Kirby copies from him (articles 3 and 4,
 * item kinds 260 and 261 on Akaneia): the popgun and the peanut again, with their data in the hat
 * file (itdiddy.c). */
extern ItemLogicTable ftDd_ItemLogic[5];

void ftDd_Init_OnLoad(HSD_GObj* gobj);
void ftDd_Init_OnDeath(HSD_GObj* gobj);
void ftDd_Init_OnDestroy(HSD_GObj* gobj);
void ftDd_Init_OnItemPickup(HSD_GObj* gobj, bool catch_item);
void ftDd_Init_OnItemInvisible(HSD_GObj* gobj);
void ftDd_Init_OnItemVisible(HSD_GObj* gobj);
void ftDd_Init_OnItemDrop(HSD_GObj* gobj, bool drop_item);
void ftDd_Init_OnItemCatch(HSD_GObj* gobj, bool catch_item);
void ftDd_Init_OnItemUnk(HSD_GObj* gobj, bool drop_item);
void ftDd_Init_OnKnockbackEnter(HSD_GObj* gobj);
void ftDd_Init_OnKnockbackExit(HSD_GObj* gobj);
void ftDd_Init_OnFrame(HSD_GObj* gobj);
void ftDd_Init_OnRespawn(HSD_GObj* gobj);
void ftDd_Init_EnterDoubleJump(HSD_GObj* gobj);

/* Victory pose */
Item_GObj* ftDd_Win_SpawnGun(HSD_GObj* gobj, Fighter_Part bone);
void ftDd_Win_Shoot(HSD_GObj* gobj, Item_GObj* gun);

/* Spawning an article the way Diddy's code does. */
Item_GObj* ftDd_SpawnArticle(HSD_GObj* gobj, int article, int hold_kind, Vec3* pos,
                             GroundOrAir ga);

/* ------------------------------------------------------------------------------------------ */
/* Special N: Peanut Popgun (ftdiddyspecialn.c)                                               */
/* ------------------------------------------------------------------------------------------ */

void ftDd_SpecialN_Enter(HSD_GObj* gobj);
void ftDd_SpecialAirN_Enter(HSD_GObj* gobj);
void ftDd_SpecialN_GunSpawn(HSD_GObj* gobj);
void ftDd_SpecialN_GunSpawnArticle(HSD_GObj* gobj, int article);
void ftDd_SpecialN_GunDestroy(HSD_GObj* gobj);
void ftDd_SpecialN_GunChangeModel(Item_GObj* gun, int model);
void ftDd_SpecialN_GunShoot(HSD_GObj* gobj);
void ftDd_SpecialN_GunShootWith(HSD_GObj* gobj, int article, const ftDd_PeanutParams* da,
                                float charge_frames);

void ftDd_SpecialNStart_Anim(HSD_GObj* gobj);
void ftDd_SpecialNStart_IASA(HSD_GObj* gobj);
void ftDd_SpecialNStart_Phys(HSD_GObj* gobj);
void ftDd_SpecialNStart_Coll(HSD_GObj* gobj);
void ftDd_SpecialNCharge_Anim(HSD_GObj* gobj);
void ftDd_SpecialNCharge_IASA(HSD_GObj* gobj);
void ftDd_SpecialNCharge_Phys(HSD_GObj* gobj);
void ftDd_SpecialNCharge_Coll(HSD_GObj* gobj);
void ftDd_SpecialNDanger_Anim(HSD_GObj* gobj);
void ftDd_SpecialNDanger_IASA(HSD_GObj* gobj);
void ftDd_SpecialNDanger_Phys(HSD_GObj* gobj);
void ftDd_SpecialNDanger_Coll(HSD_GObj* gobj);
void ftDd_SpecialNBlow_Anim(HSD_GObj* gobj);
void ftDd_SpecialNBlow_IASA(HSD_GObj* gobj);
void ftDd_SpecialNBlow_Phys(HSD_GObj* gobj);
void ftDd_SpecialNBlow_Coll(HSD_GObj* gobj);
void ftDd_SpecialNShoot_Anim(HSD_GObj* gobj);
void ftDd_SpecialNShoot_IASA(HSD_GObj* gobj);
void ftDd_SpecialNShoot_Phys(HSD_GObj* gobj);
void ftDd_SpecialNShoot_Coll(HSD_GObj* gobj);
void ftDd_SpecialAirNStart_Anim(HSD_GObj* gobj);
void ftDd_SpecialAirNStart_IASA(HSD_GObj* gobj);
void ftDd_SpecialAirNStart_Phys(HSD_GObj* gobj);
void ftDd_SpecialAirNStart_Coll(HSD_GObj* gobj);
void ftDd_SpecialAirNCharge_Anim(HSD_GObj* gobj);
void ftDd_SpecialAirNCharge_IASA(HSD_GObj* gobj);
void ftDd_SpecialAirNCharge_Phys(HSD_GObj* gobj);
void ftDd_SpecialAirNCharge_Coll(HSD_GObj* gobj);
void ftDd_SpecialAirNDanger_Anim(HSD_GObj* gobj);
void ftDd_SpecialAirNDanger_IASA(HSD_GObj* gobj);
void ftDd_SpecialAirNDanger_Phys(HSD_GObj* gobj);
void ftDd_SpecialAirNDanger_Coll(HSD_GObj* gobj);
void ftDd_SpecialAirNBlow_Anim(HSD_GObj* gobj);
void ftDd_SpecialAirNBlow_IASA(HSD_GObj* gobj);
void ftDd_SpecialAirNBlow_Phys(HSD_GObj* gobj);
void ftDd_SpecialAirNBlow_Coll(HSD_GObj* gobj);
void ftDd_SpecialAirNShoot_Anim(HSD_GObj* gobj);
void ftDd_SpecialAirNShoot_IASA(HSD_GObj* gobj);
void ftDd_SpecialAirNShoot_Phys(HSD_GObj* gobj);
void ftDd_SpecialAirNShoot_Coll(HSD_GObj* gobj);

/* ------------------------------------------------------------------------------------------ */
/* Special S: Monkey Flip (ftdiddyspecials.c)                                                 */
/* ------------------------------------------------------------------------------------------ */

void ftDd_SpecialS_Enter(HSD_GObj* gobj);
void ftDd_SpecialAirS_Enter(HSD_GObj* gobj);

void ftDd_SpecialSStart_Anim(HSD_GObj* gobj);
void ftDd_SpecialSStart_IASA(HSD_GObj* gobj);
void ftDd_SpecialSStart_Phys(HSD_GObj* gobj);
void ftDd_SpecialSStart_Coll(HSD_GObj* gobj);
void ftDd_SpecialSStick_Anim(HSD_GObj* gobj);
void ftDd_SpecialSStick_IASA(HSD_GObj* gobj);
void ftDd_SpecialSStick_Phys(HSD_GObj* gobj);
void ftDd_SpecialSStick_Coll(HSD_GObj* gobj);
void ftDd_SpecialSStickAttack_Anim(HSD_GObj* gobj);
void ftDd_SpecialSStickAttack_IASA(HSD_GObj* gobj);
void ftDd_SpecialSStickAttack_Phys(HSD_GObj* gobj);
void ftDd_SpecialSStickAttack_Coll(HSD_GObj* gobj);
void ftDd_SpecialSStickAttack2_Anim(HSD_GObj* gobj);
void ftDd_SpecialSStickAttack2_IASA(HSD_GObj* gobj);
void ftDd_SpecialSStickAttack2_Phys(HSD_GObj* gobj);
void ftDd_SpecialSStickAttack2_Coll(HSD_GObj* gobj);
void ftDd_SpecialSStickJump_Anim(HSD_GObj* gobj);
void ftDd_SpecialSStickJump_IASA(HSD_GObj* gobj);
void ftDd_SpecialSStickJump_Phys(HSD_GObj* gobj);
void ftDd_SpecialSStickJump_Coll(HSD_GObj* gobj);
void ftDd_SpecialSStickJump2_Anim(HSD_GObj* gobj);
void ftDd_SpecialSStickJump2_IASA(HSD_GObj* gobj);
void ftDd_SpecialSStickJump2_Phys(HSD_GObj* gobj);
void ftDd_SpecialSStickJump2_Coll(HSD_GObj* gobj);
void ftDd_SpecialAirSStart_Anim(HSD_GObj* gobj);
void ftDd_SpecialAirSStart_IASA(HSD_GObj* gobj);
void ftDd_SpecialAirSStart_Phys(HSD_GObj* gobj);
void ftDd_SpecialAirSStart_Coll(HSD_GObj* gobj);
void ftDd_SpecialAirSJump_Anim(HSD_GObj* gobj);
void ftDd_SpecialAirSJump_IASA(HSD_GObj* gobj);
void ftDd_SpecialAirSJump_Phys(HSD_GObj* gobj);
void ftDd_SpecialAirSJump_Coll(HSD_GObj* gobj);
void ftDd_SpecialAirSKick_Anim(HSD_GObj* gobj);
void ftDd_SpecialAirSKick_IASA(HSD_GObj* gobj);
void ftDd_SpecialAirSKick_Phys(HSD_GObj* gobj);
void ftDd_SpecialAirSKick_Coll(HSD_GObj* gobj);

void ftDd_SpecialSStickWaitTaro_Anim(HSD_GObj* gobj);
void ftDd_SpecialSStickWaitTaro_IASA(HSD_GObj* gobj);
void ftDd_SpecialSStickWaitTaro_Phys(HSD_GObj* gobj);
void ftDd_SpecialSStickWaitTaro_Coll(HSD_GObj* gobj);
void ftDd_SpecialAirSStickWaitTaro_Anim(HSD_GObj* gobj);
void ftDd_SpecialAirSStickWaitTaro_IASA(HSD_GObj* gobj);
void ftDd_SpecialAirSStickWaitTaro_Phys(HSD_GObj* gobj);
void ftDd_SpecialAirSStickWaitTaro_Coll(HSD_GObj* gobj);
void ftDd_SpecialSStickJumpTaro_Anim(HSD_GObj* gobj);
void ftDd_SpecialSStickJumpTaro_IASA(HSD_GObj* gobj);
void ftDd_SpecialSStickJumpTaro_Phys(HSD_GObj* gobj);
void ftDd_SpecialSStickJumpTaro_Coll(HSD_GObj* gobj);
void ftDd_SpecialAirSStickJumpTaro_Anim(HSD_GObj* gobj);
void ftDd_SpecialAirSStickJumpTaro_IASA(HSD_GObj* gobj);
void ftDd_SpecialAirSStickJumpTaro_Phys(HSD_GObj* gobj);
void ftDd_SpecialAirSStickJumpTaro_Coll(HSD_GObj* gobj);
void ftDd_SpecialSStickAttackTaro_Anim(HSD_GObj* gobj);
void ftDd_SpecialSStickAttackTaro_IASA(HSD_GObj* gobj);
void ftDd_SpecialSStickAttackTaro_Phys(HSD_GObj* gobj);
void ftDd_SpecialSStickAttackTaro_Coll(HSD_GObj* gobj);
void ftDd_SpecialAirSStickAttackTaro_Anim(HSD_GObj* gobj);
void ftDd_SpecialAirSStickAttackTaro_IASA(HSD_GObj* gobj);
void ftDd_SpecialAirSStickAttackTaro_Phys(HSD_GObj* gobj);
void ftDd_SpecialAirSStickAttackTaro_Coll(HSD_GObj* gobj);

/* ------------------------------------------------------------------------------------------ */
/* Special Hi: Rocketbarrel Boost (ftdiddyspecialhi.c)                                        */
/* ------------------------------------------------------------------------------------------ */

void ftDd_SpecialHi_Enter(HSD_GObj* gobj);
void ftDd_SpecialAirHi_Enter(HSD_GObj* gobj);

void ftDd_SpecialHiStart_Anim(HSD_GObj* gobj);
void ftDd_SpecialHiStart_IASA(HSD_GObj* gobj);
void ftDd_SpecialHiStart_Phys(HSD_GObj* gobj);
void ftDd_SpecialHiStart_Coll(HSD_GObj* gobj);
void ftDd_SpecialHiCharge_Anim(HSD_GObj* gobj);
void ftDd_SpecialHiCharge_IASA(HSD_GObj* gobj);
void ftDd_SpecialHiCharge_Phys(HSD_GObj* gobj);
void ftDd_SpecialHiCharge_Coll(HSD_GObj* gobj);
void ftDd_SpecialAirHiStart_Anim(HSD_GObj* gobj);
void ftDd_SpecialAirHiStart_IASA(HSD_GObj* gobj);
void ftDd_SpecialAirHiStart_Phys(HSD_GObj* gobj);
void ftDd_SpecialAirHiStart_Coll(HSD_GObj* gobj);
void ftDd_SpecialAirHiCharge_Anim(HSD_GObj* gobj);
void ftDd_SpecialAirHiCharge_IASA(HSD_GObj* gobj);
void ftDd_SpecialAirHiCharge_Phys(HSD_GObj* gobj);
void ftDd_SpecialAirHiCharge_Coll(HSD_GObj* gobj);
void ftDd_SpecialAirHiJump_Anim(HSD_GObj* gobj);
void ftDd_SpecialAirHiJump_IASA(HSD_GObj* gobj);
void ftDd_SpecialAirHiJump_Phys(HSD_GObj* gobj);
void ftDd_SpecialAirHiJump_Coll(HSD_GObj* gobj);
void ftDd_SpecialAirHiDamage_Anim(HSD_GObj* gobj);
void ftDd_SpecialAirHiDamage_IASA(HSD_GObj* gobj);
void ftDd_SpecialAirHiDamage_Phys(HSD_GObj* gobj);
void ftDd_SpecialAirHiDamage_Coll(HSD_GObj* gobj);

/* ------------------------------------------------------------------------------------------ */
/* Special Lw: Banana Peel (ftdiddyspeciallw.c)                                               */
/* ------------------------------------------------------------------------------------------ */

void ftDd_SpecialLw_Enter(HSD_GObj* gobj);
void ftDd_SpecialAirLw_Enter(HSD_GObj* gobj);

void ftDd_SpecialLw_Anim(HSD_GObj* gobj);
void ftDd_SpecialLw_IASA(HSD_GObj* gobj);
void ftDd_SpecialLw_Phys(HSD_GObj* gobj);
void ftDd_SpecialLw_Coll(HSD_GObj* gobj);
void ftDd_SpecialAirLw_Anim(HSD_GObj* gobj);
void ftDd_SpecialAirLw_IASA(HSD_GObj* gobj);
void ftDd_SpecialAirLw_Phys(HSD_GObj* gobj);
void ftDd_SpecialAirLw_Coll(HSD_GObj* gobj);

/* ------------------------------------------------------------------------------------------ */
/* Articles (itdiddy.c)                                                                       */
/* ------------------------------------------------------------------------------------------ */

/* Puts a released banana into its thrown-from-hand state. */
void itDdBanana_SpawnThrown_Enter(Item_GObj* gobj);

/* ------------------------------------------------------------------------------------------ */
/* The ability Kirby copies from Diddy (diddy_kirby.c, m-ex kbFunction of PlKbCpDd.dat)        */
/* ------------------------------------------------------------------------------------------ */

extern const MuAkKirbyCopy ftKbDd_Copy;

#endif
