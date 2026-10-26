/* Practice CPU: a Training partner whose movement, attacks, defense, out of shield options, DI,
 * SDI, techs, getups, ledge options and recovery are set by the player, each with its own odds.
 *
 * One decision model for both engines. It sees a small view of the CPU and its opponent and returns
 * the controller the CPU holds this frame; it never touches game memory. The Source Port fills the
 * view from the Fighter structs (sourceport/game/shim/mu_practice_cpu.c), the Static Recomp from
 * guest memory (port/runtime/hle/practice_cpu.cpp). The model's own state lives in a struct the caller
 * owns, so a savestate that keeps that struct replays the same decisions.
 *
 * Odds are rolled once at each decision point (a new hit, hitstun ending, the CPU becoming free,
 * shieldstun ending, landing on the ledge), never once per frame. Plain C that also compiles as C++.
 * Offline Training only: callers never run it online, while an online match is pending, or during
 * replay playback. */
#ifndef MU_PRACTICE_CPU_H
#define MU_PRACTICE_CPU_H

/* No system includes: the game's code has a C library of its own (as mu_native_pose.h). */
#if defined(_MSC_VER)
typedef unsigned __int8 mu_pc_u8;
typedef unsigned __int32 mu_pc_u32;
typedef signed __int32 mu_pc_s32;
#else
typedef __UINT8_TYPE__ mu_pc_u8;
typedef __UINT32_TYPE__ mu_pc_u32;
typedef __INT32_TYPE__ mu_pc_s32;
#endif

#define MU_PC_CONFIG_VERSION 1u

/* GameCube controller bits (the game's HSD_PAD_* values). */
#define MU_PC_DLEFT 0x0001u
#define MU_PC_DRIGHT 0x0002u
#define MU_PC_DDOWN 0x0004u
#define MU_PC_DUP 0x0008u
#define MU_PC_Z 0x0010u
#define MU_PC_R 0x0020u
#define MU_PC_L 0x0040u
#define MU_PC_A 0x0100u
#define MU_PC_B 0x0200u
#define MU_PC_X 0x0400u
#define MU_PC_Y 0x0800u

/* What the CPU does while nothing calls for a reaction. */
enum {
    MU_PC_MOVE_STAND,
    MU_PC_MOVE_WALK,
    MU_PC_MOVE_DASH_DANCE,
    MU_PC_MOVE_APPROACH,
    MU_PC_MOVE_RUN_AWAY,
    MU_PC_MOVE_SHORT_HOP,
    MU_PC_MOVE_FULL_HOP,
    MU_PC_MOVE_WAVEDASH_IN,
    MU_PC_MOVE_WAVEDASH_OUT,
    MU_PC_MOVE_CROUCH,
    MU_PC_MOVE_COUNT
};

/* Attacks it picks from when the opponent is in range. Aerials are short or full hopped as set. */
enum {
    MU_PC_ATK_JAB,
    MU_PC_ATK_FTILT,
    MU_PC_ATK_UTILT,
    MU_PC_ATK_DTILT,
    MU_PC_ATK_FSMASH,
    MU_PC_ATK_USMASH,
    MU_PC_ATK_DSMASH,
    MU_PC_ATK_DASH_ATTACK,
    MU_PC_ATK_GRAB,
    MU_PC_ATK_BOOST_GRAB,
    MU_PC_ATK_NAIR,
    MU_PC_ATK_FAIR,
    MU_PC_ATK_BAIR,
    MU_PC_ATK_UAIR,
    MU_PC_ATK_DAIR,
    MU_PC_ATK_NEUTRAL_B,
    MU_PC_ATK_SIDE_B,
    MU_PC_ATK_UP_B,
    MU_PC_ATK_DOWN_B,
    MU_PC_ATK_WAVEDASH_FTILT,   /* a chain: wavedash in, then forward tilt */
    MU_PC_ATK_COUNT
};

/* Its answer when the opponent attacks near it. */
enum {
    MU_PC_DEF_NOTHING,
    MU_PC_DEF_SHIELD,
    MU_PC_DEF_SPOT_DODGE,
    MU_PC_DEF_ROLL_IN,
    MU_PC_DEF_ROLL_AWAY,
    MU_PC_DEF_JUMP_AWAY,
    MU_PC_DEF_COUNT
};

