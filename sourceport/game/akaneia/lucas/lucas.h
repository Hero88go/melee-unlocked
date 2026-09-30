/* Akaneia's Lucas, native.
 *
 * Hand-written C for the behavior the Akaneia disc ships as PowerPC inside PlLc.dat (the m-ex
 * "ftFunction" and "itFunction" blobs). Every routine here was read from that code and rewritten
 * against the decomp's own types; console offsets are kept in comments so the next reader can go
 * back to the original. See NOTES.md for the per-routine status and what the integration layer
 * must provide. */
#ifndef MU_AK_LUCAS_H
#define MU_AK_LUCAS_H

#include <mu_disc.h>

#include <melee/ft/forward.h>
#include <melee/ft/types.h>
#include <melee/ft/kinds/ftCommon/types.h>
#include <melee/it/forward.h>
#include <melee/it/types.h>
#include <melee/ft/inlines.h>
#include <melee/it/inlines.h>
#include <melee/it/item.h>
#include <melee/it/kinds/types.h>
#include <melee/mp/types.h>
#include <sysdolphin/baselib/forward.h>

#include "../mu_ak_fighter.h"

/* The disc's float compares use cror with the unordered bit in places; this keeps the NaN
 * branches identical where it matters. */
#define LC_ISNAN(x) ((x) != (x))

/* ---------------------------------------------------------------------------------------------
 * Motion states (move_logic), numbered from ftCo_MS_Count (341) exactly as the disc's table.
 * ------------------------------------------------------------------------------------------- */
enum ftLucas_MotionState {
    ftLc_MS_AttackS4 = 341,
    ftLc_MS_SpecialNStart,      /* 342  PK Freeze */
    ftLc_MS_SpecialNHold,       /* 343 */
    ftLc_MS_SpecialNEnd,        /* 344 */
    ftLc_MS_SpecialAirNStart,   /* 345 */
    ftLc_MS_SpecialAirNHold,    /* 346 */
    ftLc_MS_SpecialAirNEnd,     /* 347 */
    ftLc_MS_SpecialS,           /* 348  PK Fire */
    ftLc_MS_SpecialAirS,        /* 349 */
    ftLc_MS_SpecialHiStart,     /* 350  PK Thunder */
    ftLc_MS_SpecialHiHold,      /* 351 */
    ftLc_MS_SpecialHiEnd,       /* 352 */
    ftLc_MS_SpecialHi,          /* 353  PK Thunder 2 (self hit), grounded */
    ftLc_MS_SpecialAirHiStart,  /* 354 */
    ftLc_MS_SpecialAirHiHold,   /* 355 */
    ftLc_MS_SpecialAirHiEnd,    /* 356 */
    ftLc_MS_SpecialAirHi,       /* 357  PK Thunder 2, aerial */
    ftLc_MS_SpecialHiBound,     /* 358  PK Thunder 2 bounced off a wall or ceiling */
    ftLc_MS_SpecialLwStart,     /* 359  PSI Magnet */
    ftLc_MS_SpecialLwHold,      /* 360 */
    ftLc_MS_SpecialLwHit,       /* 361 */
    ftLc_MS_SpecialLwEnd,       /* 362 */
    ftLc_MS_SpecialLwTurn,      /* 363  unreachable (see NOTES.md) */
    ftLc_MS_SpecialAirLwStart,  /* 364 */
    ftLc_MS_SpecialAirLwHold,   /* 365 */
    ftLc_MS_SpecialAirLwHit,    /* 366 */
    ftLc_MS_SpecialAirLwEnd,    /* 367 */
    ftLc_MS_SpecialAirLwTurn,   /* 368  unreachable */
    ftLc_MS_AirCatch,           /* 369  Rope Snake tether (zair) */
    ftLc_MS_AirCatchHit,        /* 370  hanging from a ledge by the snake */
    ftLc_MS_End,
};
#define ftLc_MS_SelfCount (ftLc_MS_End - 341)

/* Common motion ids Lucas checks by number. */
#define ftLc_MS_Catch 212          /* ftCo_MS_Catch */
#define ftLc_MS_CatchDash 214      /* ftCo_MS_CatchDash */
#define ftLc_MS_FallSpecialEntry 236 /* the common state AirCatch reads a landing lag for */
#define ftLc_MS_JumpAerialF 27
#define ftLc_MS_JumpAerialB 28

