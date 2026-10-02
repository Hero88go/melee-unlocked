/* 20XX Hack Pack CPU AI ("20XX CPUs"), rewritten as native C.
 *
 * The Hack Pack loads one block of code at match start and enters it where the game stores a CPU
 * fighter's buttons for the frame (Fighter_procInput, console address 8006B008). That code looks at
 * the fighter, the stage's two ledges and one opponent, then rewrites the CPU's sticks, C-stick,
 * trigger and buttons for the frame: wavedashes, shine, short hop lasers, jump-cancelled grabs and
 * up smashes, out of shield options, ledge options, fast falls, recovery with a chosen Fire Fox
 * angle, crouch-cancel style C-stick down at the end of hitlag.
 *
 * This file is that behaviour written by hand against the native Fighter struct, one block per
 * block of the original and in the same order, so the conditions, thresholds, frame numbers and
 * the order of the random number calls are the original's. run-source/rel09-20xx-ai/ANALYSIS.md
 * describes every routine; the L_xxxxx labels are the original's offsets, which is what
 * run-source/rel09-20xx-ai/verify.py checks this file against.
 *
 * The Hack Pack's debug menu toggles that the code reads are fixed here at the Hack Pack's default
 * values (the hp_* variables below).
 *
 * Off unless the player turns "20XX CPUs" on. Never in an online match. In replay playback the
 * host passes the options the replay was recorded with, so playback follows the recording. */
#include <dolphin/os.h>
#include <melee/ft/fighter.h>
#include <melee/ft/forward.h>
#include <melee/ft/types.h>
#include <melee/ft/kinds/ftCommon/forward.h>
#include <melee/gm/forward.h>
#include <melee/gm/gm_1A3F.h>
#include <melee/gm/gmvs.h>
#include <melee/gm/types.h>
#include <melee/gr/forward.h>
#include <melee/gr/ground.h>
#include <melee/gr/types.h>
#include <melee/mp/mplib.h>
#include <melee/mp/types.h>
#include <melee/pl/player.h>
#include <sysdolphin/baselib/controller.h>
#include <sysdolphin/baselib/gobj.h>
#include <sysdolphin/baselib/random.h>

#include "math_native.h"
#include "mu_native.h"

int mu_online_active(void);
int mu_online_pending(void);
int mu_replay_abi_active(void);
char* getenv(const char* name);
float atan2f(float y, float x);
float sinf(float x);
float cosf(float x);

/* The Hack Pack's debug menu values this code reads, at their defaults. The console address of
 * each is in the comment; ANALYSIS.md lists what every value selects. */
static int hp_cpu_mode = 0;              /* 80003374[player]: 0 default, 1 hold shield, 2 shield then act, 3 stand */
static int hp_allstar_cpu_kind = 4;      /* 803FAED0: CPU type given to All-Star opponents */
static int hp_sub_fighter_cpu_kind = 6;  /* 803FBAB8[player]: CPU type of a secondary fighter */
static int hp_oos_option = 0;            /* 803FA320: 0 picks an out of shield option at random */
static int hp_aerial_option = 0;         /* 803FA324: 0 picks an aerial at random */
static int hp_defense_gate = 100;        /* 803FA330: percent of frames the defensive branch may run */
static int hp_attack_gate = 100;         /* 803FA334: percent of frames the attack branch may run */
static int hp_spotdodge = 15;            /* 803FAEBC: spot dodge after a tech or getup, percent */
static int hp_shield_after_tech = 10;    /* 803FAEC0: shield after a tech or getup, percent */
static int hp_shield = 10;               /* 803FAEC4: shield when the opponent is close, percent */
static int hp_shine_after_tech = 10;     /* 803FAF5C: Fox and Falco shine after a tech or getup, percent */
static int hp_shield_frames = 0;         /* 803FAF74: frames to keep shielding after the shield is hit */
static int hp_shield_frames_random = 0;  /* 803FAF78: nonzero picks 0..frames at random instead */

/* What the original kept in two spare bytes of the fighter's object: the current plan ("act") and
 * a countdown some plans use. Kept per player here; it is ordinary game state. */
typedef struct AiState {
    HSD_GObj* owner;
    unsigned char timer;
    unsigned char act;
} AiState;
static AiState ai_states[6][2];

/* The frame's working values (the original kept them in registers for the whole pass). */
static Fighter* ai_fp;        /* the CPU fighter */
static Fighter* ai_opp;       /* the opponent it reacts to, or NULL */
static AiState* ai_st;
static double ai_dx, ai_dy;   /* own position minus the opponent's */
static float ai_lx, ai_ly;    /* left ledge */
static float ai_rx, ai_ry;    /* right ledge */
static unsigned int ai_btn;   /* buttons this frame will hold */

#define FP ai_fp
#define OPP ai_opp
#define M ((int) ai_fp->motion_id)
#define K ((int) ai_fp->kind)
#define OM ((int) ai_opp->motion_id)
#define SX (ai_fp->input.lstick[0].x)
#define SY (ai_fp->input.lstick[0].y)
#define CX (ai_fp->input.cstick[0].x)
#define CY (ai_fp->input.cstick[0].y)
#define FACING (ai_fp->facing_dir)
#define FRAME (ai_fp->cur_anim_frame)
#define ACT (ai_st->act)
#define TIMER (ai_st->timer)

/* The debug menu values, read where the original called a small function to load each. */
#define OPT_SPOTDODGE() hp_spotdodge
#define OPT_SHIELD_AFTER_TECH() hp_shield_after_tech
#define OPT_SHINE_AFTER_TECH() hp_shine_after_tech
#define OPT_SHIELD() hp_shield
#define OPT_ATTACK_GATE() hp_attack_gate
#define OPT_DEFENSE_GATE() hp_defense_gate

/* ---- log lines for hidden test runs (MELEE_TRACE_20XX_AI) ---- */

static const char* const act_names[0x26] = {
    "idle", "out of hitstun", "wavedash forward", "wavedash back", "waveland in place",
    "short hop laser", "wavedash left", "wavedash right", "dash", "jump-cancel grab",
    "jump then aerial", "jump out of shield", "hold shield", "jump-cancel down special",
    "ledge jump airdodge", "platform drop", "shield hit, out of shield option",
    "ledge: Sheik up special", "ledge: jump, up special", "ledge: Falco drop, laser",
    "ledge: Marth drop, jump, attack", "ledge: jump, side special", "edge: turnaround shine stall",
    "recovery: Fire Fox angle", "jump-cancel up smash", "recovery: side special",
    "roll out of shield", "full hop", "ledge: Falcon and Ganondorf drop, jump", "ledge: second jump, up special",
    "ledge: drop, jump, waveland", "Peach float", "up special out of shield",
    "ledge: Peach drop, up special", "ledge: drop, jump, aerial", "edge: Sheik needle stall",
    "after tech or getup", "edge: jump, airdodge back to the ledge",
};

static int trace_on(void)
{
    static int on = -1;
    if (on < 0) {
        const char* v = getenv("MELEE_TRACE_20XX_AI");
        on = v != NULL && *v != '\0' && *v != '0';
    }
    return on;
}

/* One line the first time each plan starts for each player. */
static void trace_act(int act)
{
    static unsigned long long seen[6];
    const int p = FP->player_idx < 6 ? FP->player_idx : 0;
    if (!trace_on() || act <= 0 || act >= 0x26 || (seen[p] >> act) & 1) {
        return;
    }
    seen[p] |= 1ull << act;
    OSReport("[20xx-ai] p%d first %s (act %02X) frame %u kind %d motion %d\n", p + 1, act_names[act], act,
             gm_GetFrameCount(), K, M);
}

/* One line the first time each named input goes out for each player. */
enum { EV_ACTIVE, EV_AIRDODGE, EV_SHINE, EV_UPB, EV_GRAB, EV_ASDI_DOWN, EV_FASTFALL, EV_COUNT };
static void trace_event(int ev)
{
    static const char* const names[EV_COUNT] = {
        "active", "wavedash airdodge", "down special", "up special", "grab", "C-stick down in hitlag", "fast fall",
    };
    static unsigned int seen[6];
    const int p = FP->player_idx < 6 ? FP->player_idx : 0;
    if (!trace_on() || (seen[p] >> ev) & 1) {
        return;
    }
    seen[p] |= 1u << ev;
    OSReport("[20xx-ai] p%d first %s frame %u kind %d level %d motion %d\n", p + 1, names[ev], gm_GetFrameCount(), K,
             FP->cpu.level, M);
}

/* ---- small readers ---- */

static unsigned int fbits(float v)
{
    unsigned int b;
    __builtin_memcpy(&b, &v, sizeof b);
    return b;
}

/* The top half of a float's bits: the original compared it to tell "frame is exactly 2.0" and
 * the like with an integer compare. */
static int fhi(float v)
{
    return (int) (fbits(v) >> 16);
}

/* The first word of the motion's own variables, whatever the motion keeps there. */
static unsigned int mv_word0(void)
{
    unsigned int b;
    __builtin_memcpy(&b, &FP->mv, sizeof b);
    return b;
}

static float stage_center(void)
{
    return stage_info.cam_info.cam_x_offset;
}

/* ---- inputs (blob 03960 to 03D8C) ---- */

static float facing_mul(float v)
{
    return v * FACING;
}

static void btn(unsigned int v) { ai_btn = v; }
static void btn_none(void) { ai_btn = 0; }
static void btn_L(void) { ai_btn = HSD_PAD_L; }
static void btn_LA(void)
{
    ai_btn = HSD_PAD_L | HSD_PAD_A;
    trace_event(EV_GRAB);
}
static void btn_X(void) { ai_btn = HSD_PAD_X; }
static void btn_A(void) { ai_btn = HSD_PAD_A; }
static void btn_B(void) { ai_btn = HSD_PAD_B; }

static void special_vertical(float y)
{
    SY = y;
    CX = 0.0f;
    CY = 0.0f;
    SX = 0.0f;
    ai_btn = HSD_PAD_B;
}
static void up_B(void)
{
    special_vertical(1.0f);
    trace_event(EV_UPB);
}
static void down_B(void)
{
    special_vertical(-1.0f);
    trace_event(EV_SHINE);
}

static void stick_left(void) { SY = 0.0f; SX = -1.0f; }
static void stick_right(void) { SY = 0.0f; SX = 1.0f; }
static void stick_up(void) { SX = 0.0f; SY = 1.0f; }
static void stick_down(void) { SX = 0.0f; SY = -1.0f; }
static void stick_neutral(void) { SX = 0.0f; SY = 0.0f; }
static void stick_forward(void) { SX = FACING; SY = 0.0f; }
static void stick_back(void) { SX = -FACING; SY = 0.0f; }
static void cstick_neutral(void) { CX = 0.0f; CY = 0.0f; }
static void cstick_up(void) { CX = 0.0f; CY = 1.0f; }
static void cstick_right(void) { CX = 1.0f; CY = 0.0f; }
static void cstick_left(void) { CX = -1.0f; CY = 0.0f; }
static void cstick_down(void) { CX = 0.0f; CY = -1.0f; }
static void trigger_none(void) { FP->input.triggers[0] = 0.0f; }
static void inputs_none(void)
{
    SX = 0.0f;
    SY = 0.0f;
    CX = 0.0f;
    CY = 0.0f;
    FP->input.triggers[0] = 0.0f;
    ai_btn = 0;
}

/* ---- random numbers: the game's own generator, as the original used ---- */

static int rand_n(int n) { return HSD_Randi(n); }
static int rand100(void) { return HSD_Randi(100); }

/* ---- the plan ---- */

static void set_act(int act)
{
    ACT = (unsigned char) act;
    trace_act(act);
}

/* Hold shield: the countdown is how many frames it may keep holding with the opponent near. */
static void set_act_shield(void)
{
    ACT = 0x0C;
    TIMER = (unsigned char) rand_n(0x78);
    trace_act(0x0C);
}

static void clear_act(void)
{
    TIMER = 0;
    ACT = 0;
}

/* ---- questions about the fighter, the opponent and the stage (blob 038E0 to 050B0) ---- */

/* What the fighter stands on: 1 a platform it can drop through, 2 a floor with a ledge, 3 both. */
static int floor_type(void)
{
    const int line = FP->coll_data.floor.index;
    if (line < 0) {
        return 0;   /* the original only asks on the ground; no line would have stopped the game */
    }
    return (int) ((mpLineGetFlags(line) >> 8) & 3);
}

/* Active hitboxes. The original's loop reuses its counter as the offset, so it only ever looks at
 * the first two; kept. */
static int hitboxes_of(Fighter* f)
{
    return (f->x914[0].state != 0) + (f->x914[1].state != 0);
}
static int own_hitboxes(void) { return hitboxes_of(FP); }
static int opp_hitboxes(void) { return OPP != NULL ? hitboxes_of(OPP) : 0; }

static int is_spacie(void) { return K == Ft_Kind_Fox || K == Ft_Kind_Falco; }

static float absf(float v) { return v < 0.0f ? -v : v; }
static double absd(double v) { return v < 0.0 ? -v : v; }

static int opp_within(float r)
{
    return OPP != NULL && !(absd(ai_dx) > (double) r) && !(absd(ai_dy) > (double) r);
}
static int opp_within_x(float r) { return OPP != NULL && !(absd(ai_dx) > (double) r); }
static int opp_within_y(float r) { return OPP != NULL && !(absd(ai_dy) > (double) r); }

