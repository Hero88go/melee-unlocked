/* King Dedede (Akaneia), native.
 *
 * Private header for the files in this folder. Everything here was read out of the fighter's own
 * m-ex code in PlDe.dat (the "ftFunction" and "itFunction" roots) and rewritten by hand against the
 * decomp. Dedede's Neutral B is Kirby's inhale reworked: his own inhale fields mirror Kirby's
 * (fp->u.kb.xE0 to xF4 on the console) and the victim side reuses the common CaptureKirby and
 * ThrownKirbyStar motion variables, which common code reads.
 *
 * Console offsets are given in comments as "fp+XXXX" (Fighter), "ip+XXXX" (Item), "da+XXX"
 * (Dedede's special attributes, PlDe.dat ftDataDedede->ext_attr, 0x17C bytes). */
#ifndef MU_AK_DEDEDE_FTDE_H
#define MU_AK_DEDEDE_FTDE_H

#include <placeholder.h>
#include <Runtime/platform.h>

#include <melee/ft/forward.h>
#include <melee/ft/kinds/ftCommon/forward.h>
#include <melee/ft/types.h>
#include <melee/it/forward.h>
#include <melee/it/types.h>
#include <melee/it/kinds/types.h>
#include <sysdolphin/baselib/forward.h>

#include <mu_disc.h>

#include "../mu_ak_fighter.h"

/* ---------------------------------------------------------------- motion states (move_logic) */

/* move_logic entry n is action state 341 + n. The two numbers per pair of Hi/Lw states are the
 * left/right or uncharged/charged variants the code picks between. */
enum ftDe_MotionState {
    ftDe_MS_JumpAerialF1 = 341,
    ftDe_MS_JumpAerialF2,
    ftDe_MS_JumpAerialF3,
    ftDe_MS_JumpAerialF4,
    ftDe_MS_SpecialNStart,      /* 345 */
    ftDe_MS_SpecialNLoop,
    ftDe_MS_SpecialNEnd,
    ftDe_MS_SpecialNGrab,
    ftDe_MS_SpecialNGrabItem,
    ftDe_MS_SpecialNEat,        /* 350 */
    ftDe_MS_SpecialNEatWait,
    ftDe_MS_SpecialNEatTurn,
    ftDe_MS_SpecialNSpit,
    ftDe_MS_SpecialNSpitItem,
    ftDe_MS_SpecialNEatWalkSlow, /* 355 */
    ftDe_MS_SpecialNEatWalkMiddle,
    ftDe_MS_SpecialNEatWalkFast,
    ftDe_MS_SpecialNEatJump1,
    ftDe_MS_SpecialNEatJump2,
    ftDe_MS_SpecialNEatLanding, /* 360 */
    ftDe_MS_SpecialAirNStart,
    ftDe_MS_SpecialAirNLoop,
    ftDe_MS_SpecialAirNEnd,
    ftDe_MS_SpecialAirNGrab,
    ftDe_MS_SpecialAirNGrabItem, /* 365 */
    ftDe_MS_SpecialAirNEat,
    ftDe_MS_SpecialAirNEatWait,
    ftDe_MS_SpecialAirNEatTurn,
    ftDe_MS_SpecialAirNSpit,
    ftDe_MS_SpecialAirNSpitItem, /* 370 */
    ftDe_MS_SpecialS,
    ftDe_MS_SpecialAirS,
    ftDe_MS_SpecialHiStartL,    /* 373: facing left */
    ftDe_MS_SpecialHiStartR,    /* 374: facing right */
    ftDe_MS_SpecialHiJump,      /* 375 */
    ftDe_MS_SpecialHiLoop,
    ftDe_MS_SpecialHiTurnL,
    ftDe_MS_SpecialHiTurnR,
    ftDe_MS_SpecialHiLandingL,
    ftDe_MS_SpecialHiLandingR,  /* 380 */
    ftDe_MS_SpecialHiHit,
    ftDe_MS_SpecialLwStart,
    ftDe_MS_SpecialLw,          /* release, uncharged */
    ftDe_MS_SpecialLwMax,       /* release, fully charged */
    ftDe_MS_SpecialLwHold,      /* 385 */
    ftDe_MS_SpecialLwHoldMax,
    ftDe_MS_SpecialLwTurn,
    ftDe_MS_SpecialLwWalk,
    ftDe_MS_SpecialLwJumpSquat,
    ftDe_MS_SpecialLwJump,      /* 390 */
    ftDe_MS_SpecialLwFall,
    ftDe_MS_SpecialLwLanding,
    ftDe_MS_SpecialAirLwStart,
    ftDe_MS_SpecialAirLw,
    ftDe_MS_SpecialAirLwMax,    /* 395 */
    ftDe_MS_End
};
#define FTDE_MS_COUNT (ftDe_MS_End - ftDe_MS_JumpAerialF1)