/* The motion flags the disc code passes, spelled as the decomp's MotionFlags. */
#define ftLc_MF_Switch ftCommon_GroundAirColl_MF                      /* 0x0C4C5080 */
#define ftLc_MF_SwitchGfx (ftCommon_GroundAirColl_MF | Ft_MF_KeepGfx) /* 0x0C4C5082 */
#define ftLc_MF_SwitchGfxHit \
    (ftCommon_GroundAirColl_MF | Ft_MF_KeepGfx | Ft_MF_SkipHit)       /* 0x0C4C508A */
#define ftLc_MF_MagnetEnd \
    ((ftCommon_GroundAirColl_MF & ~Ft_MF_SkipColAnim) | Ft_MF_KeepGfx) /* 0x0C4C4082 */

/* Lucas's bones the code names by index (his skeleton, not Ness's FtPart order). */
#define ftLc_Part_Hip 4           /* parts[4]:  PK Thunder / rope checks (console fp->parts + 0x40) */
#define ftLc_Part_Magnet 11       /* parts[11]: PSI Magnet effect (+0xB0) */
#define ftLc_Part_Head 24         /* parts[24]: PK Freeze / PK Thunder spawn (+0x180) */
#define ftLc_Part_Hand 44         /* parts[44]: held item, PK Fire, rope root (+0x2C0) */
#define ftLc_Part_SnakeHead 139   /* parts[139]: replaced by the snake head joint (+0x8B0) */

/* The m-ex article slots in PlLc.dat's itFunction, and ft_data->x48_items. */
enum ftLucas_Article {
    ftLc_Art_PKFreeze = 0,
    ftLc_Art_PKFire = 1,
    ftLc_Art_Unused2 = 2,         /* empty on the disc */
    ftLc_Art_PKThunder = 3,       /* the head */
    ftLc_Art_PKThunderTail1 = 4,  /* tail segments 0-4 */
    ftLc_Art_PKThunderTail2 = 5,  /* 5-7 */
    ftLc_Art_PKThunderTail3 = 6,  /* 8 */
    ftLc_Art_PKThunderTail4 = 7,  /* 9 */
    ftLc_Art_Stick = 8,           /* forward smash stick, win pose stick */
    ftLc_Art_Snake = 9,           /* Rope Snake: grab, tether, taunt, intro */
    ftLc_Art_Count = 10,
};

/* ---------------------------------------------------------------------------------------------
 * Special attributes: ft_data->ext_attr, 0x13C bytes of disc data (big-endian).
 * ------------------------------------------------------------------------------------------- */
