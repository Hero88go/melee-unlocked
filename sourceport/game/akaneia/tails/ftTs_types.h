/* Akaneia's Tails, native: data layouts.
 *
 * Everything here was read off the PowerPC in PlTs.dat (m-ex ftFunction + move_logic, and the one
 * article's itFunction). Console offsets are kept in the comments so each field can be checked
 * against the original. Disc data (the attribute block, the costume colour table, the article's
 * attributes) is big-endian DISC_STRUCT; the fighter and motion variables are host memory. */
#ifndef MU_AK_TAILS_TYPES_H
#define MU_AK_TAILS_TYPES_H

#include <Runtime/platform.h>

#include <dolphin/gx/GXStruct.h>
#include <dolphin/mtx.h>
#include <melee/ft/forward.h>
#include <melee/ft/kinds/ftCommon/types.h>
#include <melee/ft/types.h>
#include <sysdolphin/baselib/forward.h>
#include <sysdolphin/baselib/psstructs.h>

#include <mu_disc.h>

/* ------------------------------------------------------------------------------------------------
 * Motion states (m-ex move_logic, numbered from ftCo_MS_Count = 341)
 * --------------------------------------------------------------------------------------------- */
enum ftTails_MotionState {
    ftTs_MS_SpecialN = 341,       /* 0x155 Energy shot, ground */
    ftTs_MS_SpecialAirN,          /* 0x156 Energy shot, air */
    ftTs_MS_SpecialHiStart,       /* 0x157 Flight start, ground */
    ftTs_MS_SpecialAirHiStart,    /* 0x158 Flight start, air */
    ftTs_MS_SpecialHiLoop,        /* 0x159 Flying (fuel drains, B flaps) */
    ftTs_MS_SpecialHiExhaust,     /* 0x15A Out of fuel, tired fall */
    ftTs_MS_SpecialHiCancel,      /* 0x15B Never entered by the code; kept for the table */
    ftTs_MS_SpecialLwStart,       /* 0x15C Spin dash start, ground */
    ftTs_MS_SpecialLwEnd,         /* 0x15D Spin dash end, ground */
    ftTs_MS_SpecialAirLwStart,    /* 0x15E Spin dash start, air */
    ftTs_MS_SpecialAirLwEnd,      /* 0x15F Spin dash end, air */
    ftTs_MS_SpecialLwCharge,      /* 0x160 Revving (press B to charge) */
    ftTs_MS_SpecialLwRun,         /* 0x161 Rolling */
    ftTs_MS_SpecialLwRunTurn,     /* 0x162 Rolling turnaround */
    ftTs_MS_SpecialLwRunJump,     /* 0x163 Jump out of the roll */
    ftTs_MS_SpecialLwDive,        /* 0x164 Aerial release: dive */
    ftTs_MS_SpecialLwRunBrake,    /* 0x165 Roll timer ran out */
    ftTs_MS_SpecialLwStopWallR,   /* 0x166 Bonk into a wall facing right */
    ftTs_MS_SpecialLwStopWallL,   /* 0x167 Bonk into a wall facing left */
    ftTs_MS_SpecialSStart,        /* 0x168 Tail spin start, ground */
    ftTs_MS_SpecialSEnd,          /* 0x169 Tail spin end, ground */
    ftTs_MS_SpecialSLoop0,        /* 0x16A..0x16E five spin loops, one animation each */
    ftTs_MS_SpecialSLoop1,
    ftTs_MS_SpecialSLoop2,
    ftTs_MS_SpecialSLoop3,
    ftTs_MS_SpecialSLoop4,
    ftTs_MS_SpecialAirSStart,     /* 0x16F */
    ftTs_MS_SpecialAirSEnd,       /* 0x170 */
    ftTs_MS_SpecialAirSLoop0,     /* 0x171..0x175 */
    ftTs_MS_SpecialAirSLoop1,
    ftTs_MS_SpecialAirSLoop2,
    ftTs_MS_SpecialAirSLoop3,
    ftTs_MS_SpecialAirSLoop4,
    ftTs_MS_End,
};
#define ftTs_MS_SelfCount (ftTs_MS_End - ftTs_MS_SpecialN) /* 33 */
#define ftTs_SpecialS_LoopCount 5

/* Motion flag words of the table. Same bits the vanilla specials use (Fox's SpecialN word is the
 * same 0x00340111): model, item visibility, throw exception, physics update, freeze. */