/* Animation ids of the three EatWalk speeds (the per-fighter animation table, not motion states). */
#define FTDE_SM_EATWALK_SLOW 305
#define FTDE_SM_EATWALK_MIDDLE 306
#define FTDE_SM_EATWALK_FAST 307

/* Articles (ftDataDedede->x48_items, registered in that order at load). */
enum ftDe_Article {
    ftDe_Article_StarModel = 0, /* model only: the star a spat fighter rides in */
    ftDe_Article_SpitStar = 1,  /* star spat out after swallowing an item */
    ftDe_Article_Gordo = 2,     /* Side B */
    ftDe_Article_HiStar = 3,    /* the two stars of the Up B landing */
    ftDe_Article_Count = 4
};

/* Common motion states the m-ex code names by number (0x120, 0x122, 0x124, 0x45, 0xFA). */
#define FTDE_CO_CAPTUREKIRBY ftCo_MS_CaptureKirby
#define FTDE_CO_THROWNKIRBYSTAR ftCo_MS_ThrownKirbyStar
#define FTDE_CO_THROWNKIRBY ftCo_MS_ThrownKirby
#define FTDE_CO_ATTACKAIRLW ftCo_MS_AttackAirLw
#define FTDE_CO_STOPCEIL ftCo_MS_StopCeil
_Static_assert(ftCo_MS_CaptureKirby == 0x120 && ftCo_MS_ThrownKirbyStar == 0x122 &&
                   ftCo_MS_ThrownKirby == 0x124 && ftCo_MS_AttackAirLw == 0x45 && ftCo_MS_StopCeil == 0xFA,
               "common motion state numbers used by the m-ex code");
_Static_assert(ftDe_MS_End == 396, "move_logic ends at 395");

/* ---------------------------------------------------------------- special attributes */

/* PlDe.dat ext_attr, copied to fp->dat_attrs at load and respawn. Big-endian disc layout.
 * Names follow what each value does in the code; the Kirby names in brackets are the Kirby
 * attribute each one stands in for. */