typedef struct ftLucasAttributes {
    /* PK Freeze */
    /* +000 */ s32 x0_PKFREEZE_UNK;           /* copied to fp->x2338.y, never read */
    /* +004 */ s32 x4_PKFREEZE_HOLD_MIN;      /* hold frames before the release checks */
    /* +008 */ s32 x8_PKFREEZE_END_DELAY;     /* frames held after the freeze is gone */
    /* +00C */ s32 xC_PKFREEZE_GRAVITY_DELAY;
    /* +010 */ float x10_PKFREEZE_UNK;
    /* +014 */ float x14_PKFREEZE_FALL_ACCEL;
    /* +018 */ float x18_PKFREEZE_UNK;
    /* +01C */ float x1C_PKFREEZE_LANDING_LAG; /* 0: plain Fall after the aerial version */
    /* +020 */ ftCollisionBox_BE x20_PKFREEZE_LAND_BOX;
    /* +038 */ float x38_unused[3];
    /* PK Fire */
    /* +044 */ float x44_PKFIRE_VEL_X;
    /* +048 */ float x48_PKFIRE_SPAWN_X;
    /* +04C */ float x4C_PKFIRE_SPAWN_Y;
    /* +050 */ float x50_PKFIRE_LANDING_LAG;
    /* +054 */ float x54_unused;
    /* PK Thunder */
    /* +058 */ s32 x58_PKT_LOOP1;
    /* +05C */ s32 x5C_PKT_LOOP2;
    /* +060 */ s32 x60_PKT_GRAVITY_DELAY;
    /* +064 */ float x64_unused;
    /* +068 */ float x68_PKT_FALL_ACCEL;       /* also PK Thunder 2's per-frame drop */
    /* +06C */ float x6C_PKT2_SPEED;           /* also the cap on that drop */
    /* +070 */ float x70_PKT2_UNK;             /* stored in the motion vars, not read */
    /* +074 */ float x74_PKT2_DECEL;
    /* +078 */ float x78_PKT2_KNOCKDOWN_ANGLE; /* degrees */
    /* +07C */ float x7C_PKT2_WALLHUG_ANGLE;   /* degrees */
    /* +080 */ float x80_unused;
    /* +084 */ float x84_PKT_FREEFALL_MOBILITY;
    /* +088 */ float x88_PKT_LANDING_LAG;      /* 0: plain Fall */
    /* PSI Magnet */
    /* +08C */ float x8C_MAGNET_MIN_FRAMES;    /* truncated to int */
    /* +090 */ float x90_MAGNET_TURN_FRAMES;
    /* +094 */ float x94_MAGNET_HIT_REFRESH_FRAME;
    /* +098 */ float x98_unused;
    /* +09C */ s32 x9C_MAGNET_SFX_DELAY;
    /* +0A0 */ float xA0_MAGNET_AIR_VEL_X_DIV;
    /* +0A4 */ float xA4_MAGNET_FALL_ACCEL;
    /* +0A8 */ float xA8_unused;
    /* +0AC */ float xAC_MAGNET_HEAL_MUL;
    /* +0B0 */ float xB0_MAGNET_TURN_STICK;
    /* +0B4 */ AbsorbDesc_BE xB4_MAGNET_ABSORB;
    /* Forward smash stick */
    /* +0C8 */ ReflectDesc_BE xC8_STICK_REFLECT;
    /* Rope Snake (tether) */
    /* +0EC */ s32 xEC_TETHER_ENABLED;         /* 1: EnterTether goes to AirCatch */
    /* +0F0 */ s32 xF0_TETHER_HAND_PART;
    /* +0F4 */ s32 xF4_ROPE_FIRST_JOINT;
    /* +0F8 */ s32 xF8_ROPE_TIP_JOINT;
    /* +0FC */ s32 xFC_unused;
    /* +100 */ float x100_ROPE_HANG_DAMPING;
    /* +104 */ s32 x104_ROPE_SEGMENTS;
    /* +108 */ s32 x108_ROPE_ITERATIONS;
    /* +10C */ float x10C_ROPE_LAUNCH;
    /* +110 */ float x110_ROPE_REEL_STEP;
    /* +114 */ float x114_ROPE_REEL_MIN;
    /* +118 */ float x118_ROPE_CLIMB_DIST;
    /* +11C */ s32 x11C_HANG_FRAMES;
    /* +120 */ s32 x120_REEL_FRAMES;
    /* +124 */ float x124_CLIMB_FORCE;
    /* +128 */ float x128_LEDGE_SEARCH_SIZE;
    /* +12C */ float x12C_TIP_OFFSET_X;
    /* +130 */ s32 x130_WALL_CHECK_DIST;
    /* +134 */ float x134_AIRCATCH_LANDING_LAG;
    /* +138 */ s32 x138_MISS_LANDING_LAG;
} DISC_STRUCT ftLucasAttributes;

/* ---------------------------------------------------------------------------------------------
 * Rope Snake physics (the "aircatch.c" of the original): a verlet rope of masses and springs,
 * allocated per fighter at OnLoad (console: 0x598 bytes at fp+0x2258).
 * ------------------------------------------------------------------------------------------- */
#define LUCAS_ROPE_MAX_MASSES 21  /* the console asserts beyond this (it can reach 22) */
#define LUCAS_ROPE_MAX_SPRINGS 20

typedef struct LucasMass {
    /* +00 */ u8 pinned;
    /* +01 */ u8 x1;
    /* +04 */ Vec3 pos;
    /* +10 */ Vec3 prev_pos;
    /* +1C */ Vec3 pin_pos;
    /* +28 */ float gravity;
    /* +2C */ float damping;   /* applied to x only, as on console */
    /* +30 */ HSD_JObj* joint;
} LucasMass; /* console stride 0x34 */