/* Getting up from the floor or teching. */
static int in_getup_or_tech(void)
{
    switch (M) {
    case ftCo_MS_Passive:
    case ftCo_MS_PassiveStandF:
    case ftCo_MS_PassiveStandB:
    case ftCo_MS_DownStandU:
    case ftCo_MS_DownAttackU:
    case ftCo_MS_DownFowardU:
    case ftCo_MS_DownBackU:
    case ftCo_MS_DownStandD:
    case ftCo_MS_DownAttackD:
    case ftCo_MS_DownFowardD:
    case ftCo_MS_DownBackD:
        return 1;
    default:
        return 0;
    }
}

static int in_aerial_attack(void) { return M >= ftCo_MS_AttackAirN && M <= ftCo_MS_AttackAirLw; }

/* Above the level of the right ledge and between the two ledges. */
static int over_stage(void)
{
    if (FP->cur_pos.y < ai_ry) {
        return 0;
    }
    if (FP->cur_pos.x > ai_rx || FP->cur_pos.x < ai_lx) {
        return 0;
    }
    return 1;
}

static int opp_over_stage(void)
{
    return OPP != NULL && !(OPP->cur_pos.x > ai_rx) && !(OPP->cur_pos.x < ai_lx);
}

static int on_right_side(void) { return stage_center() > FP->cur_pos.x ? 0 : 1; }

/* The CPU level decides how often it acts at all: level 9 always, level 1 one frame in nine. */
static int level_roll(void)
{
    const int r = rand_n(9);
    int level = FP->cpu.level;
    if (level == 0) {
        level = 1;
    }
    return level > r;
}

static int opp_not_below(void) { return !(ai_dy < 0.0); }

/* The opponent's total vertical speed is under v. */
static int opp_vy_below(float v)
{
    if (OPP == NULL) {
        return 0;
    }
    return v > OPP->self_vel.y + OPP->x8c_kb_vel.y + OPP->x98_atk_shield_kb.y;
}

static int jumps_left(void) { return FP->co_attrs.max_jumps - FP->x1968_jumpsUsed; }

static int opp_shielding(void) { return OPP != NULL && OM >= ftCo_MS_GuardOn && OM <= ftCo_MS_GuardReflect; }
static int opp_in_hitstun(void) { return OPP != NULL && OPP->x221C_b6; }
static int in_hitstun(void) { return FP->x221C_b6 ? 1 : 0; }

/* The opponent can be approached: it is busy (hitstun, standing, crouching, landing), far away
 * and running, or out of jumps. Not while it is invincible. */
static int opp_open(void)
{
    int m, jumps;
    if (OPP == NULL || OPP->x198C != 0) {
        return 0;
    }
    if (FP->x198C != 0 || OPP->x221C_b6) {
        return 1;
    }
    m = OM;
    if ((m >= ftCo_MS_Wait && m <= ftCo_MS_WalkFast) || (m >= ftCo_MS_Squat && m <= ftCo_MS_SquatRv) ||
        m == ftCo_MS_LandingFallSpecial || (m >= ftCo_MS_LandingAirN && m <= ftCo_MS_LandingAirLw))
    {
        return 1;
    }
    if ((m == ftCo_MS_Dash || m == ftCo_MS_Run) && !(absd(ai_dx) < 70.0)) {
        return 1;
    }
    jumps = OPP->co_attrs.max_jumps;
    if (jumps == OPP->x1968_jumpsUsed) {
        return 1;
    }
    /* The original goes on to compare the jump count against the shield and floor motion numbers
     * (it meant the motion); kept as it is, it never matches. */
    return jumps >= 0xB2 && jumps <= 0xD3;
}

/* The fighter faces the opponent. */
static int opp_ahead(void)
{
    if (OPP == NULL) {
        return 0;
    }
    if (fhi(FACING) == 0x3F80) {
        return FP->cur_pos.x < OPP->cur_pos.x;
    }
    return FP->cur_pos.x > OPP->cur_pos.x;
}

/* The fighter faces away from the middle of the stage. */
static int facing_offstage(void)
{
    const int right = fhi(FACING) == 0x3F80;
    if (FP->cur_pos.x > stage_center()) {
        return right;
    }
    return !right;
}

static int opp_percent_ge(float v) { return OPP != NULL && !(OPP->dmg.x1830_percent < v); }
static int percent_ge(float v) { return !(FP->dmg.x1830_percent < v); }

static int opp_knocked_down(void)
{
    return OPP != NULL && (OM == ftCo_MS_DownBoundU || OM == ftCo_MS_DownWaitU || OM == ftCo_MS_DownBoundD ||
                           OM == ftCo_MS_DownWaitD);
}
static int opp_in_aerial_landing(void)
{
    return OPP != NULL && OM >= ftCo_MS_LandingAirN && OM <= ftCo_MS_LandingAirLw;
}
static int opp_grabbing(void) { return OPP != NULL && OM == ftCo_MS_Catch; }

/* Yoshi's shield has its own motions (0x155 to 0x159). */
static int yoshi_shield_a(void) { return K == Ft_Kind_Yoshi && (M == 0x155 || M == 0x159); }
static int yoshi_shield_b(void) { return K == Ft_Kind_Yoshi && M >= 0x155 && M <= 0x159; }

/* Per-character tables (blob 0077C and 04370 to 04C48), by FighterKind. tables.py prints them. */
static const float ledge_fall_frame_tbl[27] = {
    7.0f, 0.0f, 3.0f, 2.0f, 7.0f, 2.0f, 2.0f, 7.0f, 1.0f,
    1.0f, 8.0f, 1.0f, 0.0f, 2.0f, 1.0f, 7.0f, 1.0f, 10.0f,
    2.0f, 2.0f, 4.0f, 7.0f, 0.0f, 0.0f, 4.0f, 1.0f, 1.0f,
};
static const float reach_x_tbl[27] = {
    45.8499985f, 37.5999985f, 64.0999985f, 48.1100006f, 43.5099983f, 29.0f, 42.0600014f, 39.9099998f, 38.7599983f,
    50.2599983f, 34.9000015f, 37.5999985f, 61.3699989f, 61.75f, 57.7700005f, 48.1399994f, 50.0400009f, 35.5299988f,
    48.5f, 47.9799995f, 40.7999992f, 45.8499985f, 43.5999985f, 55.6699982f, 43.8499985f, 38.3300018f, 39.3800011f,
};
static const unsigned char chance_ftilt_tbl[27] = {
    10, 10, 10, 10, 10, 10, 10, 25, 10,
    0, 10, 10, 10, 25, 10, 0, 10, 25,
    10, 10, 10, 10, 20, 10, 10, 10, 10,
};
static const unsigned char chance_dtilt_tbl[27] = {
    0, 10, 10, 10, 25, 10, 10, 10, 0,
    10, 10, 10, 10, 10, 25, 0, 25, 0,
    25, 0, 10, 0, 10, 10, 25, 10, 25,
};
static const unsigned char chance_utilt_tbl[27] = {
    10, 25, 10, 10, 25, 10, 10, 25, 10,
    0, 10, 10, 10, 10, 10, 25, 10, 25,
    25, 10, 10, 10, 25, 10, 10, 0, 10,
};
static const unsigned char chance_usmash_tbl[27] = {
    10, 20, 5, 5, 5, 0, 5, 10, 5,
    5, 10, 5, 20, 0, 10, 10, 5, 10,
    5, 5, 5, 10, 5, 10, 5, 5, 5,
};
static const unsigned char chance_dsmash_tbl[27] = {
    8, 8, 4, 4, 4, 4, 4, 8, 0,
    20, 8, 4, 8, 20, 8, 4, 4, 8,
    4, 8, 4, 8, 8, 4, 0, 4, 4,
};
static const unsigned char chance_fsmash_tbl[27] = {
    7, 7, 4, 4, 4, 0, 7, 4, 4,
    4, 7, 4, 4, 12, 7, 7, 0, 4,
    12, 4, 4, 4, 12, 0, 7, 4, 7,
};
static const unsigned char chance_dair_tbl[27] = {
    20, 25, 25, 20, 20, 0, 20, 25, 20,
    25, 25, 30, 20, 25, 20, 25, 15, 25,
    25, 0, 25, 20, 30, 15, 20, 25, 0,
};
static const unsigned char chance_nair_tbl[27] = {
    25, 35, 35, 20, 0, 15, 35, 35, 20,
    35, 15, 35, 25, 35, 20, 25, 20, 35,
    25, 15, 25, 25, 35, 25, 25, 15, 20,
};
static const unsigned char chance_uair_tbl[27] = {
    25, 25, 30, 30, 15, 15, 20, 25, 20,
    25, 25, 30, 30, 25, 25, 25, 20, 20,
    20, 15, 20, 25, 20, 15, 15, 30, 15,
};
static const unsigned char chance_bair_tbl[27] = {
    20, 30, 25, 25, 25, 15, 20, 30, 25,
    30, 25, 30, 15, 20, 20, 30, 25, 25,
    20, 30, 20, 25, 30, 15, 15, 25, 15,
};
static const unsigned char chance_fair_tbl[27] = {
    20, 15, 30, 15, 20, 20, 20, 30, 20,
    25, 20, 30, 15, 20, 25, 25, 25, 25,
    30, 30, 20, 25, 15, 15, 25, 25, 15,
};

static int kind_index(void) { return K >= 0 && K < 27 ? K : Ft_Kind_Nana; }

/* The frame of the fall from a ledge on which each character jumps back. */
static float ledge_fall_frame(void) { return ledge_fall_frame_tbl[kind_index()]; }
/* How far in front each character's running attacks reach. */
static float reach_x(void) { return reach_x_tbl[kind_index()]; }
/* Percent chances of each move when the opponent is in range. */
static int chance_ftilt(void) { return chance_ftilt_tbl[kind_index()]; }
static int chance_dtilt(void) { return chance_dtilt_tbl[kind_index()]; }
static int chance_utilt(void) { return chance_utilt_tbl[kind_index()]; }
static int chance_usmash(void) { return chance_usmash_tbl[kind_index()]; }
static int chance_dsmash(void) { return chance_dsmash_tbl[kind_index()]; }
static int chance_fsmash(void) { return chance_fsmash_tbl[kind_index()]; }
static int chance_dair(void) { return chance_dair_tbl[kind_index()]; }
static int chance_nair(void) { return chance_nair_tbl[kind_index()]; }
static int chance_uair(void) { return chance_uair_tbl[kind_index()]; }
static int chance_bair(void) { return chance_bair_tbl[kind_index()]; }
static int chance_fair(void) { return chance_fair_tbl[kind_index()]; }

/* Airborne and free to act: the jumps and falls, and the knockback tumbles. */
static int in_free_air_state(void)
{
    return (M >= ftCo_MS_JumpAerialF && M < ftCo_MS_Squat) || (M >= 0x54 && M < 0x5C);
}

/* A stick value the game's pad would give: 1/80 steps, nothing under 11, and 11 to 22 raised to
 * 23 (just past the dead zone). */
static int stick_steps(int n)
{
    if (n < 0x17) {
        n = n >= 0x0B ? 0x17 : 0;
    }
    return n & 0xFFFF;
}

/* A unit direction component (a sine or cosine) as a stick value. */
static float quant_unit(float v)
{
    const float sign = v >= 0.0f ? 1.0f : -1.0f;
    const int n = stick_steps((int) (80.0f * absf(v)));
    return (float) ((double) ((float) n * sign) / 80.0);
}

/* n/80 as a stick value, n at most 80. */
static float quant_stick(int n)
{
    if (n >= 0x50) {
        n = 0x50;
    }
    return (float) ((double) (float) stick_steps(n) / 80.0);
}

/* Distance to a point 4 under the nearer ledge. The height is always the right ledge's, and the
 * square root is the console's two estimate instructions, as in the original. */
static float ledge_dist(void)
{
    const float edge_x = FP->cur_pos.x > stage_center() ? ai_rx : ai_lx;
    const double ddx = (double) edge_x - (double) FP->cur_pos.x;
    const double ddy = ((double) ai_ry - 4.0) - (double) FP->cur_pos.y;
    const float sy = (float) (ddy * ddy);
    const float sx = (float) (ddx * ddx);
    return (float) mu_fres(mu_frsqrte((double) sy + (double) sx));
}
static int ledge_within(float r) { return !(ledge_dist() > r); }

/* In a second jump (Kirby and Jigglypuff: any of their extra jumps). */
static int in_double_jump(void)
{
    if (K == Ft_Kind_Purin || K == Ft_Kind_Kirby) {
        return M >= 0x155 && M <= 0x159;
    }
    return M == ftCo_MS_JumpAerialF || M == ftCo_MS_JumpAerialB;
}

static int is_fastfalling(void) { return FP->fall_fast ? 1 : 0; }
static int opp_on_ledge(void) { return OPP != NULL && (OM == ftCo_MS_CliffCatch || OM == ftCo_MS_CliffWait); }

/* How far outside the nearer ledge the fighter is (negative: inside), at most r. */
static int past_edge_le(float r)
{
    const float edge = absf(FP->cur_pos.x > stage_center() ? ai_rx : ai_lx);
    return !((double) absf(FP->cur_pos.x) - (double) edge > (double) r);
}

/* Within r above or below the nearer ledge. */
static int y_near_edge(float r)
{
    const float edge_y = FP->cur_pos.x > stage_center() ? ai_ry : ai_ly;
    return !(absd((double) FP->cur_pos.y - (double) edge_y) > (double) r);
}

/* Any of the flag bits the original tests as one byte (hitlag sets one; so does a fast fall). */
static int flags_221A(void)
{
    return FP->x221A_b0 | FP->x221A_b1 | FP->allow_sdi | FP->x221A_b3 | FP->fall_fast | FP->x221A_b5 |
           FP->x221A_b6 | FP->x221A_b7;
}