typedef struct ftDe_DatAttrs {
    /* da+000 */ u16 specialn_min_loop_frames; /* [jumpaerial_unk] frames before B release ends it */
    /* da+002 */ u16 x2;
    /* da+004 */ float specialn_mouth_x;        /* [x_offset_inhaled] */
    /* da+008 */ float specialn_mouth_y;        /* [y_offset_inhaled] */
    /* da+00C */ float specialn_shrink_dist;    /* victim starts shrinking inside this distance */
    /* da+010 */ float specialn_shrink_amount;  /* how much of its size the victim loses */
    /* da+014 */ float specialn_pull_max_x;     /* max pull per frame toward the mouth */
    /* da+018 */ float specialn_pull_max_y;
    /* da+01C */ float specialn_inhale_range;   /* [inhale_velocity] compared squared */
    /* da+020 */ float specialn_mash_value;     /* grab mash, captured fighter */
    /* da+024 */ float specialn_hold_decay;     /* grab timer decrement per frame */
    /* da+028 */ float specialn_hold_time;      /* [base_duration] grab timer at capture */
    /* da+02C */ float star_mash_value;         /* grab mash while flying as a star */
    /* da+030 */ float star_time_decay;         /* grab timer decrement while a star */
    /* da+034 */ float star_time;               /* [star_base_duration] */
    /* da+038 */ float x38;
    /* da+03C */ float star_end_time;           /* [spit_spin] time spent tumbling after the star */
    /* da+040 */ float specialn_turn_stick;     /* [x_axis_range_walk] stick x to turn while full */
    /* da+044 */ float x44;
    /* da+048 */ float specialn_walk_speed;     /* [walk_speed] */
    /* da+04C */ float specialn_jump_height;    /* [jump_height] */
    /* da+050 */ float specialn_struggle_speed; /* [stop_momentum] velocity the victim's mashing adds */
    /* da+054 */ float star_speed;              /* [ground_spit_initial_horizontal_velocity] */
    /* da+058 */ float star_decel;              /* [spit_deceleration_rate] */
    /* da+05C */ float x5C[3];
    /* da+068 */ float star_end_vel_x;          /* tumble velocity after the star breaks */
    /* da+06C */ float star_end_vel_y;
    /* da+070 */ float x70[4];
    /* da+080 */ float star_coll_box[6];        /* top, bottom, left.x, left.y, right.x, right.y */
    /* da+098 */ float specialhi_start_drift[3]; /* threshold, accel, max (NoFriction drift) */
    /* da+0A4 */ float specialhi_start_vel_y;
    /* da+0A8 */ float specialhi_stick_range;
    /* da+0AC */ float specialhi_vel_x;
    /* da+0B0 */ float specialhi_vel_y;
    /* da+0B4 */ float specialhi_rise_decay;
    /* da+0B8 */ float specialhi_fall_terminal;
    /* da+0BC */ float specialhi_fall_gravity;
    /* da+0C0 */ float specialhi_rise_min;
    /* da+0C4 */ s32 specialhi_min_frames;      /* frames in Loop before down cancels */
    /* da+0C8 */ float specialhi_turn_gravity;
    /* da+0CC */ float specialhi_turn_terminal;
    /* da+0D0 */ float specialhi_turn_drift_accel;
    /* da+0D4 */ float specialhi_turn_drift_max;
    /* da+0D8 */ float specialhi_fall_mobility;
    /* da+0DC */ float specialhi_landing_lag;
    /* da+0E0 */ float xE0[7];
    /* da+0FC */ Vec3_BE specialhi_star_offset;
    /* da+108 */ s32 specials_gordo_bone;       /* Fighter_Part the Gordo is held on */
    /* da+10C */ s32 specials_smash_frames;     /* stick tapped within this many frames = smash */
    /* da+110 */ float speciallw_turn_stick;
    /* da+114 */ float x114;
    /* da+118 */ float speciallw_walk_accel;
    /* da+11C */ float speciallw_walk_max;
    /* da+120 */ float speciallw_air_accel_base;
    /* da+124 */ float speciallw_air_accel_stick;
    /* da+128 */ float speciallw_air_max_stick;
    /* da+12C */ float speciallw_air_friction;
    /* da+130 */ float x130[2];
    /* da+138 */ s32 speciallw_charge_frames;
    /* da+13C */ float speciallw_damage_per_frame;
    /* da+140 */ float speciallw_self_damage;
    /* da+144 */ s32 speciallw_self_damage_interval;
    /* da+148 */ Fighter_x2D0_t multijump;      /* fp->x2D0 points here */
} DISC_STRUCT ftDe_DatAttrs;

/* The two color overlays of the charged Down B: ftData->x48_items[4]. */
typedef DISC_PTR(struct Fighter_804D653C_t) ftDe_OverlaySlot;
typedef struct ftDe_ColAnims {
    /* +0 */ DISC_PTR(void) x0;
    /* +4 */ DISC_PTR(ftDe_OverlaySlot) overlays; /* [0] charging, [1] charged */
} DISC_STRUCT ftDe_ColAnims;

/* ---------------------------------------------------------------- per-fighter state */

/* fp->u. Only this code reads it. The inhale fields are at Kirby's offsets because the m-ex source
 * was Kirby's inhale; the rest is Dedede's own. */