typedef struct LucasSpring {
    /* +0 */ LucasMass* a;
    /* +4 */ LucasMass* b;
    /* +8 */ float rest_len;
    /* +C */ float max_len;
} LucasSpring; /* console stride 0x10 */

struct LucasRope;
typedef void (*LucasMassCallback)(struct LucasRope* rope, LucasMass* mass);

typedef struct LucasRope {
    /* +000 */ int mass_count;
    /* +004 */ int spring_count;
    /* +008 */ int iterations;
    /* +00C */ float stiffness;
    /* +010 */ LucasMassCallback mass_cb;
    /* +014 */ LucasMass masses[LUCAS_ROPE_MAX_MASSES + 2];   /* +2: see Lucas_UpdateSnakeModel */
    /* +458 */ LucasSpring springs[LUCAS_ROPE_MAX_SPRINGS + 1];
} LucasRope;

/* ---------------------------------------------------------------------------------------------
 * Per-fighter state. The console keeps these at fixed offsets inside the Fighter (the decomp's
 * fp->u union at +222C and fp->mv at +2340); here they overlay the same unions by name.
 * ------------------------------------------------------------------------------------------- */
typedef struct ftLucas_FighterVars {
    /* +222C */ int x222C;
    /* +2230 */ int x2230[4];
    /* +2240 */ Item_GObj* pkfreeze_gobj;
    /* +2244 */ Item_GObj* pkthunder_gobj;
    /* +2248 */ Item_GObj* held_item_gobj;   /* stick (fsmash, win pose) or the Rope Snake */
    /* +224C */ int pkthunder_gfx;
    /* +2250 */ int x2250[2];
    /* +2258 */ LucasRope* rope;
} ftLucas_FighterVars;

typedef union ftLucas_MotionVars {
    struct {
        /* +2340 */ int hold_min;
        /* +2344 */ int end_delay;
        /* +2348 */ int gravity_delay;
        /* +234C */ int release_delay;
    } specialn;
    struct {
        /* +2340 */ int state;        /* 1 head near Lucas, 0 armed, 2 hit Lucas */
        /* +2344 */ int loop1;
        /* +2348 */ int loop2;
        /* +234C */ int gravity_delay;
        /* +2350 */ Vec3 head_pos0;
        /* +235C */ Vec3 head_pos1;
        /* +2368 */ float angle;
        /* +236C */ float x236C;
        /* +2370 */ float dir_y;
        /* +2374 */ Vec3 saved_vel;
        /* +2380 */ int frame;
        /* +2384 */ Vec3 drop;       /* only .x is used */
    } specialhi;
    struct {
        /* +2340 */ int turn_timer;
        /* +2344 */ int released;
        /* +2348 */ int gravity_delay;
        /* +234C */ int sfx_timer;
        /* +2350 */ float x2350;
    } speciallw;
    struct {
        /* +2340 */ int line_id;
        /* +2344 */ int x2344;
        /* +2348 */ int reeling;
        /* +234C */ int timer;
        /* +2350 */ float rope_scale;
        /* +2354 */ int landing_lag;
    } aircatch;
} ftLucas_MotionVars;

static inline ftLucas_FighterVars* ftLc_Vars(Fighter* fp)
{
    _Static_assert(sizeof(ftLucas_FighterVars) <= sizeof(fp->u), "Lucas vars overflow fp->u");
    return (ftLucas_FighterVars*) &fp->u;
}

static inline ftLucas_MotionVars* ftLc_MV(Fighter* fp)
{
    _Static_assert(sizeof(ftLucas_MotionVars) <= sizeof(fp->mv), "Lucas motion vars overflow fp->mv");
    return (ftLucas_MotionVars*) &fp->mv;
}

static inline ftLucasAttributes* ftLc_Attrs(Fighter* fp)
{
    return (ftLucasAttributes*) fp->dat_attrs;
}

/* fp+0x233C: the int just below the motion vars (the decomp's x2338.y). The disc code keeps the
 * PK Freeze copy and PSI Magnet's minimum-frames counter there. */
#define ftLc_X233C(fp) ((fp)->x2338.y)

/* ---------------------------------------------------------------------------------------------
 * What the integration layer (mu_ak_fighters.c) must provide. On console these are m-ex runtime
 * calls at fixed addresses (0x803D7058, 0x803D706C, 0x803D7088) or reads of static game data.
 * ------------------------------------------------------------------------------------------- */