/* Out of shield, once shieldstun ends. */
enum {
    MU_PC_OOS_HOLD,
    MU_PC_OOS_DROP,
    MU_PC_OOS_JUMP,
    MU_PC_OOS_NAIR,
    MU_PC_OOS_UP_B,
    MU_PC_OOS_SHINE,      /* down special out of a jump: shine for the space animals */
    MU_PC_OOS_GRAB,
    MU_PC_OOS_WAVEDASH,
    MU_PC_OOS_ROLL,
    MU_PC_OOS_SPOT_DODGE,
    MU_PC_OOS_COUNT
};

/* Directional influence held as hitlag ends. "In" and "out" are toward and away from the opponent. */
enum {
    MU_PC_DI_NONE,
    MU_PC_DI_IN,
    MU_PC_DI_OUT,
    MU_PC_DI_SURVIVAL,    /* up and toward the middle of the stage */
    MU_PC_DI_DOWN_IN,     /* down and toward the opponent (combo DI) */
    MU_PC_DI_DOWN_OUT,
    MU_PC_DI_COUNT
};

enum { MU_PC_TECH_IN_PLACE, MU_PC_TECH_IN, MU_PC_TECH_AWAY, MU_PC_TECH_MISS, MU_PC_TECH_COUNT };

/* After a missed tech. */
enum {
    MU_PC_GETUP_STAND,
    MU_PC_GETUP_ROLL_IN,
    MU_PC_GETUP_ROLL_AWAY,
    MU_PC_GETUP_ATTACK,
    MU_PC_GETUP_WAIT,
    MU_PC_GETUP_COUNT
};

/* From the ledge. */
enum {
    MU_PC_LEDGE_GETUP,
    MU_PC_LEDGE_ROLL,
    MU_PC_LEDGE_JUMP,
    MU_PC_LEDGE_ATTACK,
    MU_PC_LEDGE_DROP_JUMP,
    MU_PC_LEDGE_DASH,      /* drop, jump and air dodge onto the stage */
    MU_PC_LEDGE_COUNT
};

/* As hitstun ends. */
enum {
    MU_PC_AFTER_NOTHING,
    MU_PC_AFTER_JUMP,
    MU_PC_AFTER_AIR_DODGE,
    MU_PC_AFTER_NAIR,
    MU_PC_AFTER_UP_B,
    MU_PC_AFTER_COUNT
};

enum { MU_PC_RECOVER_GAME, MU_PC_RECOVER_HIGH, MU_PC_RECOVER_LOW, MU_PC_RECOVER_MIX, MU_PC_RECOVER_COUNT };

/* Everything the player sets. Odds are relative weights 0..100 within each group; a group whose
 * weights are all 0 never acts. Bump MU_PC_CONFIG_VERSION when the layout changes. */
typedef struct MuPcConfig {
    mu_pc_u8 version;
    mu_pc_u8 enabled;            /* the practice CPU drives the Training CPU */
    mu_pc_u8 move_odds[MU_PC_MOVE_COUNT];
    mu_pc_u8 attack_odds[MU_PC_ATK_COUNT];
    mu_pc_u8 defend_odds[MU_PC_DEF_COUNT];
    mu_pc_u8 oos_odds[MU_PC_OOS_COUNT];
    mu_pc_u8 di_odds[MU_PC_DI_COUNT];
    mu_pc_u8 tech_odds[MU_PC_TECH_COUNT];
    mu_pc_u8 getup_odds[MU_PC_GETUP_COUNT];
    mu_pc_u8 ledge_odds[MU_PC_LEDGE_COUNT];
    mu_pc_u8 after_odds[MU_PC_AFTER_COUNT];
    mu_pc_u8 aggression;         /* 0..100: chance to attack (rather than move) when in range */
    mu_pc_u8 attack_range;       /* game units from the opponent where attacks start */
    mu_pc_u8 reaction_frames;    /* frames before it answers an attack (human-like: 12 to 20) */
    mu_pc_u8 full_hop_aerials;   /* 0..100: chance an aerial is full hopped instead of short hopped */
    mu_pc_u8 fast_fall;          /* 0..100 */
    mu_pc_u8 l_cancel;           /* 0..100 */
    mu_pc_u8 sdi_amount;         /* 0 none, 1 light, 2 medium, 3 heavy */
    mu_pc_u8 recovery;           /* MU_PC_RECOVER_* */
    mu_pc_u8 freeze;             /* 1: the CPU stands still and only does the hit reactions (DI, tech...) */
    mu_pc_u8 ledge_delay;        /* most frames it waits on the ledge before its option (random below it) */
    mu_pc_u8 tools;              /* MU_PC_TOOL_*: Training tools the Practice page turns on (host side) */
    mu_pc_u8 preset;             /* 1 + the preset last applied; 0 once anything was changed by hand */
    mu_pc_u8 reserved[4];
} MuPcConfig;