typedef struct ftDe_FighterVars {
    /* fp+222C */ Item_GObj* held_gordo;   /* Side B Gordo (held or thrown, one at a time) */
    /* fp+2230 */ int x4;
    /* fp+2234 */ void* scratch_30;        /* 0x30 bytes allocated at load, never used */
    /* fp+2238 */ void* scratch_C0;        /* 0xC0 bytes allocated at load, never used */
    /* fp+223C */ HSD_JObj* ecb_joint;     /* parts[5]: ECB bone for every state */
    /* fp+2240 */ HSD_JObj* ecb_joint_dair; /* parts[46]: ECB bone for Down Air */
    /* fp+230C */ int specialn_x0;         /* set to 4 on Neutral B, never read [kb.xE0] */
    /* fp+2310 */ u16 specialn_loop_timer; /* [kb.xE4] */
    /* fp+2314 */ float eatwalk_slow_len;  /* [kb.xE8] animation lengths for the EatWalk blend */
    /* fp+2318 */ float eatwalk_middle_len;
    /* fp+231C */ float eatwalk_fast_len;
    /* fp+2320:0 */ u8 captured_item : 1;  /* [kb.xF4_b0] mouth holds an item, not a fighter */
} ftDe_FighterVars;

/* fp->mv while Dedede is in his own states. */
typedef union ftDe_MotionVars {
    struct {
        /* fp+2340 */ int x0;
        /* fp+2344 */ int jump_input;  /* EatJump: ftCo_Jump_GetInput, then "already jumped" */
    } specialn;
    struct {
        /* fp+2340 */ u8 spawned;      /* Gordo is out */
        /* fp+2341 */ u8 thrown;       /* and has been let go */
        /* fp+2344 */ int smash;       /* the stick was tapped: faster throw */
    } specials;
    struct {
        /* fp+2340 */ int x0;
        /* fp+2344 */ int falling;     /* 0 rising, 1 falling */
        /* fp+2348 */ int frames;
    } specialhi;
    struct {
        /* fp+2340 */ int x0;
        /* fp+2344 */ int jump_input;
        /* fp+2348 */ int x8;
        /* fp+234C */ int charge;       /* frames charged, up to speciallw_charge_frames */
        /* fp+2350 */ int damage_timer; /* frames until the next self damage at full charge */
        /* fp+2354 */ int charged;      /* full charge reached (effect and overlay done) */
        /* fp+2358 */ int base_damage[4]; /* hitbox damage before the charge bonus, -1 = unread */
    } speciallw;
} ftDe_MotionVars;

/* ip->xDD4_itemVar of the articles. */
typedef struct itDe_SpitStarVars {
    /* ip+DD4 */ float decel; /* speed lost per frame */
} itDe_SpitStarVars;

typedef struct itDe_GordoVars {
    /* ip+DD4 */ int toss;     /* 0 forward, 1 down, 2 up (from the stick at release) */
    /* ip+DD8 */ float slow;   /* horizontal speed lost per frame toward gordo_cruise_speed */
    /* ip+DDC */ u32 dedede;   /* the Dedede that spawned it (console address width, like the
                                * game's motion vars); m-ex kept this past the end of the item
                                * at ip+FCC, see NOTES.md */
} itDe_GordoVars;

/* The Gordo's article attributes (Article->x4_specialAttributes). */
typedef struct itDe_GordoAttrs {
    /* +00 */ float lifetime;
    /* +04 */ float hit_lifetime_bonus;  /* lifetime added when it is hit back */
    /* +08 */ float hit_damage_scale;
    /* +0C */ s32 hit_min_damage;        /* smaller hits do not knock it back */
    /* +10 */ float x10[5];
    /* +24 */ float fwd_speed, fwd_angle, fwd_slow;   /* forward toss */
    /* +30 */ float down_speed, down_angle, down_slow; /* down toss */
    /* +3C */ float down_bounce;         /* vertical bounce kept on landing after a down toss */
    /* +40 */ float up_speed, up_angle, up_slow;       /* up toss */
    /* +4C */ float up_bounce;
    /* +50 */ float spin_min;            /* degrees per frame */
    /* +54 */ float spin_max;
    /* +58 */ float rebound_x;           /* velocity kept after it hits someone */
    /* +5C */ float rebound_y;
    /* +60 */ float rebound_damage_scale;
    /* +64 */ float cruise_speed;
    /* +68 */ float smash_speed_mul;     /* smash Side B */
} DISC_STRUCT itDe_GordoAttrs;