/* ---- one frame of thought (blob 00280 to 038DC) ----
 * Ends through `clear` (drop the plan) or `done` (keep it); both return the frame's buttons. */
static unsigned int ai_think(void)
{
    int act, r, r3, mode, opt, ft, n;
    float v, x, y, rr, d, reach, e;
    double ax, ay, t;

    /* 00280 */
    FP->dmg.x18C8 = -1;
    mode = hp_cpu_mode;
    if (mode != 1) goto L_002D4;
    /* Debug menu "hold shield": keep L down while shielding. */
    if (yoshi_shield_b() == 1) goto L_002C0;
    if (M < 0xB2) goto L_002D0;
    if (M > 0xB6) goto L_002D0;
L_002C0:
    btn_L();
    stick_neutral();
    cstick_neutral();
    goto clear;
L_002D0:
    goto L_002F4;
L_002D4:
    if (mode != 2) goto L_002F4;
    if (ACT == 0x10) goto L_017BC;
    if (ACT == 0x0C) goto L_015D4;
L_002F4:
    /* Continue the plan chosen on an earlier frame. */
    act = ACT;
    if (act == 0x25) goto L_0042C;
    if (act == 0x24) goto L_004BC;
    if (act == 0x23) goto L_00504;
    if (act == 0x22) goto L_005A4;
    if (act == 0x21) goto L_00FC4;
    if (act == 0x20) goto L_01598;
    if (act == 0x1F) goto L_00890;
    if (act == 0x1E) goto L_009F0;
    if (act == 0x1D) goto L_00B18;
    if (act == 0x1C) goto L_00B18;
    if (act == 0x1B) goto L_0102C;
    if (act == 0x1A) goto L_01048;
    if (act == 0x19) goto L_00EBC;
    if (act == 0x18) goto L_01E18;
    if (act == 0x17) goto L_011E0;
    if (act == 0x16) goto L_00E14;
    if (act == 0x15) goto L_00EFC;
    if (act == 0x14) goto L_01420;
    if (act == 0x13) goto L_00D08;
    if (act == 0x12) goto L_00F8C;
    if (act == 0x11) goto L_010A4;
    if (act == 0x10) goto L_017BC;
    if (act == 0x0F) goto L_01134;
    if (act == 0x0E) goto L_014C0;
    if (act == 0x0D) goto L_015A0;
    if (act == 0x0C) goto L_015D4;
    if (act == 0x0B) goto L_015B4;
    if (act == 0x0A) goto L_01C64;
    if (act == 0x09) goto L_01E00;
    if (act == 0x08) goto L_01E34;
    if (act == 0x07) goto L_02548;
    if (act == 0x06) goto L_02548;
    if (act == 0x05) goto L_00BF8;
    if (act == 0x04) goto L_02548;
    if (act == 0x03) goto L_02548;
    if (act == 0x02) goto L_02548;
    if (act == 0x01) goto L_0260C;
    if (act == 0x00) goto L_02844;

    /* act 25: at the edge, facing in. Jump, then airdodge down and back so the wavedash ends
     * hanging on the ledge. */
L_0042C:
    if (M == 0x1D) goto L_0045C;
    if (M == 0x2B) goto L_004B4;
    if (M == 0x18) goto done;
    if (M == 0x19) goto L_00488;
    if (M == 0x1A) goto L_00488;
    goto clear;
L_0045C:
    if (!y_near_edge(20.0f)) goto clear;
    if (is_fastfalling() == 1) goto L_004B4;
    btn_none();
    stick_down();
    cstick_neutral();
    goto done;
L_00488:
    btn_L();
    SY = -0.2875f;
    SX = -facing_mul(0.95f);
    cstick_neutral();
    trace_event(EV_AIRDODGE);
    goto done;
L_004B4:
    inputs_none();
    goto done;

    /* act 24: teching or getting up. Shielding at the end rolls away at random; standing again
     * hands over to the idle logic, which reads this act once. */
L_004BC:
    if (M == 0x0E) goto L_028CC;
    if (M == 0xB6) goto L_004E0;
    if (in_getup_or_tech() == 1) goto done;
    goto clear;
L_004E0:
    btn_L();
    stick_neutral();
    if (rand100() > 0x32) goto L_004FC;
    cstick_left();
    goto clear;
L_004FC:
    cstick_right();
    goto clear;

    /* act 23: Sheik at the edge facing out. Walk off, then needles near the ledge's height. */
L_00504:
    if (FP->ground_or_air == GA_Ground) goto L_00598;
    if (M == 0x1D) goto L_00540;
    if (M == 0x159) goto L_00588;
    if (M == 0x15A) goto L_0057C;
    if (M == 0x15B) goto L_00538;
    goto clear;
L_00538:
    inputs_none();
    goto done;
L_00540:
    if (!y_near_edge(20.0f)) goto clear;
    if (!y_near_edge(10.0f)) goto L_00538;
    stick_neutral();
    SX = -facing_mul(0.5f);
    btn_B();
    goto done;
L_0057C:
    btn(HSD_PAD_B | HSD_PAD_L);
    goto L_0058C;
L_00588:
    btn_B();
L_0058C:
    stick_neutral();
    cstick_neutral();
    goto done;
L_00598:
    stick_forward();
    btn_none();
    goto done;

    /* act 22: from the ledge. Drop, jump on the character's frame, then an aerial at the opponent
     * and drift back to the stage. */
L_005A4:
    if (M == 0x1D) goto L_005CC;
    if (in_double_jump() == 1) goto L_00608;
    if (in_aerial_attack() == 1) goto L_00718;
    goto clear;
L_005CC:
    if (fbits(ledge_fall_frame()) == fbits(FRAME)) goto L_005E8;
    if (rand100() > 0x0F) goto L_005F8;
L_005E8:
    btn_X();
    stick_neutral();
    cstick_neutral();
    goto done;
L_005F8:
    btn_none();
    cstick_neutral();
    stick_down();
    goto done;
L_00608:
    if (!opp_within(30.0f)) goto L_00718;
    if (rand100() > 0x19) goto L_00718;
L_00624:
    if (!opp_ahead()) goto L_00640;
    if (rand100() > 5) goto L_00668;
    goto L_00654;
L_00640:
    r = rand100();
    if (r > chance_bair() + 0x1E) goto L_00668;
L_00654:
    cstick_neutral();
    CX = -FACING;
    goto L_00718;
L_00668:
    if (is_spacie() == 1) goto L_006B4;
    if (opp_ahead() == 1) goto L_00690;
    if (rand100() > 5) goto L_006B4;
    goto L_006A4;
L_00690:
    r = rand100();
    if (r > chance_fair() + 0x0F) goto L_006B4;
L_006A4:
    cstick_neutral();
    CX = FACING;
    goto L_00718;
L_006B4:
    r = rand100();
    if (r > chance_dair()) goto L_006D4;
    cstick_neutral();
    CY = -1.0f;
    goto L_00718;
L_006D4:
    r = rand100();
    if (r > chance_nair()) goto L_006F4;
    cstick_neutral();
    btn_A();
    stick_neutral();
    goto done;
L_006F4:
    r = rand100();
    if (r > chance_uair()) goto L_00714;
    cstick_neutral();
    CY = 1.0f;
    goto L_00718;
L_00714:
    goto L_00624;
L_00718:
    r3 = on_right_side();
    if (r3 == 0) goto L_00734;
    if (FP->cur_pos.y > ai_ry) goto L_00768;
    goto L_0073C;
L_00734:
    if (FP->cur_pos.y > ai_ly) goto L_00768;
L_0073C:
    if (!y_near_edge(15.0f)) goto L_00774;
    if (!in_double_jump()) goto L_00768;
    if (rand100() > 0x0A) goto L_00768;
    set_act(0x0E);
L_00768:
    btn_none();
    stick_forward();
    goto done;
L_00774:
    stick_neutral();
    goto done;

    /* act 1F: Peach. Jump, hold the jump to float, attack out of the float, drop when the attack
     * is out. */
L_00890:
    if (M == 0x18) goto L_009E4;
    if (M == 0x19) goto L_009E0;
    if (M == 0x1A) goto L_009E0;
    if (M == 0x155) goto L_008F4;
    if (M < 0x158) goto L_008C4;
    if (M <= 0x15C) goto L_008C8;
L_008C4:
    goto clear;
L_008C8:
    if (!own_hitboxes()) goto L_009E4;
    if (rand100() > 0x4B) goto L_009E4;
    btn_none();
    stick_down();
    FP->active_timer.lstick.y = 0;
    goto clear;
L_008F4:
    opt = hp_aerial_option;
    if (opt == 0) goto L_0092C;
    if (opt == 1) goto L_00938;
    if (opt == 2) goto L_00968;
    if (opt == 3) goto L_00994;
    if (opt == 4) goto L_009B8;
    if (opt == 5) goto L_009D0;
L_0092C:
    if (rand100() > 0x23) goto L_00950;
L_00938:
    stick_neutral();
    cstick_neutral();
    btn(HSD_PAD_X | HSD_PAD_A);
    goto done;
L_00950:
    if (!opp_ahead()) goto L_0097C;
    if (rand100() > 0x14) goto L_0097C;
L_00968:
    cstick_neutral();
    CX = FACING;
    btn_X();
    goto done;
L_0097C:
    if (opp_ahead() == 1) goto L_009AC;
    if (rand100() > 0x23) goto L_009AC;
L_00994:
    cstick_neutral();
    CX = -FACING;
    btn_X();
    goto done;
L_009AC:
    if (rand100() > 0x14) goto L_009C4;
L_009B8:
    cstick_up();
    btn_X();
    goto done;
L_009C4:
    if (rand100() > 0x14) goto L_009DC;
L_009D0:
    cstick_down();
    btn_X();
    goto done;
L_009DC:
    goto L_009E4;
L_009E0:
    stick_down();
L_009E4:
    btn_X();
    cstick_neutral();
    goto done;

    /* act 1E: Captain Falcon and Marth from the ledge. Drop with a fast fall, jump, airdodge down
     * and in to land on the stage. */
L_009F0:
    if (M == 0x1D) goto L_00A18;
    if (M == 0x1B) goto L_00A88;
    if (M == 0x2B) goto L_00B10;
    if (M == 0xEC) goto L_00B10;
    goto clear;
L_00A18:
    if (!is_fastfalling()) goto L_00B00;
    if (!ledge_within(15.0f)) goto L_00A60;
    if (rand100() < 5) goto L_00A7C;
    if (!opp_within(10.0f)) goto L_00AE8;
    if (opp_on_ledge() == 1) goto L_00A7C;
    goto L_00AE8;
L_00A60:
    if (!is_fastfalling()) goto L_00B00;
    if (fhi(FRAME) == 0x4000) goto L_00A80;
    goto L_00B00;
L_00A7C:
    clear_act();
L_00A80:
    btn_X();
    goto L_00AEC;
L_00A88:
    if (K == 2) goto L_00AA8;
    if (past_edge_le(-3.0f) == 1) goto L_00ABC;
    goto L_00AE8;
L_00AA8:
    if (past_edge_le(-6.0f) == 1) goto L_00ABC;
    goto L_00AE8;
L_00ABC:
    btn_L();
    cstick_neutral();
    SY = -0.7f;
    SX = FACING * -0.7f;
    trace_event(EV_AIRDODGE);
    goto done;
L_00AE8:
    btn_none();
L_00AEC:
    stick_neutral();
    cstick_neutral();
    SX = FACING;
    goto done;
L_00B00:
    btn_none();
    stick_down();
    cstick_neutral();
    goto done;
L_00B10:
    inputs_none();
    goto done;

    /* acts 1C and 1D: Captain Falcon and Ganondorf from the ledge. Drop, jump (1C), and up
     * special if the opponent is on the ledge next to them (1D). */
L_00B18:
    if (act == 0x1D) goto L_00BC0;
    if (M == 0x1D) goto L_00B40;
    if (M == 0x1B) goto L_00B88;
    goto clear;
L_00B40:
    if (K == 2) goto L_00B50;
    if (fhi(FRAME) == 0x3F80) goto L_00BB0;
L_00B50:
    if (fhi(FRAME) == 0) goto L_00B74;
    if (rand100() > 0x19) goto L_00BB0;
    if (fhi(FRAME) == 0x40E0) goto L_00B74;
    goto L_00B78;
L_00B74:
    set_act(0x1D);
L_00B78:
    btn_X();
    stick_neutral();
    cstick_neutral();
    goto done;
L_00B88:
    if (K == 0x19) goto L_00BC0;
    if (is_fastfalling() == 1) goto L_00BC0;
    if (FP->self_vel.y >= 0.0f) goto L_00BC0;
L_00BB0:
    btn_none();
    stick_down();
    cstick_neutral();
    goto done;
L_00BC0:
    if (M != 0x1B) goto clear;
    if (!opp_within(10.0f)) goto L_00BE8;
    if (opp_on_ledge() == 1) goto L_00BF0;
L_00BE8:
    inputs_none();
    goto done;
L_00BF0:
    up_B();
    goto clear;

    /* act 5: Fox and Falco short hop laser. Short hop, fire, fast fall, and sometimes jump again
     * on landing. */
L_00BF8:
    if (M == 0x18) goto L_00C38;
    if (M == 0xF4) goto L_00C60;
    if (M == 0x19) goto L_00C78;
    if (M == 0x1A) goto L_00C78;
    if (M == 0x158) goto L_00C40;
    if (M == 0x159) goto L_00CA8;
    if (M == 0x2A) goto L_00CC4;
    goto clear;
L_00C38:
    FP->mv.co.kneebend.is_short_hop = 1;
L_00C40:
    btn_none();
    if (FP->self_vel.y >= 0.0f) goto L_00C5C;
    SY = -1.0f;
    trace_event(EV_FASTFALL);
L_00C5C:
    goto done;
L_00C60:
    if (rand100() > 0x4B) goto L_00C70;
    goto L_00CB8;
L_00C70:
    btn_none();
    goto done;
L_00C78:
    if (K == 1) goto L_00CB8;
    if (fbits(FRAME) == 0x40A00000u) goto L_00CB8;
    if (rand100() < 0x0A) goto L_00CB8;
    btn_none();
    goto done;
L_00CA8:
    if (K == 1) goto L_00CB8;
    goto L_00C40;
L_00CB8:
    btn_B();
    stick_neutral();
    goto done;
L_00CC4:
    if (fhi(FRAME) != 0x4080) goto done;
    if (K == 1) goto L_00CE4;
    rr = 20.0f;
    goto L_00CE8;
L_00CE4:
    rr = 60.0f;
L_00CE8:
    if (opp_within(rr) == 1) goto clear;
    if (rand100() > 0x19) goto clear;
    btn_X();
    goto done;

    /* act 13: Falco from the ledge. Drop, jump, laser while drifting in. */
L_00D08:
    if (M == 0x158) goto L_00DB4;
    if (M == 0x159) goto L_00DDC;
    if (M == 0x1B) goto L_00D8C;
    if (M == 0x1D) goto L_00D30;
    goto clear;
L_00D30:
    if (FRAME == 7.0f) goto L_00D7C;
    if (FRAME < 5.0f) goto L_00D6C;
    if (rand100() > 0x32) goto L_00D6C;
    goto L_00D7C;
L_00D6C:
    btn_none();
    cstick_neutral();
    stick_down();
    goto done;
L_00D7C:
    btn_X();
    stick_neutral();
    cstick_neutral();
    goto done;
L_00D8C:
    stick_neutral();
    btn_B();
    cstick_neutral();
    SX = 0.3125f * FACING;
    goto done;
L_00DB4:
    stick_neutral();
    btn_none();
    cstick_neutral();
    SX = 0.5f * FACING;
    goto done;
L_00DDC:
    stick_neutral();
    btn_none();
    cstick_neutral();
    SX = 0.5f * FACING;
    if (fhi(FRAME) != 0x4000) goto L_00E10;
    btn_B();
L_00E10:
    goto done;

    /* act 16: Fox and Falco at the edge facing out. Walk off, shine on frame 6 of the fall, turn
     * the shine around, then jump and up special. */
L_00E14:
    cstick_neutral();
    trigger_none();
    if (FP->ground_or_air == GA_Ground) goto L_00E58;
    if (M == 0x1D) goto L_00E64;
    if (M == 0x16D) goto L_00E84;
    if (M == 0x16E) goto L_00E9C;
    if (M == 0x171) goto L_00E90;
    if (M == 0x1B) goto L_00EB4;
    goto clear;
L_00E58:
    btn_none();
    stick_forward();
    goto done;
L_00E64:
    if (fhi(FRAME) == 0x40C0) goto L_00E7C;
    btn_none();
    stick_down();
    goto done;
L_00E7C:
    down_B();
    goto done;
L_00E84:
    btn_none();
    stick_back();
    goto done;
L_00E90:
    btn_none();
    stick_up();
    goto done;
L_00E9C:
    btn_none();
    SX = FP->input.lstick[1].x;
    SY = FP->input.lstick[1].y;
    goto done;
L_00EB4:
    up_B();
    goto clear;

    /* act 19: Fox and Falco side special recovery. Press B during the dash to cut it short when
     * it would overshoot the ledge. */
L_00EBC:
    if (M == 0x15E) goto L_00EF4;
    if (M != 0x15F) goto clear;
    if (!past_edge_le(37.0f)) goto L_00EF4;
    if (rand100() > 0x32) goto L_00EF4;
    btn_B();
    goto done;
L_00EF4:
    btn_none();
    goto done;

    /* act 15: Fox and Falco from the ledge. Drop, jump, side special onto the stage. */
L_00EFC:
    if (M == 0x1D) goto L_00F24;
    if (M == 0x1B) goto L_00F3C;
    if (M == 0x15E) goto L_00F34;
    if (M == 0x15F) goto L_00F78;
    goto clear;
L_00F24:
    btn_X();
    stick_neutral();
    cstick_neutral();
    goto done;
L_00F34:
    inputs_none();
    goto done;
L_00F3C:
    if (K == 1) goto L_00F58;
    if (fhi(FRAME) == 0x40A0) goto L_00F60;
    goto L_00F34;
L_00F58:
    if (fhi(FRAME) != 0x4080) goto L_00F34;
L_00F60:
    stick_neutral();
    SX = FACING;
    cstick_neutral();
    btn_B();
    goto done;
L_00F78:
    if (rand100() > 0x20) goto L_00F34;
    btn_B();
    goto clear;

    /* act 12: Fox and Falco from the ledge. Drop, jump, up special. */
L_00F8C:
    if (M == 0x1D) goto L_00FA4;
    if (M == 0x1B) goto L_00FB4;
    goto clear;
L_00FA4:
    btn_X();
    stick_neutral();
    cstick_neutral();
    goto done;
L_00FB4:
    btn_B();
    stick_up();
    cstick_neutral();
    goto done;

    /* act 21: Peach from the ledge. Drop with a fast fall, then up special. */
L_00FC4:
    if (M == 0x1D) goto L_00FD4;
    goto clear;
L_00FD4:
    if (!is_fastfalling()) goto L_0101C;
    if (!y_near_edge(49.0f)) goto L_01014;
    if (!opp_on_ledge()) goto L_0100C;
    if (opp_within(49.0f) == 1) goto L_01014;
L_0100C:
    stick_back();
    goto L_01020;
L_01014:
    up_B();
    goto clear;
L_0101C:
    stick_down();
L_01020:
    btn_none();
    cstick_neutral();
    goto done;

    /* act 1B: full hop. Hold X through the jump squat. */
L_0102C:
    if (M != 0x18) goto clear;
    btn_X();
    FP->mv.co.kneebend.jump_input = JumpInput_XY;
    goto done;

    /* act 1A: roll (or spot dodge) out of shield. */
L_01048:
    if (yoshi_shield_a() == 1) goto L_01068;
    if (M == 0xB6) goto L_01068;
    if (M != 0xB2) goto clear;
L_01068:
    btn_L();
    stick_neutral();
    r = rand_n(3);
    if (r == 0) goto L_01094;
    if (r == 1) goto L_0109C;
    stick_down();
    cstick_neutral();
    goto clear;
L_01094:
    cstick_left();
    goto clear;
L_0109C:
    cstick_right();
    goto clear;

    /* act 11: Sheik from the ledge. Drop, up special, aim it onto the stage when the opponent is
     * on the ledge next to her. */
L_010A4:
    if (M == 0x1D) goto L_010C4;
    if (M == 0x166) goto L_010D4;
    if (M == 0x167) goto L_0112C;
    goto clear;
L_010C4:
    btn_B();
    stick_up();
    cstick_neutral();
    goto done;
L_010D4:
    if (!opp_within(20.0f)) goto L_01100;
    if (OPP == NULL) goto L_01100;
    if (OM == 0xFC) goto L_01110;
    if (OM == 0xFD) goto L_01110;
L_01100:
    btn_none();
    stick_down();
    cstick_neutral();
    goto done;
L_01110:
    btn_none();
    SX = FACING;
    SY = 0.0f;
    cstick_neutral();
    goto done;
L_0112C:
    inputs_none();
    goto done;

    /* act F: drop through a platform from shield, and usually attack on the way down. */
L_01134:
    if (yoshi_shield_a() == 1) goto L_011CC;
    if (M == 0xB2) goto L_011CC;
    if (M == 0xB6) goto L_011CC;
    if (M == 0xF4) goto L_01160;
    goto clear;
L_01160:
    if (fbits(FRAME) == 0) goto L_011C4;
    if (!opp_within(30.0f)) goto L_01190;
    if (rand100() > 0x4B) goto L_01190;
    set_act(0x0A);
    goto L_01C64;
L_01190:
    goto L_011C4;
L_011C4:
    inputs_none();
    goto done;
L_011CC:
    btn_L();
    stick_neutral();
    SY = -0.6875f;
    goto done;

    /* act 17: Fox and Falco charging the up special in the air. Pick the angle: at the ledge, at
     * a point past it, or a random spread around it, and give it as a stick direction. */
L_011E0:
    if (M != 0x162) goto clear;
    r3 = on_right_side();
    e = ai_lx;
    if (r3 == 0) goto L_01200;
    e = ai_rx;
L_01200:
    ax = (double) e - (double) FP->cur_pos.x;
    ay = ((double) ai_ry - 4.0) - (double) FP->cur_pos.y;
    if (FP->cur_pos.y > ai_ry) goto L_01340;
    if (rand_n(2) == 0) goto L_013CC;
    if (stage_info.grkind == Gr_Kind_PStadium) goto L_01290;
    if (stage_info.grkind == Gr_Kind_Battle) goto L_01290;
    r3 = on_right_side();
    if (r3 == 0) goto L_01278;
    if (FP->cur_pos.x < ai_rx) goto L_012F8;
    goto L_01280;
L_01278:
    if (FP->cur_pos.x > ai_lx) goto L_012F8;
L_01280:
    if (rand_n(2) == 0) goto L_012F8;
L_01290:
    d = ledge_dist();
    if (K == 1) goto L_012A8;
    reach = 87.0f;
    goto L_012AC;
L_012A8:
    reach = 107.0f;
L_012AC:
    if (d >= reach) goto L_013CC;
    t = ((double) reach - (double) d) / 360.0;
    n = rand_n(0x168);
    t = (double) (float) (t * (double) n);
    ay = ay + t;
    goto L_013CC;
L_012F8:
    d = ledge_dist();
    if (K == 1) goto L_01310;
    reach = 87.0f;
    goto L_01314;
L_01310:
    reach = 107.0f;
L_01314:
    if (d >= reach) goto L_013CC;
    t = (double) reach - (double) d;
    if (on_right_side() == 0) goto L_01338;
    t = -t;
L_01338:
    ax = t + ax;
    goto L_013CC;
L_01340:
    if (rand_n(2) == 0) goto L_013CC;
    ay = ay < 0.0 ? -ay : ay;
    ay = (double) (float) (2.0 * ay);
    t = ax < 0.0 ? -ax : ax;
    if (K == 1) goto L_0137C;
    reach = 87.0f;
    goto L_01380;
L_0137C:
    reach = 107.0f;
L_01380:
    t = (double) reach - t;
    ay = ay + t;
    ay = ay / 360.0;
    n = rand_n(0x168);
    ay = (double) (float) (ay * (double) n);
    ay = ay - (double) FP->cur_pos.y;
    goto L_013CC;
L_013CC:
    v = atan2f((float) ay, (float) ax);
    x = sinf(v);
    SY = quant_unit(x);
    x = cosf(v);
    SX = quant_unit(x);
    goto done;

    /* act 14: Marth from the ledge. Drop, jump, and on frame 3 of the jump a forward air, up air
     * or side special. */
L_01420:
    if (M == 0x1D) goto L_01458;
    if (M == 0x1B) goto L_01468;
    if (M == 0x42) goto L_014B8;
    if (M == 0x44) goto L_014B8;
    if (M == 0x159) goto L_014B8;
    if (M == 0x15B) goto L_014B8;
    goto clear;
L_01458:
    btn_X();
    stick_neutral();
    cstick_neutral();
    goto done;
L_01468:
    if (fhi(FRAME) != 0x4040) goto L_014B4;
    btn_none();
    stick_neutral();
    cstick_neutral();
    r = rand_n(3);
    if (r == 0) goto L_014A0;
    if (r == 1) goto L_014AC;
    btn_B();
    goto done;
L_014A0:
    CX = FACING;
    goto done;
L_014AC:
    cstick_up();
    goto done;
L_014B4:
    goto done;
L_014B8:
    inputs_none();
    goto done;

    /* act E: from the ledge. Drop, jump, and airdodge onto the stage once high enough. */
L_014C0:
    if (M == 0x1D) goto L_0156C;
    if (in_double_jump() == 1) goto L_014DC;
    goto L_01594;
L_014DC:
    btn_none();
    stick_forward();
    if ((double) FP->cur_pos.y + (double) FP->coll_data.desired_ecb.bottom.y < (double) ai_ry) goto L_01568;
    r3 = is_spacie();
    v = 0.0f;
    if (r3 == 0) goto L_01508;
    v = 1.0f;
L_01508:
    e = absf(FP->cur_pos.x > stage_center() ? ai_rx : ai_lx);
    if ((double) absf(FP->cur_pos.x) - (double) e > (double) v) goto L_01568;
    btn_L();
    SX = facing_mul(0.9125f);
    SY = -0.4f;
    trace_event(EV_AIRDODGE);
    goto clear;
L_01568:
    goto done;
L_0156C:
    btn_X();
    stick_neutral();
    SX = FACING;
    if (K != 0x0D) goto L_01590;
    SX = 0.0f;
L_01590:
    goto done;
L_01594:
    goto clear;

    /* act 20: up special out of shield (the jump was already pressed). */
L_01598:
    up_B();
    goto clear;

    /* act D: jump-cancelled down special. Wait out the jump squat, then press it. */
L_015A0:
    if (M == 0x18) goto done;
    down_B();
    goto clear;

    /* act B: jump out of shield, then an aerial. */
L_015B4:
    if (M == 0xB2) goto L_015C8;
    if (M != 0xB6) goto clear;
L_015C8:
    btn_X();
    set_act(0x0A);
    goto done;

    /* act C: holding shield. Let go at random, grab a shielding opponent in range, and when the
     * shield is hit (last frame of its stun) switch to the out of shield plan. */
L_015D4:
    if (yoshi_shield_b() == 1) goto L_015F4;
    if (M < 0xB2) goto clear;
    if (M > 0xB6) goto clear;
L_015F4:
    if (K != 0x0E) goto L_01618;
    if (M != 0x158) goto L_01618;
    if (fhi(FP->dmg.x195c_hitlag_frames) == 0x3F80) goto L_01634;
L_01618:
    if (M != 0xB5) goto L_0165C;
    if (fbits(FRAME) != fbits(FP->frame_speed_mul)) goto L_0165C;
L_01634:
    if (hp_cpu_mode == 2) goto L_01780;
    if (rand100() > 0x32) goto L_0165C;
    goto L_01780;
L_0165C:
    if (hp_cpu_mode == 2) goto L_01770;
    if (OPP == NULL) goto L_016C4;
    if (OM == 0xBC) goto L_01780;
    if (OM == 0xBD) goto L_01780;
    if (OM == 0xC4) goto L_01780;
    if (OM == 0xC5) goto L_01780;
    if (OM == 0xC8) goto L_01780;
    if (OM == 0xC9) goto L_01780;
    if (OM != 0x0E) goto L_016C4;
    if (fhi(OPP->cur_anim_frame) > 0x4270) goto L_01780;
L_016C4:
    if (!opp_shielding()) goto L_01718;
    if (!opp_ahead()) goto L_01718;
    if (!opp_within_x(20.0f)) goto L_01718;
    if (!opp_within_y(5.0f)) goto L_01718;
    if (!level_roll()) goto L_01718;
    btn_LA();
    stick_neutral();
    cstick_neutral();
    goto clear;
L_01718:
    if (FP->shield_health < 5.0f) goto L_0175C;
    if (!opp_within(30.0f)) goto L_0175C;
    if (TIMER == 0) goto L_0175C;
    TIMER--;
    goto L_01770;
L_0175C:
    if (rand100() > 0x55) goto L_0176C;
    goto L_01780;
L_0176C:
    goto clear;
L_01770:
    btn_L();
    stick_neutral();
    cstick_neutral();
    goto done;
L_01780:
    r3 = hp_shield_frames;
    if (hp_shield_frames_random == 0) goto L_017A0;
    r3 = rand_n(r3 + 1);
L_017A0:
    TIMER = (unsigned char) r3;
    set_act(0x10);
    btn_L();
    stick_neutral();
    cstick_neutral();
    goto done;

    /* act 10: the out of shield option. After the countdown: shine, up special or up smash out of
     * shield for the characters that have a good one, grab, jump into an aerial, drop through the
     * platform, roll, spot dodge, or wavedash out of shield. */
L_017BC:
    if (K != 0x0E) goto L_017E8;
    if (M == 0x155) goto L_0180C;
    if (M == 0x156) goto L_0180C;
    if (M == 0x159) goto L_0180C;
    goto clear;
L_017E8:
    if (M == 0xB5) goto L_01C54;
    if (M == 0xB6) goto L_0180C;
    if (M == 0xB2) goto L_0180C;
    if (M != 0xB3) goto clear;
L_0180C:
    if (TIMER == 0) goto L_01828;
    TIMER--;
    goto L_01C54;
L_01828:
    opt = hp_oos_option;
    if (opt == 0) goto L_018BC;
    if (opt == 1) goto L_019F4;
    if (opt == 2) goto L_01B78;
    if (opt != 3) goto L_01858;
    stick_back();
    goto L_01B98;
L_01858:
    if (opt != 4) goto L_01868;
    stick_forward();
    goto L_01B98;
L_01868:
    if (opt == 5) goto L_01BF4;
    if (opt != 6) goto L_01888;
    if (floor_type() == 1) goto L_01B08;
    goto L_018BC;
L_01888:
    if (K == 0x0E) goto L_018BC;
    if (opt == 7) goto L_01C1C;
    if (opt == 8) goto L_01ADC;
    if (opt == 9) goto L_01AA0;
    if (opt == 0x0A) goto L_019AC;
    if (opt == 0x0B) goto L_018E4;
L_018BC:
    if (!is_spacie()) goto L_018F8;
    if (!opp_within(10.0f)) goto L_018F8;
    if (rand100() > 0x19) goto L_018F8;
L_018E4:
    btn_L();
    stick_neutral();
    cstick_up();
    set_act(0x0D);
    goto done;
L_018F8:
    if (K == 0x12) goto L_01928;
    if (K == 5) goto L_01958;
    if (K == 6) goto L_01958;
    if (K == 0x11) goto L_01980;
    if (K == 0x0D) goto L_0196C;
    goto L_019C0;
L_01928:
    if (!opp_ahead()) goto L_019C0;
    if (!opp_within_x(20.0f)) goto L_019C0;
    if (!opp_within_y(10.0f)) goto L_019C0;
    goto L_019A0;
L_01958:
    if (!opp_within(20.0f)) goto L_019C0;
    goto L_019A0;
L_0196C:
    if (!opp_within_x(10.0f)) goto L_019C0;
    goto L_019A0;
L_01980:
    if (!opp_ahead()) goto L_019C0;
    if (!opp_within_x(10.0f)) goto L_019C0;
    goto L_019A0;
L_019A0:
    if (rand100() > 0x14) goto L_019C0;
L_019AC:
    btn_L();
    stick_neutral();
    cstick_up();
    set_act(0x20);
    goto done;
L_019C0:
    if (!opp_within(16.0f)) goto L_01A60;
    if (!opp_ahead()) goto L_01A60;
    if (!opp_shielding()) goto L_01A04;
    if (rand100() > 0x4B) goto L_01A04;
L_019F4:
    btn_LA();
    stick_neutral();
    cstick_neutral();
    goto clear;
L_01A04:
    if (!opp_in_aerial_landing()) goto L_01A50;
    if (3.125 < (double) OPP->cur_anim_frame / (double) OPP->frame_speed_mul) goto L_01A40;
    if (rand100() > 0x32) goto L_01A50;
    goto L_019F4;
L_01A40:
    if (rand100() > 0x19) goto L_01A50;
    goto L_019F4;
L_01A50:
    if (rand100() > 0x14) goto L_01A60;
    goto L_019F4;
L_01A60:
    if (K == 0x0E) goto L_01AB4;
    if (!opp_within(20.0f)) goto L_01AB4;
    if (K != 1) goto L_01AB4;
    if (!opp_ahead()) goto L_01AB4;
    if (rand100() > 0x0F) goto L_01AB4;
L_01AA0:
    cstick_neutral();
    stick_up();
    btn_L();
    set_act(0x18);
    goto done;
L_01AB4:
    if (K == 0x0E) goto L_01AF0;
    if (!opp_within(25.0f)) goto L_01AF0;
    if (rand100() > 0x28) goto L_01AF0;
L_01ADC:
    cstick_up();
    stick_neutral();
    btn_L();
    set_act(0x0A);
    goto done;
L_01AF0:
    if (floor_type() != 1) goto L_01B24;
    if (rand100() > 0x19) goto L_01B24;
L_01B08:
    btn_L();
    stick_neutral();
    cstick_neutral();
    SY = -0.6875f;
    set_act(0x0F);
    goto done;
L_01B24:
    if (opp_hitboxes() == 1) goto L_01B5C;
    if (OPP == NULL) goto L_01B6C;
    if (OM != 0xD4) goto L_01B6C;
    if (OPP->cur_anim_frame > 4.0f) goto L_01B6C;
L_01B5C:
    if (rand100() > 0x23) goto L_01BA0;
    goto L_01B78;
L_01B6C:
    if (rand100() > 0x0F) goto L_01BA0;
L_01B78:
    stick_neutral();
    if (rand_n(2) == 0) goto L_01B94;
    cstick_right();
    goto L_01B98;
L_01B94:
    cstick_left();
L_01B98:
    btn_L();
    goto clear;
L_01BA0:
    if (opp_hitboxes() == 1) goto L_01BD8;
    if (OPP == NULL) goto L_01BE8;
    if (OM != 0xD4) goto L_01BE8;
    if (OPP->cur_anim_frame > 4.0f) goto L_01BE8;
L_01BD8:
    if (rand100() > 0x23) goto L_01C04;
    goto L_01BF4;
L_01BE8:
    if (rand100() > 0x0F) goto L_01C04;
L_01BF4:
    btn_L();
    stick_down();
    cstick_neutral();
    goto clear;
L_01C04:
    if (K == 0x0E) goto L_01C50;
    if (floor_type() == 2) goto L_01C50;
L_01C1C:
    cstick_up();
    btn_L();
    stick_neutral();
    if (rand_n(2) == 0) goto L_01C40;
    set_act(0x07);
    goto done;
L_01C40:
    set_act(0x06);
    goto done;
L_01C50:
    goto clear;
L_01C54:
    btn_L();
    stick_neutral();
    cstick_neutral();
    goto done;

    /* act A: jump (already pressed), then one aerial picked by the character's chances and by
     * where the opponent is. Peach may float instead. */
L_01C64:
    if (K != 9) goto L_01C90;
    if (M != 0x18) goto L_01C90;
    if (rand100() > 0x23) goto L_01C90;
    set_act(0x1F);
    goto L_00890;
L_01C90:
    if (M == 0xF4) goto L_01CB0;
    if (M < 0x18) goto clear;
    if (M == 0x18) goto done;
    if (M > 0x1C) goto clear;
L_01CB0:
    if (rand100() > 0x19) goto done;
L_01CBC:
    opt = hp_aerial_option;
    if (opt == 0) goto L_01CF4;
    if (opt == 1) goto L_01DA4;
    if (opt == 2) goto L_01D6C;
    if (opt == 3) goto L_01D14;
    if (opt == 4) goto L_01DE8;
    if (opt == 5) goto L_01DC4;
L_01CF4:
    if (opp_ahead() == 1) goto L_01D38;
    r = rand_n(0x32);
    if (r > chance_bair()) goto L_01D48;
L_01D14:
    if (K == 0x0C) goto L_01DE8;
    btn_none();
    cstick_neutral();
    CX = -FACING;
    goto clear;
L_01D38:
    if (rand100() > 5) goto L_01D48;
    goto L_01D14;
L_01D48:
    if (is_spacie() == 1) goto L_01D94;
    if (opp_ahead() == 1) goto L_01D80;
    if (rand100() > 5) goto L_01D94;
L_01D6C:
    btn_none();
    cstick_neutral();
    CX = FACING;
    goto clear;
L_01D80:
    r = rand100();
    if (r > chance_fair()) goto L_01D94;
    goto L_01D6C;
L_01D94:
    r = rand100();
    if (r > chance_nair()) goto L_01DB4;
L_01DA4:
    btn_A();
    stick_neutral();
    cstick_neutral();
    goto clear;
L_01DB4:
    r = rand100();
    if (r > chance_dair()) goto L_01DD8;
L_01DC4:
    btn_none();
    cstick_neutral();
    CY = -1.0f;
    goto clear;
L_01DD8:
    r = rand100();
    if (r > chance_uair()) goto L_01DFC;
L_01DE8:
    btn_none();
    cstick_neutral();
    CY = 1.0f;
    goto clear;
L_01DFC:
    goto L_01CBC;

    /* act 9: jump-cancelled grab. */
L_01E00:
    if (M != 0x18) goto clear;
    btn_LA();
    stick_neutral();
    goto clear;

    /* act 18: jump-cancelled up smash. */
L_01E18:
    if (M != 0x18) goto clear;
    btn_none();
    cstick_up();
    stick_neutral();
    goto clear;

    /* act 8: dashing. Keep the stick where it was, then from the dash pick the next thing:
     * shield, laser, jump-cancelled grab, dash attack, jump-cancelled up smash, a jump into an
     * aerial, a wavedash, the edge plans, a turn, or more dashing. */
L_01E34:
    if (M == 0x12) goto L_01E54;
    if (M < 0x14) goto clear;
    if (M >= 0x15) goto clear;
    goto L_01ED0;
L_01E54:
    if (K != 0x12) goto L_01ED0;
    if (opp_within(40.0f) != 1) goto L_01ED0;
    if (!opp_in_hitstun()) goto L_01E8C;
    if (rand100() > 0x0A) goto L_01ED0;
    goto L_01E98;
L_01E8C:
    if (rand100() > 4) goto L_01ED0;
L_01E98:
    /* Marth: forward smash out of the turn, toward the opponent. */
    btn_none();
    stick_neutral();
    v = 1.0f;
    if (ai_dx >= 0.0) goto L_01EC4;
    goto L_01EC8;
L_01EC4:
    v = -v;
L_01EC8:
    CX = v;
    goto clear;
L_01ED0:
    btn_none();
    stick_neutral();
    SX = FP->input.lstick[1].x;
    SY = FP->input.lstick[1].y;
    if (M == 0x12) goto done;
    if (FRAME <= p_ftCommonData->x44) goto done;
    ft = floor_type();
    if (ft == 2) goto L_023B4;
    if (ft == 1) goto L_01F20;
    goto L_01F3C;
L_01F20:
    if (rand100() > 5) goto L_01F3C;
    btn_L();
    stick_neutral();
    set_act(0x0F);
    goto done;
L_01F3C:
    if (opp_in_hitstun() == 1) goto L_01FFC;
    if (opp_knocked_down() == 1) goto L_01FFC;
    if (!opp_within(35.0f)) goto L_01FFC;
    r = rand100();
    if (r > OPT_DEFENSE_GATE()) goto L_01FFC;
    if (opp_within(15.0f) == 1) goto L_01FA0;
    r = rand100();
    if (r > OPT_SHIELD()) goto L_01FDC;
L_01F94:
    btn_L();
    set_act_shield();
    goto done;
L_01FA0:
    if (!opp_hitboxes()) goto L_01FC4;
    r = rand100();
    if (r > OPT_SHIELD() + 0x1E) goto L_01FDC;
    goto L_01F94;
L_01FC4:
    r = rand100();
    if (r > OPT_SHIELD() + 0x0A) goto L_01FDC;
    goto L_01F94;
L_01FDC:
    if (rand100() > 5) goto L_01FFC;
    btn_L();
    stick_neutral();
    cstick_neutral();
    set_act(0x1A);
    goto done;
L_01FFC:
    r = rand100();
    if (r > OPT_ATTACK_GATE()) goto L_02370;
    if (!is_spacie()) goto L_020C8;
    if (K != 1) goto L_0203C;
    if (!opp_over_stage()) goto L_020C8;
    if (opp_knocked_down() == 1) goto L_020C8;
L_0203C:
    if (!opp_ahead()) goto L_020C8;
    if (opp_within(20.0f) == 1) goto L_020C8;
    if (K == 0x16) goto L_02078;
    if (opp_within(60.0f) == 1) goto L_020C8;
    goto L_02098;
L_02078:
    if (!opp_within(45.0f)) goto L_02098;
    if (rand100() > 0x0A) goto L_020C8;
    goto L_020BC;
L_02098:
    if (20.0 < absd(ai_dy)) goto L_020C8;
    if (rand100() > 0x19) goto L_020C8;
L_020BC:
    btn_X();
    set_act(0x05);
    goto done;
L_020C8:
    if (!opp_ahead()) goto L_020FC;
    if (!opp_within(20.0f)) goto L_020FC;
    if (rand100() > 0x0F) goto L_020FC;
    btn_X();
    set_act(0x09);
    goto done;
L_020FC:
    if (!opp_ahead()) goto L_0222C;
    if (opp_within(10.0f) == 1) goto L_0222C;
    if (!opp_within_y(25.0f)) goto L_0222C;
    if (K == 0x11) goto L_0222C;
    if (K == 0x00) goto L_0222C;
    if (K == 0x10) goto L_0222C;
    if (K == 0x1A) goto L_0222C;
    if (K == 0x05) goto L_0222C;
    if (K == 0x04) goto L_0222C;
    if (K == 0x02) goto L_0222C;
    if (K == 0x0C) goto L_0222C;
    if (K == 0x07) goto L_021D4;
    if (K == 0x09) goto L_021C0;
    if (!opp_within_x(30.0f)) goto L_0222C;
    if (opp_in_hitstun() == 1) goto L_0220C;
    if (opp_knocked_down() == 1) goto L_0220C;
    if (rand100() > 0x0F) goto L_0222C;
L_021B0:
    /* dash attack */
    btn_A();
    stick_neutral();
    cstick_neutral();
    goto clear;
L_021C0:
    if (!opp_within_x(30.0f)) goto L_0222C;
    goto L_021F4;
L_021D4:
    if (!opp_within_x(35.0f)) goto L_0222C;
    if (opp_within_x(20.0f) == 1) goto L_0222C;
L_021F4:
    if (opp_in_hitstun() == 1) goto L_0221C;
    if (opp_knocked_down() == 1) goto L_0221C;
L_0220C:
    if (rand100() > 0x13) goto L_0222C;
    goto L_021B0;
L_0221C:
    if (rand100() > 0x19) goto L_0222C;
    goto L_021B0;
L_0222C:
    if (K == 1) goto L_02244;
    if (K == 0x0C) goto L_02244;
    goto L_022B4;
L_02244:
    if (!opp_within(25.0f)) goto L_022B4;
    if (!opp_ahead()) goto L_022B4;
    if (opp_in_hitstun() == 1) goto L_02298;
    if (opp_knocked_down() == 1) goto L_02298;
    if (opp_percent_ge(100.0f) == 1) goto L_02298;
    if (rand100() > 0x0A) goto L_022B4;
    goto L_022A4;
L_02298:
    if (rand100() > 0x0F) goto L_022B4;
L_022A4:
    btn_X();
    cstick_neutral();
    set_act(0x18);
    goto done;
L_022B4:
    if (!opp_ahead()) goto L_022E4;
    rr = reach_x();
    if (!opp_within_x(rr)) goto L_02370;
    if (!opp_within_y(35.0f)) goto L_0232C;
    goto L_022F4;
L_022E4:
    if (!opp_within(30.0f)) goto L_02370;
L_022F4:
    if (opp_within(15.0f) == 1) goto L_0231C;
    if (rand100() > 0x14) goto L_02370;
L_02310:
    btn_X();
    set_act(0x0A);
    goto done;
L_0231C:
    if (rand100() < 0x1E) goto L_02310;
    goto L_02370;
L_0232C:
    if (opp_not_below() == 1) goto L_02370;
    if (opp_in_hitstun() == 1) goto L_02354;
    if (rand100() > 0x19) goto L_02370;
    goto L_02360;
L_02354:
    if (rand100() > 0x32) goto L_02370;
L_02360:
    btn_X();
    cstick_neutral();
    set_act(0x1B);
    goto done;
L_02370:
    if (floor_type() != 0) goto L_023B4;
    if (rand100() > 4) goto L_02398;
    if (ai_dx > 0.0) goto L_023D8;
    goto L_023E0;
L_02398:
    if (rand100() > 1) goto L_023B4;
    if (ai_dx > 0.0) goto L_023E0;
    goto L_023D8;
L_023B4:
    if (floor_type() != 2) goto L_024A4;
    if (rand100() > 0x0F) goto L_023EC;
    if (!on_right_side()) goto L_023E0;
L_023D8:
    set_act(0x06);
    goto L_023E4;
L_023E0:
    set_act(0x07);
L_023E4:
    btn_X();
    goto done;
L_023EC:
    if (facing_offstage() == 1) goto L_0241C;
    if (opp_on_ledge() == 1) goto L_0241C;
    (void) rand100();   /* the original draws here and does not use the number */
    set_act(0x25);
    btn_X();
    cstick_neutral();
    goto done;
L_0241C:
    if (!is_spacie()) goto L_02454;
    if (!facing_offstage()) goto L_02454;
    if (rand100() > 0x4B) goto L_02454;
    set_act(0x16);
L_02444:
    stick_forward();
    btn_none();
    cstick_neutral();
    goto done;
L_02454:
    if (K != 7) goto L_02480;
    if (!facing_offstage()) goto L_02480;
    if (rand100() > 0x4B) goto L_02480;
    set_act(0x23);
    goto L_02444;
L_02480:
    if (!facing_offstage()) goto L_024A4;
    if (rand100() > 0x32) goto L_024A4;
    stick_back();
    btn_none();
    goto done;
L_024A4:
    if (FP->cmd_vars[0] == 0) goto L_024B8;
    SX = 0.5f;
L_024B8:
    r3 = opp_open();
    if (r3 == 0) goto L_02500;
    r3 = opp_ahead();
    if (r3 == 0) goto L_024F4;
    if (rand100() > 0x5A) goto L_02500;
    if (FRAME <= p_ftCommonData->x4C) goto done;
    goto L_02520;
L_024F4:
    if (r3 > 0x5A) goto L_02500;   /* the original compares the answer above, never true */
    goto L_02534;
L_02500:
    if (rand100() > 0x19) goto L_02544;
    if (FRAME <= p_ftCommonData->x4C) goto L_02534;
L_02520:
    SX = FACING;
    FP->active_timer.lstick.x = 0;
    goto L_02544;
L_02534:
    SX = -FACING;
    goto L_02544;
L_02544:
    goto done;

    /* acts 2, 3, 4, 6, 7: wavedash. Wait out the jump squat, then airdodge on the first airborne
     * frame: straight down (4), or down and forward (2), back (3), left (6), right (7) with a
     * random length. */
L_02548:
    if (M == 0x19) goto L_02568;
    if (M == 0x1A) goto L_02568;
    if (M == 0x18) goto done;
    goto clear;
L_02568:
    if (ACT == 0x04) goto L_025E8;
    n = rand_n(0x25);
    x = quant_stick(n + 0x28);
    y = -0.2875f;
    if (ACT == 0x06) goto L_025DC;
    if (ACT == 0x07) goto L_025E4;
    if (ACT == 0x03) goto L_025C0;
    if (ACT == 0x02) goto L_025D0;
L_025C0:
    x = -x * FACING;
    goto L_025FC;
L_025D0:
    x = x * FACING;
    goto L_025FC;
L_025DC:
    x = -x;
    goto L_025FC;
L_025E4:
    goto L_025FC;
L_025E8:
    SX = 0.0f;
    SY = -1.0f;
    goto L_02604;
L_025FC:
    SX = x;
    SY = y;
L_02604:
    btn_L();
    trace_event(EV_AIRDODGE);
    goto clear;

    /* act 1: the first frame out of hitstun. Shine or Rest a close opponent; in the air attack,
     * jump or do nothing; on the ground wavedash, dash away, drop through the platform or jump. */
L_0260C:
    if (!level_roll()) goto L_02690;
    if (!is_spacie()) goto L_02654;
    if (K == 1) goto L_0263C;
    r3 = opp_within(8.0f);
    goto L_02644;
L_0263C:
    r3 = opp_within(10.0f);
L_02644:
    if (r3 == 0) goto L_02654;
    down_B();
    goto clear;
L_02654:
    if (opp_shielding() == 1) goto L_02690;
    if (K != 0x0F) goto L_02690;
    if (!over_stage()) goto L_02690;
    if (!opp_within(6.0f)) goto L_02690;
    down_B();
    goto clear;
L_02690:
    if (FP->ground_or_air == GA_Ground) goto L_02708;
    if (opp_within(60.0f) == 1) goto L_026C4;
    if (!over_stage()) goto L_026C4;
    btn_A();
    stick_neutral();
    goto clear;
L_026C4:
    if (jumps_left() == 0) goto L_026E4;
    if (rand100() > 0x4B) goto L_026E4;
    btn_X();
    goto clear;
L_026E4:
    if (!over_stage()) goto L_02704;
    if (rand100() > 0x4B) goto L_02704;
    btn_A();
    goto clear;
L_02704:
    goto clear;
L_02708:
    if (opp_within(20.0f) == 1) goto L_02840;
    ft = floor_type();
    if (ft == 2) goto L_027C0;
    if (ft == 1) goto L_027A0;
    if (rand100() > 0x23) goto L_02760;
    if (rand_n(2) == 1) goto L_02754;
    btn_X();
    set_act(0x03);
    goto done;
L_02754:
    btn_X();
    set_act(0x02);
    goto done;
L_02760:
    if (rand100() > 0x23) goto L_0279C;
    if (rand_n(2) == 1) goto L_0278C;
    btn_none();
    stick_right();
    set_act(0x08);
    goto done;
L_0278C:
    btn_none();
    stick_left();
    set_act(0x08);
    goto done;
L_0279C:
    goto L_0283C;
L_027A0:
    if (rand100() > 0x23) goto L_027C0;
    btn_none();
    stick_neutral();
    SY = -1.0f;
    goto clear;
L_027C0:
    if (on_right_side() == 1) goto L_02804;
    if (rand100() > 0x23) goto L_027E4;
    btn_X();
    set_act(0x07);
    goto done;
L_027E4:
    if (rand100() > 0x23) goto L_02800;
    btn_none();
    stick_right();
    set_act(0x08);
    goto done;
L_02800:
    goto L_0283C;
L_02804:
    if (rand100() > 0x23) goto L_0281C;
    btn_X();
    set_act(0x06);
    goto done;
L_0281C:
    if (rand100() > 0x23) goto L_02838;
    btn_none();
    stick_left();
    set_act(0x08);
    goto done;
L_02838:
    goto L_0283C;
L_0283C:
    btn_X();
L_02840:
    goto clear;

    /* act 0: no plan. */
L_02844:
    if (!in_getup_or_tech()) goto L_02858;
    set_act(0x24);
    goto done;
L_02858:
    /* Marth falling: one side special in 120 frames, toward the opponent, to stall. */
    if (M < 0x1D) goto L_028CC;
    if (M < 0x23) goto L_02878;
    if (M == 0x26) goto L_02888;
    goto L_028CC;
L_02878:
    if (K == 0x12) goto L_02888;
    goto L_028CC;
L_02888:
    if (FP->u.ms.x222C != 0) goto L_028CC;
    if (rand_n(0x78) != 0) goto L_028CC;
    r3 = opp_ahead();
    v = FACING;
    if (r3 == 1) goto L_028B8;
    v = -v;
L_028B8:
    SX = v;
    SY = 0.0f;
    btn_B();
    goto done;
L_028CC:
    /* On the ground and free to act: standing, walking, crouching, at an edge, a landing past its
     * lag, or a ground attack that can be interrupted. */
    if (FP->ground_or_air == GA_Air) goto L_02EEC;
    if (M < 0x0E) goto L_02EEC;
    if (M < 0x12) goto L_0293C;
    if (M == 0x28) goto L_0293C;
    if (M == 0xF5) goto L_0293C;
    if (M == 0xF6) goto L_0293C;
    if (M != 0x2A) goto L_0291C;
    if (FRAME >= FP->co_attrs.normal_landing_lag) goto L_0293C;
L_0291C:
    if (M < 0x2C) goto L_02938;
    if (M > 0x40) goto L_02938;
    if (FP->allow_interrupt) goto L_0293C;
L_02938:
    goto L_02EEC;
L_0293C:
    if (ACT != 0x24) goto L_029C8;
    /* Just teched or got up with the opponent near: shine, spot dodge or shield. */
    if (opp_within(30.0f) == 1) goto L_02964;
    clear_act();
    goto L_029C8;
L_02964:
    if (!is_spacie()) goto L_02988;
    r = rand100();
    if (r > OPT_SHINE_AFTER_TECH()) goto L_02988;
    down_B();
    goto clear;
L_02988:
    r = rand100();
    if (r > OPT_SPOTDODGE()) goto L_029A8;
    btn_L();
    stick_down();
    cstick_neutral();
    goto clear;
L_029A8:
    r = rand100();
    if (r > OPT_SHIELD_AFTER_TECH()) goto L_029C8;
    btn_L();
    cstick_neutral();
    stick_neutral();
    goto done;
L_029C8:
    mode = hp_cpu_mode;
    if (mode == 0) goto L_02A00;
    if (mode == 1) goto L_002C0;
    if (mode != 2) goto L_029F8;
    set_act_shield();
    goto L_01770;
L_029F8:
    if (mode == 3) goto L_02A0C;
L_02A00:
    if (level_roll() != 0) goto L_02A14;
L_02A0C:
    inputs_none();
    goto done;
L_02A14:
    if (!is_spacie()) goto L_02A5C;
    if (K == 1) goto L_02A38;
    r3 = opp_within(8.0f);
    goto L_02A40;
L_02A38:
    r3 = opp_within(10.0f);
L_02A40:
    if (r3 == 0) goto L_02A5C;
    if (rand100() > 0x4B) goto L_02A5C;
    down_B();
    goto clear;
L_02A5C:
    if (K != 0x0F) goto L_02AA4;
    if (opp_shielding() == 1) goto L_02AA4;
    if (!over_stage()) goto L_02AA4;
    if (!opp_within(6.0f)) goto L_02AA4;
    if (rand100() > 0x4B) goto L_02AA4;
    down_B();
    goto clear;
L_02AA4:
    /* Defence: only against an opponent that is free to attack. */
    if (opp_in_hitstun() == 1) goto L_02BB8;
    if (opp_shielding() == 1) goto L_02BB8;
    if (opp_knocked_down() == 1) goto L_02BB8;
    if (!opp_within(30.0f)) goto L_02BB8;
    r = rand100();
    if (r > OPT_DEFENSE_GATE()) goto L_02BB8;
    if (!opp_hitboxes()) goto L_02B74;
    r = rand100();
    if (r > OPT_SHIELD() + 0x0A) goto L_02B0C;
    goto L_02B84;
L_02B0C:
    if (rand100() > 0x14) goto L_02B1C;
    goto L_02BA4;
L_02B1C:
    if (K == 0x0F) goto L_02B34;
    if (opp_grabbing() == 1) goto L_02B70;
L_02B34:
    if (percent_ge(75.0f) == 1) goto L_02B60;
    if (rand100() > 0x32) goto L_02B70;
L_02B50:
    /* crouch (crouch cancel) */
    btn_none();
    cstick_neutral();
    stick_down();
    goto done;
L_02B60:
    if (rand100() > 0x28) goto L_02B70;
    goto L_02B50;
L_02B70:
    goto L_02BB8;
L_02B74:
    r = rand100();
    if (r > OPT_SHIELD()) goto L_02B98;
L_02B84:
    btn_L();
    stick_neutral();
    cstick_neutral();
    set_act_shield();
    goto done;
L_02B98:
    if (rand100() > 0x0A) goto L_02BB8;
L_02BA4:
    btn_L();
    stick_neutral();
    cstick_neutral();
    set_act(0x1A);
    goto done;
L_02BB8:
    /* Attack: tilts, smashes and jumps into aerials by the character's chances. */
    r = rand100();
    if (r > OPT_ATTACK_GATE()) goto L_02E4C;
    if (!opp_within(20.0f)) goto L_02BF0;
    if (rand100() > 0x19) goto L_02BF0;
    btn_X();
    cstick_neutral();
    set_act(0x0A);
    /* The original does not leave here: the checks below can still replace the jump. */
L_02BF0:
    if (!opp_ahead()) goto L_02C70;
    if (!opp_within_x(30.0f)) goto L_02C70;
    if (!opp_within_y(15.0f)) goto L_02C70;
    r = rand100();
    if (chance_ftilt() < r) goto L_02C48;
    btn_A();
    stick_neutral();
    cstick_neutral();
    SX = facing_mul(0.3125f);
    goto done;
L_02C48:
    r = rand100();
    if (chance_dtilt() < r) goto L_02C70;
    btn_A();
    stick_neutral();
    cstick_neutral();
    SY = -0.3125f;
    goto done;
L_02C70:
    if (!opp_within(20.0f)) goto L_02CA8;
    r = rand100();
    if (chance_utilt() < r) goto L_02CA8;
    btn_A();
    stick_neutral();
    cstick_neutral();
    SY = 0.3125f;
    goto done;
L_02CA8:
    if (opp_within_y(15.0f) == 1) goto L_02CF0;
    if (!opp_not_below()) goto L_02CD8;
    if (opp_vy_below(0.0f) == 1) goto L_02CF0;
    goto L_02E4C;
L_02CD8:
    if (!opp_vy_below(-0.0001f)) goto L_02CF0;
    goto L_02E4C;
L_02CF0:
    if (K == 0x01) goto L_02D10;
    if (K == 0x0C) goto L_02D10;
    if (K == 0x17) goto L_02D10;
    goto L_02D1C;
L_02D10:
    if (!opp_ahead()) goto L_02D80;
L_02D1C:
    if (K == 0x07) goto L_02D3C;
    if (K == 0x01) goto L_02D3C;
    if (K == 0x0C) goto L_02D3C;
    goto L_02D50;
L_02D3C:
    if (!opp_within(25.0f)) goto L_02D80;
    goto L_02D60;
L_02D50:
    if (!opp_within(15.0f)) goto L_02D80;
L_02D60:
    r = rand100();
    if (chance_usmash() < r) goto L_02D80;
    btn_none();
    stick_neutral();
    cstick_up();
    goto done;
L_02D80:
    if (!opp_within_x(25.0f)) goto L_02DD4;
    if (K == 0x07) goto L_02DB4;
    if (K == 0x09) goto L_02DB4;
    if (!opp_within_y(15.0f)) goto L_02DD4;
L_02DB4:
    r = rand100();
    if (chance_dsmash() < r) goto L_02DD4;
    btn_none();
    stick_neutral();
    cstick_down();
    goto done;
L_02DD4:
    if (K != 0x12) goto L_02DF4;
    if (!opp_within(40.0f)) goto L_02E4C;
    goto L_02E14;
L_02DF4:
    if (!opp_within_x(30.0f)) goto L_02E4C;
    if (!opp_within_y(15.0f)) goto L_02E4C;
L_02E14:
    r = rand100();
    if (chance_fsmash() < r) goto L_02E4C;
    btn_none();
    stick_neutral();
    cstick_neutral();
    r3 = opp_ahead();
    v = FACING;
    if (r3 == 1) goto L_02E44;
    v = -v;
L_02E44:
    CX = v;
    goto done;
L_02E4C:
    /* Movement: drop through a platform, or start a dash toward or away from the opponent. */
    if (floor_type() != 1) goto L_02E70;
    if (rand100() > 0x0A) goto L_02E70;
    btn_none();
    stick_down();
    goto done;
L_02E70:
    if (rand100() > 0x4B) goto L_02EEC;
    FP->active_timer.lstick.x = 0;
    btn_none();
    cstick_neutral();
    set_act(0x08);
    if (!opp_open()) goto L_02EB0;
    if (rand_n(4) == 0) goto L_02EC0;
    goto L_02ED0;
L_02EB0:
    if (rand_n(2) == 0) goto L_02ED0;
L_02EC0:
    if (!opp_ahead()) goto L_02EDC;
    goto L_02EE4;
L_02ED0:
    if (!opp_ahead()) goto L_02EE4;
L_02EDC:
    stick_forward();
    goto done;
L_02EE4:
    stick_back();
    goto done;

    /* Fox and Falco off the stage, free to act, not in hitstun: recovery. By height against the
     * ledge and distance to it: second jump, side special, fast fall, or up special. */
L_02EEC:
    if (!is_spacie()) goto L_032E4;
    if (over_stage() == 1) goto L_032E4;
    if (in_hitstun() == 1) goto L_032E4;
    if (!in_free_air_state()) goto L_032E4;
    if ((double) FP->cur_pos.y < (double) ai_ry - 12.0) goto L_03118;
    if ((double) FP->cur_pos.y < (double) ai_ry - 5.0) goto L_03054;
    if (past_edge_le(10.0f) == 1) goto L_0325C;
    if (K == 1) goto L_02F74;
    rr = 84.0f;
    goto L_02F78;
L_02F74:
    rr = 107.0f;
L_02F78:
    if (!ledge_within(rr)) goto L_03034;
    if (jumps_left() == 0) goto L_02FD0;
    if (rand100() > 0x0F) goto L_02FA0;
    goto L_0321C;
L_02FA0:
    if (!ledge_within(40.0f)) goto L_02FC0;
    if (rand100() > 3) goto L_03000;
    goto L_03288;
L_02FC0:
    if (rand100() > 7) goto L_03000;
    goto L_03288;
L_02FD0:
    if (FP->self_vel.y > 0.0f) goto L_02FF0;
    if (rand100() > 0x12) goto L_03000;
    goto L_03288;
L_02FF0:
    if (rand100() > 7) goto L_03000;
    goto L_03288;
L_03000:
    if (FP->self_vel.y >= 0.0f) goto L_0325C;
    if (is_fastfalling() == 1) goto L_0325C;
    if (rand100() > 0x19) goto L_0325C;
    SY = -1.0f;
    trace_event(EV_FASTFALL);
    goto L_0325C;
L_03034:
    if (jumps_left() == 0) goto L_03050;
    if (rand100() > 0x19) goto L_0325C;
    goto L_0321C;
L_03050:
    goto L_0325C;
L_03054:
    if (absf(FP->self_vel.y) > 0.46875f) goto L_03080;
    if (rand100() > 0x4B) goto L_032DC;
    goto L_0308C;
L_03080:
    if (rand100() > 0x32) goto L_032DC;
L_0308C:
    if (K == 1) goto L_030A0;
    rr = 87.0f;
    goto L_030A4;
L_030A0:
    rr = 93.0f;
L_030A4:
    if (!past_edge_le(rr)) goto L_030C0;
    if (rand100() > 0x4B) goto L_030C0;
    goto L_03264;
L_030C0:
    if (!past_edge_le(30.0f)) goto L_030E8;
    if (jumps_left() != 0) goto L_030E8;
    if (rand100() > 0x0A) goto L_032DC;
L_030E8:
    if (K == 1) goto L_030FC;
    rr = 80.0f;
    goto L_03100;
L_030FC:
    rr = 102.0f;
L_03100:
    if (!past_edge_le(rr)) goto L_032DC;
    up_B();
    set_act(0x17);
    goto done;
L_03118:
    if (K == 1) goto L_0312C;
    rr = 80.0f;
    goto L_03130;
L_0312C:
    rr = 102.0f;
L_03130:
    if (!ledge_within(rr)) goto L_031EC;
    if (jumps_left() == 0) goto L_031BC;
    if (rand100() > 0x14) goto L_03158;
    goto L_0321C;
L_03158:
    if (!ledge_within(40.0f)) goto L_03178;
    if (rand100() > 3) goto L_0325C;
    goto L_03250;
L_03178:
    if (rand100() > 5) goto L_0325C;
    goto L_03250;
L_031BC:
    if (FP->self_vel.y > 0.0f) goto L_031DC;
    if (rand100() > 0x12) goto L_0325C;
    goto L_03250;
L_031DC:
    if (rand100() > 7) goto L_0325C;
    goto L_03250;
L_031EC:
    if (jumps_left() == 0) goto L_03208;
    if (rand100() > 0x4B) goto L_0325C;
    goto L_0321C;
L_03208:
    if (FP->self_vel.y > 0.0f) goto L_0325C;
    goto L_03250;
L_0321C:
    /* second jump, sometimes away from the stage when close */
    btn_X();
    cstick_neutral();
    if (!ledge_within(45.0f)) goto done;
    if (rand100() < 0x42) goto done;
    SX = -FACING;
    goto done;
L_03250:
    up_B();
    set_act(0x17);
    goto done;
L_0325C:
    btn_none();
    goto L_032DC;
L_03264:
    /* side special toward the stage */
    btn_B();
    set_act(0x19);
    if (!on_right_side()) goto L_03280;
    stick_left();
    goto done;
L_03280:
    stick_right();
    goto done;
L_03288:
    if (y_near_edge(20.0f) == 1) goto L_032B8;
    if (y_near_edge(40.0f) == 1) goto L_032C8;
    if (rand100() > 0x19) goto L_032D8;
    goto L_032D4;
L_032B8:
    if (rand100() > 0x4B) goto L_032D8;
    goto L_032D4;
L_032C8:
    if (rand100() > 0x32) goto L_032D8;
L_032D4:
    goto L_03264;
L_032D8:
    goto L_03250;
L_032DC:
    btn_none();
    goto done;

    /* Hanging on a ledge: wait a random time, then pick a ledge plan for the character, or one of
     * the plain options (roll up, climb, jump, drop). */
L_032E4:
    if (M == 0xFC) goto L_03584;
    if (M != 0xFD) goto L_035B0;
    if (fbits(FRAME) == 0) goto L_03584;
    if (opp_within(30.0f) == 1) goto L_03320;
    if (!opp_ahead()) goto L_03330;
L_03320:
    if (rand100() > 0x4B) goto L_03340;
    goto L_0333C;
L_03330:
    if (rand100() > 0x5F) goto L_03340;
L_0333C:
    goto L_03584;
L_03340:
    if (K == 0x02) goto L_03354;
    if (K != 0x19) goto L_03368;
L_03354:
    if (rand100() > 0x14) goto L_03368;
    set_act(0x1C);
    goto L_03590;
L_03368:
    if (K != 0x09) goto L_03388;
    if (rand100() > 0x14) goto L_03388;
    set_act(0x21);
    goto L_03590;
L_03388:
    if (K == 0x02) goto L_0339C;
    if (K != 0x12) goto L_033B0;
L_0339C:
    if (rand100() > 0x14) goto L_033B0;
    set_act(0x1E);
    goto L_03590;
L_033B0:
    if (K != 0x07) goto L_033D0;
    if (rand100() > 0x14) goto L_033D0;
    set_act(0x11);
    goto L_03590;
L_033D0:
    if (!is_spacie()) goto L_03430;
    if (!opp_within(20.0f)) goto L_033FC;
    if (rand100() > 0x14) goto L_03410;
    goto L_03408;
L_033FC:
    if (rand100() > 0x0A) goto L_03410;
L_03408:
    set_act(0x12);
    goto L_03590;
L_03410:
    if (!opp_ahead()) goto L_03430;
    if (rand100() > 0x0D) goto L_03430;
    set_act(0x15);
    goto L_03590;
L_03430:
    if (K != 0x16) goto L_0345C;
    if (!opp_ahead()) goto L_0345C;
    if (rand100() > 0x14) goto L_0345C;
    set_act(0x13);
    goto L_03590;
L_0345C:
    if (K != 0x12) goto L_03498;
    if (!opp_ahead()) goto L_03498;
    if (!opp_within(40.0f)) goto L_03498;
    if (rand100() > 0x14) goto L_03498;
    set_act(0x14);
    goto L_03590;
L_03498:
    if (K == 0x19) goto L_034F8;
    if (K == 0x09) goto L_034F8;
    if (K == 0x08) goto L_034F8;
    if (K == 0x10) goto L_034F8;
    if (K == 0x0E) goto L_034F8;
    if (!opp_within(20.0f)) goto L_034E4;
    if (rand100() > 0x2D) goto L_034F8;
    goto L_034F0;
L_034E4:
    if (rand100() > 0x0F) goto L_034F8;
L_034F0:
    set_act(0x22);
    goto L_03590;
L_034F8:
    if (K == 0x09) goto L_03538;
    if (percent_ge(100.0f) == 1) goto L_03524;
    if (rand100() > 0x32) goto L_03538;
    goto L_03530;
L_03524:
    if (rand100() > 0x4B) goto L_03538;
L_03530:
    set_act(0x0E);
    goto L_03590;
L_03538:
    r = rand_n(4);
    if (r == 0) goto L_03560;
    if (r == 1) goto L_0356C;
    if (r == 2) goto L_03578;
    if (r == 3) goto L_03584;
L_03560:
    inputs_none();
    btn_L();
    goto done;
L_0356C:
    inputs_none();
    stick_forward();
    goto done;
L_03578:
    inputs_none();
    btn_X();
    goto done;
L_03584:
    inputs_none();
    trigger_none();
    goto done;
L_03590:
    /* let go of the ledge: C-stick away from the stage */
    trigger_none();
    stick_neutral();
    btn_none();
    cstick_neutral();
    CX = -FACING;
    goto done;

    /* Everything else. First a fast fall when dropping over the stage. */
L_035B0:
    if (in_aerial_attack() == 1) goto L_035E0;
    if (in_double_jump() == 1) goto L_035E0;
    if (M < 0x19) goto L_035DC;
    if (M < 0x27) goto L_035E0;
L_035DC:
    goto L_03624;
L_035E0:
    if (!over_stage()) goto L_03624;
    if (is_fastfalling() == 1) goto L_03624;
    if (FP->self_vel.y >= 0.0f) goto L_03624;
    if (rand100() > 0x23) goto L_03624;
    FP->active_timer.lstick.y = 0;
    SY = -1.0f;
    trace_event(EV_FASTFALL);
L_03624:
    if (flags_221A() == 0) goto L_03640;
    if (!FP->x221C_b6) goto L_038DC;
    goto L_038A0;
L_03640:
    /* The last frame of hitstun: plan the "out of hitstun" act for the next frame. */
    if (!FP->x221C_b6) goto L_03680;
    if (mv_word0() != 0x3F800000u) goto L_03680;
    if (!is_spacie()) goto L_03678;
    if (over_stage() == 1) goto L_03678;
    goto done;
L_03678:
    set_act(0x01);
    goto done;
L_03680:
    if (!is_spacie()) goto L_0389C;
    /* Fox and Falco: jump out of an aerial shine. */
    if (M != 0x16E) goto L_036D8;
    if (y_near_edge(2.0f) == 1) goto L_036D8;
    if (jumps_left() == 0) goto L_036D8;
    btn_X();
    if (K == 1) goto done;
    if (!over_stage()) goto done;
    set_act(0x0A);
    goto done;
L_036D8:
    /* Fox and Falco: jump out of a grounded shine, into a full hop, a grab, an aerial, another
     * shine or a wavedash. */
    if (M != 0x169) goto L_03858;
    btn_X();
    if (K == 1) goto L_03728;
    if (!opp_percent_ge(60.0f)) goto L_03714;
    if (rand100() > 0x23) goto L_03734;
    goto L_03720;
L_03714:
    if (rand100() > 0x19) goto L_03734;
L_03720:
    set_act(0x1B);
    goto done;
L_03728:
    if (rand100() <= 5) goto L_03720;
L_03734:
    if (!opp_ahead()) goto L_03780;
    if (!opp_within(10.0f)) goto L_03780;
    if (!opp_shielding()) goto L_0376C;
    if (rand100() > 0x14) goto L_03780;
    goto L_03778;
L_0376C:
    if (rand100() > 5) goto L_03780;
L_03778:
    set_act(0x09);
    goto done;
L_03780:
    if (!opp_within(20.0f)) goto L_037D8;
    if (opp_shielding() == 1) goto L_037C4;
    if (!opp_in_hitstun()) goto L_037B4;
    if (rand100() > 0x0F) goto L_037D8;
L_037B4:
    if (rand100() > 0x28) goto L_037D8;
    goto L_037D0;
L_037C4:
    if (rand100() > 0x4B) goto L_037D8;
L_037D0:
    set_act(0x0A);
    goto done;
L_037D8:
    if (!opp_shielding()) goto L_037F4;
    if (rand100() > 0x23) goto L_03808;
    goto L_03800;
L_037F4:
    if (rand100() > 0x0F) goto L_03808;
L_03800:
    set_act(0x0D);
    goto done;
L_03808:
    if (floor_type() == 0) goto L_03818;
    goto L_03854;
L_03818:
    if (rand100() > 0x55) goto L_03854;
    r = rand100();
    if (r > 0x5F) goto L_0383C;
    if (r > 0x42) goto L_03844;
    goto L_0384C;
L_0383C:
    set_act(0x04);
    goto done;
L_03844:
    set_act(0x03);
    goto done;
L_0384C:
    set_act(0x02);
    goto done;
L_03854:
    goto done;
L_03858:
    /* Fox and Falco standing or walking next to the opponent: shine. */
    if (K == 1) goto L_03870;
    r3 = opp_within(8.0f);
    goto L_03878;
L_03870:
    r3 = opp_within(10.0f);
L_03878:
    if (r3 == 0) goto L_0389C;
    if (M < 0x0E) goto done;
    if (M > 0x11) goto done;
    down_B();
    goto clear;
L_0389C:
    goto done;
L_038A0:
    /* Hit: drop the plan. On the ground, on the last frame of hitlag, usually hold the C-stick
     * down so the knockback is taken on the floor. */
    ACT = 0;
    if (FP->ground_or_air != GA_Ground) goto done;
    if (fhi(FP->dmg.x195c_hitlag_frames) != 0x3F80) goto done;
    if (rand100() > 0x4B) goto done;
    cstick_down();
    trace_event(EV_ASDI_DOWN);
    goto done;
L_038DC:
    goto done;

clear:
    clear_act();
done:
    return ai_btn;
}