#define ftTs_MF_Special 0x00340110u /* SkipModel|SkipThrowException|SkipItemVis|UnkUpdatePhys|FreezeState */
#define ftTs_MF_SpecialN (ftTs_MF_Special | 0x1u)  /* | KeepFastFall */
#define ftTs_MF_SpecialS (ftTs_MF_Special | 0x2u)  /* | KeepGfx */
#define ftTs_MF_SpecialHi (ftTs_MF_Special | 0x3u) /* | KeepFastFall | KeepGfx */
#define ftTs_MF_SpecialLw (ftTs_MF_Special | 0x4u) /* | KeepColAnimHitStatus */

/* Flags the Trans (ground/air swap keeping the frame) functions pass to ChangeMotionState. */
#define ftTs_MF_TransN 0x4008u     /* UpdateCmd | SkipHit */
#define ftTs_MF_TransSStart 0x4002u /* UpdateCmd | KeepGfx */
#define ftTs_MF_TransSEnd 0x4008u  /* UpdateCmd | SkipHit (ground end) */
#define ftTs_MF_TransAirSEnd 0x4000u /* UpdateCmd */
#define ftTs_MF_TransSLoop 0x4006u /* UpdateCmd | KeepColAnimHitStatus | KeepGfx */

/* ------------------------------------------------------------------------------------------------
 * Effect and sound ids. These are m-ex ids: the model effects (5000+) and particle generators
 * (6000+) come from Tails's own effect file (EfTsData.dat) and the sounds from his sound bank, so
 * the integration layer must route them (see NOTES.md).
 * --------------------------------------------------------------------------------------------- */
enum {
    ftTs_Ef_BallJump = 5000,      /* 0x1388 ball effect on RunJump / Dive */
    ftTs_Ef_BallRoll = 5002,      /* 0x138A ball effect on Charge / Run */
    ftTs_Ef_BallUpSmash = 5003,   /* 0x138B AttackHi4 */
    ftTs_Ef_BallAir = 5004,       /* 0x138C AttackAirN / AttackDash */
    ftTs_Ef_HeliSpecialN = 5005,  /* 0x138D helicopter tails during the shot */
    ftTs_Ef_HeliFlight = 5006,    /* 0x138E helicopter tails during run / flight */
    ftTs_Pt_RollDust = 6001,      /* 0x1771 particles while rolling */
    ftTs_Pt_ChargeLevel0 = 6004,  /* 0x1774 + charge level (0..2) */
};
enum {
    ftTs_Se_Jump = 5001,          /* 0x1389 RunJump */
    ftTs_Se_Rev = 5007,           /* 0x138F each rev of the charge */
};

/* Fighter bone (part) indices used directly by the code. */
enum {
    ftTs_Part_Ball = 1,           /* ball effects, charge particles */
    ftTs_Part_Hand = 3,           /* AttackHi4 ball effect */
    ftTs_Part_TailBase = 47,      /* 0x2F tail ball joint, trail root */
    ftTs_Part_TailA = 51,         /* 0x33 first tail tip (trail) */
    ftTs_Part_TailB = 55,         /* 0x37 second tail tip (trail) */
    ftTs_Part_AirBall = 68,       /* 0x44 AttackAirN / AttackDash ball effect */
    ftTs_Part_BallPivot = 69,     /* 0x45 pivot the tail ball is placed at */
    ftTs_Part_ShotSource = 52,    /* 0x34, looked up through ftParts_GetBoneIndex */
};

/* ------------------------------------------------------------------------------------------------
 * The attribute block (ftData->ext_attr, 0x1A8 bytes). Names describe what the code does with
 * each value; the few the code never reads are left as xNN.
 * --------------------------------------------------------------------------------------------- */