/* m-ex 0x803D7058: register article `index` of this fighter kind (ft_data->x48_items[index]) so
 * items of that kind can spawn. Console arguments: (fp->kind, desc, index). */
void mu_ak_register_article(FighterKind kind, void* article_desc, int index);
/* m-ex 0x803D7088: the runtime ItemKind of this fighter's article `index`. */
ItemKind mu_ak_article_kind(HSD_GObj* fighter_gobj, int index);
/* mplib's static jointListStart (console 0x804D64C4), for the tether ledge search. */
CollJoint* mu_ak_mp_joint_list(void);

/* ---------------------------------------------------------------------------------------------
 * Shared routines (one owner file each).
 * ------------------------------------------------------------------------------------------- */
/* lucas.c */
void ftLc_OnLoad(HSD_GObj* gobj);
void ftLc_OnRespawn(HSD_GObj* gobj);
void ftLc_OnDestroy(HSD_GObj* gobj);
void ftLc_ResetAttributes(HSD_GObj* gobj);
void ftLc_RemoveAllArticles(HSD_GObj* gobj);
void ftLc_RemoveAndDestroyItem(Item_GObj* item_gobj);
float ftLc_Sign(float x);
Item_GObj* ftLc_SpawnStick(HSD_GObj* gobj, Vec3* pos, int part, float facing);
Item_GObj* ftLc_SpawnSnake(HSD_GObj* gobj, Vec3* pos, int part, int state, float facing);
void ftLc_CommonSpawnSnake(HSD_GObj* gobj, int state);
void ftLc_Snake_DestroyCB(HSD_GObj* gobj);
void ftLc_InitCpuSpoof(HSD_GObj* gobj, int spoof_kind);

/* m-ex ftFunction slots past the end of MuAkFighter (MexTK/ftFunction.txt stops at 37); names
 * from the disc's debug symbols. See NOTES.md. */
void mu_ak_lucas_on_intro_l(HSD_GObj* gobj); /* slot 41 */
void mu_ak_lucas_on_appeal(HSD_GObj* gobj);  /* slot 43 */
void mu_ak_lucas_on_catch(HSD_GObj* gobj);   /* slot 44 */

/* lucas_attacks4.c */
void ftLc_AttackS4_Enter(HSD_GObj* gobj);
void ftLc_AttackS4_Anim(HSD_GObj* gobj);
void ftLc_AttackS4_IASA(HSD_GObj* gobj);
void ftLc_AttackS4_Phys(HSD_GObj* gobj);
void ftLc_AttackS4_Coll(HSD_GObj* gobj);

/* lucas_specialn.c */
void ftLc_SpecialN_Enter(HSD_GObj* gobj);
void ftLc_SpecialAirN_Enter(HSD_GObj* gobj);
void ftLc_SpecialNStart_Anim(HSD_GObj* gobj);
void ftLc_SpecialNStart_IASA(HSD_GObj* gobj);
void ftLc_SpecialNStart_Phys(HSD_GObj* gobj);
void ftLc_SpecialNStart_Coll(HSD_GObj* gobj);
void ftLc_SpecialNHold_Anim(HSD_GObj* gobj);
void ftLc_SpecialNHold_IASA(HSD_GObj* gobj);
void ftLc_SpecialNHold_Phys(HSD_GObj* gobj);
void ftLc_SpecialNHold_Coll(HSD_GObj* gobj);
void ftLc_SpecialNEnd_Anim(HSD_GObj* gobj);
void ftLc_SpecialNEnd_Phys(HSD_GObj* gobj);
void ftLc_SpecialAirNStart_Anim(HSD_GObj* gobj);
void ftLc_SpecialAirNStart_IASA(HSD_GObj* gobj);
void ftLc_SpecialAirN_Phys(HSD_GObj* gobj);
void ftLc_SpecialAirNStart_Coll(HSD_GObj* gobj);
void ftLc_SpecialAirNHold_Anim(HSD_GObj* gobj);
void ftLc_SpecialAirNHold_Coll(HSD_GObj* gobj);
void ftLc_SpecialAirNEnd_Anim(HSD_GObj* gobj);
void ftLc_SpecialAirNEnd_Coll(HSD_GObj* gobj);
void ftLc_RemovePKFreeze(HSD_GObj* gobj);