/* The Up B landing stars' article attributes. */
typedef struct itDe_HiStarAttrs {
    /* +00 */ float lifetime;
    /* +04 */ float speed_x;
    /* +08 */ float speed_y;
} DISC_STRUCT itDe_HiStarAttrs;

static inline ftDe_FighterVars* ftDe_Vars(Fighter* fp)
{
    return (ftDe_FighterVars*) &fp->u;
}

static inline ftDe_MotionVars* ftDe_MV(Fighter* fp)
{
    return (ftDe_MotionVars*) &fp->mv;
}

static inline ftDe_DatAttrs* ftDe_Attrs(Fighter* fp)
{
    return (ftDe_DatAttrs*) fp->dat_attrs;
}

/* ---------------------------------------------------------------- m-ex services (integration layer) */

/* The m-ex runtime functions Dedede's code calls. They live in the m-ex codeset on the console; the
 * integration layer (mu_ak_fighters.c) provides them natively. See NOTES.md. */

/* m-ex 0x803D7058: register article `index` of fighter `kind` (its item descriptor from
 * ftData->x48_items) so it can be spawned. Called from OnLoad for articles 0 to 3. */
void mu_ak_register_article(FighterKind kind, void* article, int index);

/* m-ex 0x803D7088: the ItemKind that article `index` of this fighter was registered as. */
ItemKind mu_ak_article_kind(HSD_GObj* fighter_gobj, int index);

/* ---------------------------------------------------------------- shared entry points */

/* dedede.c */
extern MotionState ftDe_MotionStateTable[FTDE_MS_COUNT];

/* ftDe_SpecialN.c */
void ftDe_SpecialN_Enter(HSD_GObj* gobj);
void ftDe_SpecialN_GetMouthPos(HSD_GObj* gobj, Vec3* out);
void ftDe_SpecialN_EatWait_Fall(HSD_GObj* gobj);

/* ftDe_SpecialNCapture.c */
void ftDe_SpecialN_OnVictim(HSD_GObj* victim_gobj, HSD_GObj* dedede_gobj);
void ftDe_CaptureWaitKirby_IASA(HSD_GObj* victim_gobj);
void ftDe_SpecialN_ItemCaptured(Item_GObj* item_gobj, HSD_GObj* dedede_gobj, float facing_dir);
void ftDe_SpecialN_EnterStarSpit(HSD_GObj* victim_gobj, HSD_GObj* dedede_gobj);
void ftDe_SpecialN_SpawnSpitStar(HSD_GObj* gobj, Vec3* pos, Vec3* vel, float lifetime, float decel);

/* ftDe_SpecialS.c */
void ftDe_SpecialS_Enter(HSD_GObj* gobj);
void ftDe_SpecialS_ClearHeldGordo(HSD_GObj* gobj);
Item_GObj* ftDe_SpecialS_SpawnGordo(HSD_GObj* gobj, Vec3* pos, ItemKind kind, float facing_dir);
void ftDe_Gordo_EnterThrown(Item_GObj* item_gobj, Vec3* vel, Vec3* pos_offset);

/* ftDe_SpecialHi.c */
void ftDe_SpecialHi_Enter(HSD_GObj* gobj, float anim_start);

/* ftDe_SpecialLw.c */
void ftDe_SpecialLw_Enter(HSD_GObj* gobj);
void ftDe_SpecialAirLw_Enter(HSD_GObj* gobj);

/* itDe_Articles.c: one ItemLogicTable per article, index = enum ftDe_Article, NULL = no code. */
extern ItemLogicTable* const ftDe_ArticleLogic[ftDe_Article_Count];

#endif