/* ---- the frame's setup (blob 00000 to 0027C) ---- */

static AiState* state_of(Fighter* fp)
{
    AiState* st = &ai_states[fp->player_idx < 6 ? fp->player_idx : 0][fp->is_sub_fighter ? 1 : 0];
    if (st->owner != fp->gobj) {
        /* A new fighter in this slot starts with no plan, as a new object did on the console. */
        st->owner = fp->gobj;
        st->timer = 0;
        st->act = 0;
    }
    return st;
}

/* The two ledges: of every floor line that can be grabbed, the end farther from the middle of the
 * stage; the last one found on each side. Pokemon Stadium only counts its two main floor lines. */
static void find_ledges(void)
{
    const MapCollData* coll = mpLib_8004D164();
    const CollLine* lines = mpGetGroundCollLine();
    const CollVtx* verts = mpGetGroundCollVtx();
    const float center = stage_center();
    int i, count;
    ai_lx = ai_ly = ai_rx = ai_ry = 0.0f;
    if (coll == NULL || lines == NULL || verts == NULL) {
        return;
    }
    count = coll->line_count;
    for (i = 0; i < count; i++) {
        const MapLine* line = lines[i].x0;
        const CollVtx* a;
        const CollVtx* b;
        const CollVtx* p;
        if (line == NULL || !(line->hi_flags & 0x0001) || !(line->lo_flags & 0x0200)) {
            continue;
        }
        if (stage_info.grkind == Gr_Kind_PStadium && i != 0x33 && i != 0x36) {
            continue;
        }
        a = &verts[line->v0_idx];
        b = &verts[line->v1_idx];
        p = absd((double) a->pos.x - (double) center) > absd((double) b->pos.x - (double) center) ? a : b;
        if (p->pos.x > center) {
            ai_rx = p->pos.x;
            ai_ry = p->pos.y;
        } else {
            ai_lx = p->pos.x;
            ai_ly = p->pos.y;
        }
    }
}