/* lucas_specials.c */
void ftLc_SpecialS_Enter(HSD_GObj* gobj);
void ftLc_SpecialAirS_Enter(HSD_GObj* gobj);
void ftLc_SpecialS_Anim(HSD_GObj* gobj);
void ftLc_SpecialS_Phys(HSD_GObj* gobj);
void ftLc_SpecialS_Coll(HSD_GObj* gobj);
void ftLc_SpecialAirS_Anim(HSD_GObj* gobj);
void ftLc_SpecialAirS_Phys(HSD_GObj* gobj);
void ftLc_SpecialAirS_Coll(HSD_GObj* gobj);

/* lucas_specialhi.c */
void ftLc_SpecialHi_Enter(HSD_GObj* gobj);
void ftLc_SpecialAirHi_Enter(HSD_GObj* gobj);
void ftLc_SpecialHiStart_Anim(HSD_GObj* gobj);
void ftLc_SpecialHi_NoIASA(HSD_GObj* gobj);
void ftLc_SpecialHiStart_Phys(HSD_GObj* gobj);
void ftLc_SpecialHiStart_Coll(HSD_GObj* gobj);
void ftLc_SpecialHiHold_Anim(HSD_GObj* gobj);
void ftLc_SpecialHiHold_Phys(HSD_GObj* gobj);
void ftLc_SpecialHiHold_Coll(HSD_GObj* gobj);
void ftLc_SpecialHiEnd_Anim(HSD_GObj* gobj);
void ftLc_SpecialHiEnd_Phys(HSD_GObj* gobj);
void ftLc_SpecialHiEnd_Coll(HSD_GObj* gobj);
void ftLc_SpecialHi_Anim(HSD_GObj* gobj);
void ftLc_SpecialHi_Phys(HSD_GObj* gobj);
void ftLc_SpecialHi_Coll(HSD_GObj* gobj);
void ftLc_SpecialAirHiStart_Anim(HSD_GObj* gobj);
void ftLc_SpecialAirHiStart_Phys(HSD_GObj* gobj);
void ftLc_SpecialAirHiStart_Coll(HSD_GObj* gobj);
void ftLc_SpecialAirHiHold_Anim(HSD_GObj* gobj);
void ftLc_SpecialAirHiHold_Coll(HSD_GObj* gobj);
void ftLc_SpecialAirHiEnd_Anim(HSD_GObj* gobj);
void ftLc_SpecialAirHiEnd_Coll(HSD_GObj* gobj);
void ftLc_SpecialAirHi_Anim(HSD_GObj* gobj);
void ftLc_SpecialAirHi_Phys(HSD_GObj* gobj);
void ftLc_SpecialAirHi_Coll(HSD_GObj* gobj);
void ftLc_SpecialHiBound_Anim(HSD_GObj* gobj);
void ftLc_SpecialHiBound_Phys(HSD_GObj* gobj);
void ftLc_SpecialHiBound_Coll(HSD_GObj* gobj);
void ftLc_SpecialHi_DestroyGfx(HSD_GObj* gobj);
void ftLc_RemovePKThunder(HSD_GObj* gobj);
void ftLc_ThunderHead_GetPosition(Item_GObj* head, Vec3* out, int index);