typedef struct ftTails_DatAttrs {
    /* 0x000 */ float airn_x_decel;
    /* 0x004 */ float airn_gravity;
    /* 0x008 */ float airn_terminal_vel;
    /* 0x00C */ float airn_shot_vel_y;       ///< air shot: self_vel.y set when the shot spawns
    /* 0x010 */ float n_enter_vel_y_mul;     ///< self_vel.y scaled on entering the shot
    /* 0x014 */ ftCollisionBox_BE airn_ecb;
    /* 0x02C */ int hi_landing_lag;
    /* 0x030 */ int hi_ascent_frames;        ///< flap: frames of lift after each press
    /* 0x034 */ float hi_turn_step;          ///< radians per frame the model turns
    /* 0x038 */ int hi_min_fuel;
    /* 0x03C */ int hi_max_fuel;             ///< refilled on landing and respawn
    /* 0x040 */ int hi_fuel_per_frame;
    /* 0x044 */ int hi_fuel_on_hit;          ///< fuel left after being hit while flying
    /* 0x048 */ int x48;
    /* 0x04C */ float hi_fly_anim_rate;
    /* 0x050 */ float hi_hold_lift;          ///< vel.y added while B is held past the ascent
    /* 0x054 */ float hi_max_vel_y;
    /* 0x058 */ float hi_ascent_lift;        ///< vel.y added per ascent frame
    /* 0x05C */ float hi_flap_boost;         ///< vel.y added on a B press
    /* 0x060 */ int hi_fuel_per_flap;
    /* 0x064 */ float hi_gravity;
    /* 0x068 */ float hi_terminal_vel;
    /* 0x06C */ float hi_drift_accel;
    /* 0x070 */ float hi_drift_max;
    /* 0x074 */ float exhaust_gravity;
    /* 0x078 */ float exhaust_terminal_vel;
    /* 0x07C */ float exhaust_drift_accel;
    /* 0x080 */ float exhaust_drift_max;
    /* 0x084 */ float hi_cancel_fall_mobility;
    /* 0x088 */ float exhaust_frame_div;     ///< exhaust anim start = frame / div * mul
    /* 0x08C */ float exhaust_frame_mul;
    /* 0x090 */ int s_loops;                 ///< spin loops before the end state
    /* 0x094 */ float s_loop_frame_wrap;     ///< frame carried into the next loop, modulo this
    /* 0x098 */ float s_loop_blend;
    /* 0x09C */ float s_loop_anim_rate;
    /* 0x0A0 */ ftCollisionBox_BE s_ecb;
    /* 0x0B8 */ float s_start_vel_x;
    /* 0x0BC */ float s_loop_base_speed;
    /* 0x0C0 */ float s_loop_stick_speed;
    /* 0x0C4 */ float s_loop_accel;
    /* 0x0C8 */ float s_loop_friction;       ///< applied while the stick is centred
    /* 0x0CC */ float s_end_friction;
    /* 0x0D0 */ float airs_start_vel_x_mul;
    /* 0x0D4 */ float airs_drift_accel;
    /* 0x0D8 */ float airs_drift_max;
    /* 0x0DC */ float airs_start_vel_y;
    /* 0x0E0 */ float airs_gravity;
    /* 0x0E4 */ float airs_terminal_vel;
    /* 0x0E8 */ float airs_end_gravity;
    /* 0x0EC */ float airs_end_drift_max;
    /* 0x0F0 */ float airs_end_boost_vel_y;  ///< once per airtime, only if the whole spin stayed airborne
    /* 0x0F4 */ float airs_end_drift_accel;
    /* 0x0F8 */ float xF8;
    /* 0x0FC */ float airs_end_fall_mobility;
    /* 0x100 */ int s_landing_lag;
    /* 0x104 */ int lw_idle_timeout;         ///< charge ends if B is not pressed for this long
    /* 0x108 */ int lw_level_cooldown;       ///< frames between charge level-ups
    /* 0x10C */ int lw_first_cooldown;
    /* 0x110 */ int lw_max_level;
    /* 0x114 */ int x114;
    /* 0x118 */ float lw_charge_gravity;
    /* 0x11C */ float lw_charge_x_decel;
    /* 0x120 */ float lw_charge_terminal_vel;
    /* 0x124 */ int lw_auto_release;         ///< charge releases by itself after this long
    /* 0x128 */ int lw_run_frames;
    /* 0x12C */ float lw_run_accel;
    /* 0x130 */ float lw_run_speed_lv2;
    /* 0x134 */ float lw_run_speed_lv1;
    /* 0x138 */ float lw_run_speed_lv0;
    /* 0x13C */ float lw_turn_accel;
    /* 0x140 */ float lw_turn_exit_speed;
    /* 0x144 */ float x144;
    /* 0x148 */ float lw_jump_vel_y;
    /* 0x14C */ int lw_jump_aerial_lock;
    /* 0x150 */ float lw_jump_drift;         ///< also the special-fall mobility after a dive
    /* 0x154 */ float lw_jump_max_vel_x;
    /* 0x158 */ float lw_jump_gravity;
    /* 0x15C */ float lw_jump_terminal_vel;
    /* 0x160 */ float lw_brake_friction;
    /* 0x164 */ float lw_dive_vel_x;
    /* 0x168 */ float lw_dive_vel_y;         ///< 0 keeps the current vertical speed
    /* 0x16C */ int lw_dive_frames;
    /* 0x170 */ float lw_stopwall_anim_rate;
    /* 0x174 */ float lw_jump_landing_lag;   ///< float on the disc; 0 = normal landing
    /* 0x178 */ float lw_run_damage_min;
    /* 0x17C */ float lw_run_damage_max;
    /* 0x180 */ float lw_jump_damage_min;
    /* 0x184 */ float lw_jump_damage_max;
    /* 0x188 */ ftCollisionBox_BE lw_air_ecb;
    /* 0x1A0 */ float ball_offset_y;         ///< tail ball matrix translation y
    /* 0x1A4 */ float ball_tilt;             ///< tail ball rotation about x (radians)
} DISC_STRUCT ftTails_DatAttrs;