/* The opponent: starting at the player who last hit this fighter (or at the first fighter), the
 * first one down the fighter list that is another player's, not asleep, and not a team mate.
 * Giant Melee skips the team check, as the original does. */
static Fighter* find_opponent(Fighter* fp)
{
    HSD_GObj* gobj = NULL;
    const int slot = fp->dmg.x18c4_source_ply;
    if (slot >= 0 && slot < 6) {
        gobj = Player_GetPtrForSlot(slot)->player_entity[0];
    }
    if (gobj == NULL) {
        gobj = HSD_GObjPLinkHead[HSD_GOBJ_PLINK_FIGHTER];
    }
    for (; gobj != NULL; gobj = gobj->next) {
        Fighter* other = gobj->user_data;
        if (other == NULL || other == fp || other->player_idx == fp->player_idx ||
            other->motion_id == ftCo_MS_Sleep)
        {
            continue;
        }
        if (gm_GetCurrentGameMode() == GM_GIANT_VS || !gm_8016B168() || other->team != fp->team) {
            return other;
        }
    }
    /* The original, when it started at the last attacker and found nobody, went on with a stale
     * register (the fighter's own object) as the opponent. Here there is no opponent. */
    return NULL;
}

int mu_20xx_ai_enabled(void)
{
    static int forced = -1;
    if (forced < 0) {
        const char* v = getenv("MELEE_TEST_20XX_AI");
        forced = v != NULL && *v != '\0' && *v != '0';
    }
    if (forced) {
        /* Tests only. Same limits as the option: not online, not in replay playback. */
        return !(mu_online_active() || mu_online_pending() || mu_replay_abi_active());
    }
    /* The plain "20XX CPUs" option (Game tab), with or without a 20XX TE save; in replay playback
     * the host passes the recorded word, so the replay plays back as it was played. */
    return mu_option2_offline(MU_TE2_20XX_CPUS);
}