/* lucas_speciallw.c */
void ftLc_SpecialLw_Enter(HSD_GObj* gobj);
void ftLc_SpecialAirLw_Enter(HSD_GObj* gobj);
void ftLc_SpecialLw_OnAbsorb(HSD_GObj* gobj);
void ftLc_SpecialLwStart_Anim(HSD_GObj* gobj);
void ftLc_SpecialLwStart_IASA(HSD_GObj* gobj);
void ftLc_SpecialLwStart_Phys(HSD_GObj* gobj);
void ftLc_SpecialLwStart_Coll(HSD_GObj* gobj);
void ftLc_SpecialLwHold_Anim(HSD_GObj* gobj);
void ftLc_SpecialLw_NoIASA(HSD_GObj* gobj);
void ftLc_SpecialLwHold_Phys(HSD_GObj* gobj);
void ftLc_SpecialLwHold_Coll(HSD_GObj* gobj);
void ftLc_SpecialLwHit_Anim(HSD_GObj* gobj);
void ftLc_SpecialLwHit_Coll(HSD_GObj* gobj);
void ftLc_SpecialLwEnd_Anim(HSD_GObj* gobj);
void ftLc_SpecialLwEnd_Phys(HSD_GObj* gobj);
void ftLc_SpecialLwEnd_Coll(HSD_GObj* gobj);
void ftLc_SpecialLwTurn_Anim(HSD_GObj* gobj);
void ftLc_SpecialLwTurn_Coll(HSD_GObj* gobj);
void ftLc_SpecialAirLwStart_Phys(HSD_GObj* gobj);
void ftLc_SpecialAirLwStart_Coll(HSD_GObj* gobj);
void ftLc_SpecialAirLwHold_Phys(HSD_GObj* gobj);
void ftLc_SpecialAirLwHold_Coll(HSD_GObj* gobj);
void ftLc_SpecialAirLwHit_Coll(HSD_GObj* gobj);
void ftLc_SpecialAirLwEnd_Phys(HSD_GObj* gobj);
void ftLc_SpecialAirLwEnd_Coll(HSD_GObj* gobj);
void ftLc_SpecialAirLwTurn_Coll(HSD_GObj* gobj);

/* lucas_aircatch.c */
void ftLc_AirCatch_Enter(HSD_GObj* gobj);
void ftLc_AirCatch_Anim(HSD_GObj* gobj);
void ftLc_AirCatch_IASA(HSD_GObj* gobj);
void ftLc_AirCatch_Phys(HSD_GObj* gobj);
void ftLc_AirCatch_Coll(HSD_GObj* gobj);
void ftLc_AirCatchHit_Anim(HSD_GObj* gobj);
void ftLc_AirCatchHit_IASA(HSD_GObj* gobj);
void ftLc_AirCatchHit_Phys(HSD_GObj* gobj);
void ftLc_AirCatchHit_Coll(HSD_GObj* gobj);
void ftLc_Rope_Render(HSD_GObj* gobj);
void ftLc_Catch_Accessory(HSD_GObj* gobj);

/* Articles (itFunction). Index = enum ftLucas_Article; slot 2 is empty on the disc. */
extern ItemLogicTable ftLc_ArticleLogic[ftLc_Art_Count];

/* lucas_it_pkfreeze.c / lucas_it_pkfire.c / lucas_it_pkthunder.c / lucas_it_misc.c */
Item_GObj* ftLc_PKFreeze_Spawn(HSD_GObj* owner, Vec3* pos, ItemKind kind, float facing);
Item_GObj* ftLc_PKFire_Spawn(HSD_GObj* owner, Vec3* pos, Vec3* vel, float facing);
Item_GObj* ftLc_PKThunder_Spawn(HSD_GObj* owner, Vec3* pos, float facing);

extern ItemStateTable ftLc_PKFreeze_States[3];
extern ItemStateTable ftLc_PKFire_States[2];
extern ItemStateTable ftLc_PKThunder_States[1];
extern ItemStateTable ftLc_PKThunderTail_States[1];
extern ItemStateTable ftLc_Stick_States[1];
extern ItemStateTable ftLc_Snake_States[8];

bool ftLc_PKFreeze_OnReflect(Item_GObj* gobj);
void ftLc_PKFreeze_OnDestroy(Item_GObj* gobj);
void ftLc_PKFire_OnDestroy(Item_GObj* gobj);
bool ftLc_PKFire_OnGiveDamage(Item_GObj* gobj);
bool ftLc_PKFire_OnReflect(Item_GObj* gobj);
bool ftLc_PKFire_OnHitShieldBounce(Item_GObj* gobj);
bool ftLc_PKFire_OnHitShieldDestroy(Item_GObj* gobj);
void ftLc_PKThunder_OnDestroy(Item_GObj* gobj);
bool ftLc_PKThunder_OnGiveDamage(Item_GObj* gobj);
bool ftLc_PKThunder_OnReflect(Item_GObj* gobj);
bool ftLc_PKThunder_OnHitShieldBounce(Item_GObj* gobj);
bool ftLc_PKThunder_OnHitShieldDestroy(Item_GObj* gobj);
void ftLc_Stick_OnPickup(Item_GObj* gobj);
void ftLc_Snake_OnPickup(Item_GObj* gobj);

#endif