/* Training tools (Source Port's lab, shim/mu_lab.c, and the host overlay). */
#define MU_PC_TOOL_DPAD 0x01u          /* D-pad: right save state, left load, down record, up play back */
#define MU_PC_TOOL_HITBOXES 0x02u      /* hitboxes and hurtboxes over the model */
#define MU_PC_TOOL_HITBOXES_ONLY 0x04u /* ...or instead of it */
#define MU_PC_TOOL_OVERLAY 0x08u       /* inputs, action and frame advantage */
#define MU_PC_TOOL_LOOP 0x10u          /* a played back recording repeats */
#define MU_PC_TOOL_FRAME_ADVANCE 0x20u /* L+R+Z freezes Training, Z steps a frame (host side) */

/* One fighter as the model needs it. Positions are game units; facing is +1 right, -1 left. */
typedef struct MuPcFighter {
    float x, y, vx, vy, facing, percent, shield;
    mu_pc_s32 motion;          /* the common action state (ftCommon motion id); character specials are >= 341 */
    mu_pc_s32 kind;            /* character */
    mu_pc_u8 airborne;
    mu_pc_u8 hitlag;          /* in hitlag this frame */
    mu_pc_u8 hitstun;         /* in hitstun this frame */
    mu_pc_u8 can_act;         /* an interrupt is allowed or it is in a free state */
    mu_pc_u8 jumps_left;
    mu_pc_u8 present;
    mu_pc_u8 pad[2];
} MuPcFighter;

typedef struct MuPcView {
    MuPcFighter cpu, opp;
    float ledge_x;           /* x of the main stage's right ledge (the stage is symmetric) */
    float ledge_y;           /* y of the stage's top at the ledge */
    mu_pc_s32 land_frames;     /* frames until the CPU meets the floor on its current path, -1 not soon */
    mu_pc_u32 frame;          /* match frame */
} MuPcView;

typedef struct MuPcInput {
    float lx, ly, cx, cy, l, r;
    mu_pc_u32 buttons;
} MuPcInput;

/* The model's memory. Zero it to start; mu_pc_reset after a savestate load that did not keep it. */
typedef struct MuPcState {
    mu_pc_u32 rng;
    mu_pc_s32 plan, plan_t, plan_dir, plan_arg, plan_len;
    mu_pc_s32 was_hitlag, was_hitstun, was_down, was_ledge, was_shieldstun, was_free;
    mu_pc_s32 di, sdi_left, tech_choice, tech_pressed, after_choice;
    mu_pc_s32 ledge_wait, opp_attack_seen, opp_attack_t, opp_motion;
    mu_pc_s32 idle_wait, last_logged;
    mu_pc_s32 reserved[8];
} MuPcState;

void mu_pc_reset(MuPcState* s, mu_pc_u32 seed);
/* The CPU's controller for this frame. Returns 1 when it set `out`, 0 when the game's own CPU should
 * act (practice CPU off, the CPU is dead or entering, or recovery is left to the game). */
int mu_pc_frame(const MuPcConfig* c, MuPcState* s, const MuPcView* v, MuPcInput* out);
/* The defaults a fresh setting starts from, and the built-in presets (0 = default). */
void mu_pc_defaults(MuPcConfig* c);
int mu_pc_preset_count(void);
const char* mu_pc_preset_name(int index);
void mu_pc_preset(int index, MuPcConfig* c);
/* The ledge of a stage by its internal stage id (the game's Gr_Kind), for the view. */
void mu_pc_stage_ledge(int internal_stage_id, float* ledge_x, float* ledge_y);
/* Short name of the plan running (for the overlay and test logs). */
const char* mu_pc_plan_name(int plan);

#endif