unsigned int mu_20xx_ai(void* fighter, unsigned int buttons)
{
    Fighter* fp = fighter;
    if ((int) fp->kind == Ft_Kind_Nana || (int) fp->kind >= 0x1B) {
        return buttons;
    }
    ai_fp = fp;
    ai_st = state_of(fp);
    if (mu_gm_engine_state()->unk_8 == 0) {
        /* first frame of the scene */
        ai_st->timer = 0;
        ai_st->act = 0;
    }
    if (gm_GetCurrentGameMode() == GM_ALLSTAR) {
        fp->cpu.kind = (CpuKind) hp_allstar_cpu_kind;
    }
    if (fp->is_sub_fighter && (int) fp->kind != Ft_Kind_Seak && (int) fp->kind != Ft_Kind_Zelda) {
        fp->cpu.kind = (CpuKind) hp_sub_fighter_cpu_kind;
    }
    if ((int) fp->cpu.kind != 4) {
        return buttons;   /* only the normal VS CPU type */
    }
    ai_btn = buttons;
    trace_event(EV_ACTIVE);
    if ((int) fp->motion_id < ftCo_MS_Wait) {
        /* dead, asleep or on the revival platform */
        clear_act();
        return ai_btn;
    }
    find_ledges();
    ai_opp = find_opponent(fp);
    if (ai_opp != NULL) {
        ai_dx = (double) fp->cur_pos.x - (double) ai_opp->cur_pos.x;
        ai_dy = (double) fp->cur_pos.y - (double) ai_opp->cur_pos.y;
    } else {
        ai_dx = ai_dy = 0.0;
    }
    return ai_think();
}