/* The costume's "PlyTailsColor" table (0x1C bytes), or MetalColor when metal. */
typedef struct ftTails_Colors {
    /* 0x00 */ GXColor x0;
    /* 0x04 */ GXColor trail_inner;   ///< trail vertex at the tail base
    /* 0x08 */ GXColor trail_outer;   ///< trail vertex at the tail tip
    /* 0x0C */ GXColor ball_konst;    ///< TObj TEV konst of the ball effects
    /* 0x10 */ GXColor ball_tev0;     ///< TObj TEV tev0 of the ball effects
    /* 0x14 */ GXColor particle_prim;
    /* 0x18 */ GXColor particle_env;
} ftTails_Colors;

/* One sample of the spin trail: the two edge points of a tail ribbon (console 0x18 bytes). */
typedef struct ftTails_TrailPoint {
    /* 0x00 */ Vec3 base;
    /* 0x0C */ Vec3 tip;
} ftTails_TrailPoint;
#define ftTs_TrailMax 8 /* console buffers are 0xC0 bytes */

/* Hooked onto the particle generators Tails spawns (console fp+0x2290): the particle system calls
 * hookCreate for every particle, and it recolours the particle with the costume's colours. */
typedef struct ftTails_ParticleHook {
    /* 0x2290 */ HSD_PSUserFunc funcs;         ///< hookCreate = ftTs_OnSpawnParticle
    /* 0x229C */ const ftTails_Colors* colors;
    /* 0x22A0 */ Fighter* fp;
} ftTails_ParticleHook;

/* Fighter variables (console fp+0x222C). */
typedef struct ftTails_FighterVars {
    /* 0x222C */ HSD_GObj* shot_gobj;      ///< live SpecialN projectile (only one at a time)
    /* 0x2230 */ HSD_GObj* trail_gobj;     ///< GX-only gobj that draws the spin trail
    /* 0x2234 */ float ball_angle;         ///< tail ball roll, eased toward the slope/velocity
    /* 0x2238 */ Mtx ball_mtx;             ///< base tilt of the tail ball
    /* 0x2278 */ Vec3 trail_last_pos;
    /* 0x2284 */ int trail_count;
    /* 0x2288 */ ftTails_TrailPoint* trail_a;
    /* 0x228C */ ftTails_TrailPoint* trail_b;
    /* 0x2290 */ ftTails_ParticleHook particle_hook;
    /* 0x22A4 */ int airs_boost_ready;     ///< the air spin's upward boost, once per airtime
    /* 0x22A8 */ int fuel;                 ///< flight fuel
} ftTails_FighterVars;

/* Motion variables (console fp+0x2340), one view per special. */
typedef struct ftTails_SpecialHiVars {
    /* 0x2340 */ float target_rot;         ///< +-pi/2: the model's facing turn target
    /* 0x2344 */ float rot;
    /* 0x2348 */ int ascent;               ///< frames of lift left
} ftTails_SpecialHiVars;

typedef struct ftTails_SpecialSVars {
    /* 0x2340 */ int next_loop;            ///< 0..4, the next loop animation
    /* 0x2344 */ int cur_loop;
    /* 0x2348 */ int loops_done;
    /* 0x234C */ u8 all_airborne;          ///< cleared once the spin touches ground
} ftTails_SpecialSVars;

typedef struct ftTails_SpecialLwVars {
    /* 0x2340 */ s8 level;                 ///< -1 until the first level, then 0..lw_max_level
    /* 0x2341 */ u8 rolled_off;            ///< RunJump entered by rolling off an edge
    /* 0x2342 */ u8 has_air_jump;
    /* 0x2344 */ s16 idle_timer;
    /* 0x2346 */ s16 cooldown;
    /* 0x2348 */ s16 run_timer;
    /* 0x234A */ s16 dive_timer;
    /* 0x234C */ float run_speed;
    /* 0x2350 */ s16 aerial_lock;
    /* 0x2354 */ s16 dust_timer;
    /* 0x2356 */ s16 auto_release;
} ftTails_SpecialLwVars;

/* ------------------------------------------------------------------------------------------------
 * The article (SpecialN projectile)
 * --------------------------------------------------------------------------------------------- */
typedef struct itTails_ShotAttrs {
    /* 0x00 */ int lifetime;
    /* 0x04 */ int die_lifetime;
    /* 0x08 */ float speed;
    /* 0x0C */ float shield_bounce_vel_mul;
    /* 0x10 */ int max_shield_bounces;
} DISC_STRUCT itTails_ShotAttrs;

/* Item variables (console ip+0xDD4). */
typedef struct itTails_ShotVars {
    /* 0xDD4 */ int life;
    /* 0xDD8 */ HSD_GObj* owner;
    /* 0xDDC */ int bounces;
} itTails_ShotVars;

enum itTails_ShotState {
    itTs_Shot_MS_Ground,
    itTs_Shot_MS_Air,
    itTs_Shot_MS_Die,
};

/* ------------------------------------------------------------------------------------------------
 * The m-ex CPU attack tables (same record as the PlCo.dat per-kind tables; disc order)
 * --------------------------------------------------------------------------------------------- */
typedef struct ftTails_CpuAttack {
    /* 0x00 */ s32 cmd;
    /* 0x04 */ s32 x04;
    /* 0x08 */ f32 x08;
    /* 0x0C */ f32 x0C;
    /* 0x10 */ f32 x10;
    /* 0x14 */ f32 x14;
    /* 0x18 */ f32 weight;
    /* 0x1C */ s32 x1C;
    /* 0x20 */ s32 min_level;
} DISC_STRUCT ftTails_CpuAttack;

typedef struct ftTails_CpuData {
    void (*decide)(Fighter* fp);        ///< CPU_Tails
    float distance;                     ///< -> Fighter_804D64FC->x20
    const ftTails_CpuAttack* ground;    ///< -> x4
    const ftTails_CpuAttack* air;       ///< -> x8
    const ftTails_CpuAttack* ranged;    ///< -> xC
    const ftTails_CpuAttack* smash;     ///< -> x10
    const ftTails_CpuAttack* special;   ///< -> x14
    const ftTails_CpuAttack* weapon;    ///< -> x18
    const ftTails_CpuAttack* edge;      ///< -> x1C
} ftTails_CpuData;

/* ------------------------------------------------------------------------------------------------
 * Accessors
 * --------------------------------------------------------------------------------------------- */
static inline ftTails_DatAttrs* ftTs_Attrs(Fighter* fp)
{
    return (ftTails_DatAttrs*) fp->dat_attrs;
}

static inline ftTails_FighterVars* ftTs_Vars(Fighter* fp)
{
    return (ftTails_FighterVars*) &fp->u;
}

static inline ftTails_SpecialHiVars* ftTs_HiVars(Fighter* fp)
{
    return (ftTails_SpecialHiVars*) &fp->mv;
}

static inline ftTails_SpecialSVars* ftTs_SVars(Fighter* fp)
{
    return (ftTails_SpecialSVars*) &fp->mv;
}

static inline ftTails_SpecialLwVars* ftTs_LwVars(Fighter* fp)
{
    return (ftTails_SpecialLwVars*) &fp->mv;
}

/* An int attribute used where the game wants a float frame count. */
static inline float ftTs_Frames(int frames)
{
    return (float) frames;
}

#endif
