/* Practice CPU, Source Port side: the view of the CPU and its opponent from the Fighter structs, the
 * stage's ledges from its collision lines, the floor below from the game's own floor query, and the
 * model's controller handed to Fighter_procInput through the lab's input path (mu_lab_input).
 *
 * The settings come from the host (CMD_PRACTICE_CPU, source_host.cpp) once a frame. The model and
 * its state are ordinary game data here, so savestates and the lab's recordings keep them.
 * Offline Training only: never online, while an online match is pending, or in replay playback. */
#include <dolphin/os.h>
#include <melee/ft/fighter.h>
#include <melee/ft/forward.h>
#include <melee/ft/types.h>
#include <melee/ft/kinds/ftCommon/forward.h>
#include <melee/ft/kinds/ftCommon/ftCo_0A01.h>
#include <melee/gm/forward.h>
#include <melee/gm/gm_1A3F.h>
#include <melee/gm/gmvs.h>
#include <melee/gr/forward.h>
#include <melee/gr/ground.h>
#include <melee/gr/types.h>
#include <melee/mp/mplib.h>
#include <melee/mp/types.h>
#include <melee/pl/player.h>
#include <sysdolphin/baselib/gobj.h>

#include "mu_native.h"
#include "mu_practice_cpu_impl.inl"
#include "mu_ghost_ai.h"

/* The model's motion ids are the game's: checked here so a decomp renumbering cannot slip by. */
_Static_assert(ftCo_MS_RebirthWait == MU_PC_MS_REBIRTH_WAIT, "motion ids");
_Static_assert(ftCo_MS_Wait == MU_PC_MS_WAIT, "motion ids");
_Static_assert(ftCo_MS_WalkFast == MU_PC_MS_WALK_FAST, "motion ids");
_Static_assert(ftCo_MS_Dash == MU_PC_MS_DASH && ftCo_MS_Run == MU_PC_MS_RUN, "motion ids");
_Static_assert(ftCo_MS_KneeBend == MU_PC_MS_KNEE_BEND && ftCo_MS_JumpF == MU_PC_MS_JUMP_F, "motion ids");
_Static_assert(ftCo_MS_JumpAerialB == MU_PC_MS_JUMP_AERIAL_B && ftCo_MS_Fall == MU_PC_MS_FALL, "motion ids");
_Static_assert(ftCo_MS_FallAerialB == MU_PC_MS_FALL_AERIAL_B && ftCo_MS_FallSpecial == MU_PC_MS_FALL_SPECIAL, "motion ids");
_Static_assert(ftCo_MS_FallSpecialB == MU_PC_MS_FALL_SPECIAL_B && ftCo_MS_DamageFall == MU_PC_MS_DAMAGE_FALL, "motion ids");
_Static_assert(ftCo_MS_Squat == MU_PC_MS_SQUAT && ftCo_MS_SquatRv == MU_PC_MS_SQUAT_RV, "motion ids");
_Static_assert(ftCo_MS_Landing == MU_PC_MS_LANDING && ftCo_MS_LandingFallSpecial == MU_PC_MS_LANDING_FALL_SPECIAL, "motion ids");
_Static_assert(ftCo_MS_Attack11 == MU_PC_MS_ATTACK_11 && ftCo_MS_AttackAirN == MU_PC_MS_ATTACK_AIR_N, "motion ids");
_Static_assert(ftCo_MS_AttackAirLw == MU_PC_MS_ATTACK_AIR_LW && ftCo_MS_LandingAirLw == MU_PC_MS_LANDING_AIR_LW, "motion ids");
_Static_assert(ftCo_MS_DamageHi1 == MU_PC_MS_DAMAGE_HI1 && ftCo_MS_DamageFlyRoll == MU_PC_MS_DAMAGE_FLY_ROLL, "motion ids");
_Static_assert(ftCo_MS_GuardOn == MU_PC_MS_GUARD_ON && ftCo_MS_Guard == MU_PC_MS_GUARD, "motion ids");
_Static_assert(ftCo_MS_GuardOff == MU_PC_MS_GUARD_OFF && ftCo_MS_GuardSetOff == MU_PC_MS_GUARD_SET_OFF, "motion ids");
_Static_assert(ftCo_MS_GuardReflect == MU_PC_MS_GUARD_REFLECT, "motion ids");
_Static_assert(ftCo_MS_DownBoundU == MU_PC_MS_DOWN_BOUND_U && ftCo_MS_DownWaitU == MU_PC_MS_DOWN_WAIT_U, "motion ids");
_Static_assert(ftCo_MS_DownBoundD == MU_PC_MS_DOWN_BOUND_D && ftCo_MS_DownWaitD == MU_PC_MS_DOWN_WAIT_D, "motion ids");
_Static_assert(ftCo_MS_Catch == MU_PC_MS_CATCH && ftCo_MS_CatchDash == MU_PC_MS_CATCH_DASH, "motion ids");
_Static_assert(ftCo_MS_CliffCatch == MU_PC_MS_CLIFF_CATCH && ftCo_MS_CliffWait == MU_PC_MS_CLIFF_WAIT, "motion ids");
_Static_assert(ftCo_MS_Entry == MU_PC_MS_ENTRY, "motion ids");
_Static_assert(Gr_Kind_Story == 0x0A && Gr_Kind_Izumi == 0x0C && Gr_Kind_PStadium == 0x10, "stage ids");
_Static_assert(Gr_Kind_OldPupupu == 0x1C && Gr_Kind_Battle == 0x24 && Gr_Kind_Last == 0x25, "stage ids");
_Static_assert(sizeof(MuPcConfig) <= 256, "practice CPU settings fit one host reply");

int mu_online_active(void);
int mu_online_pending(void);
int mu_replay_abi_active(void);
int mu_online_abi_command(unsigned int command, const unsigned char* payload, unsigned int size,
                          unsigned char* response, unsigned int capacity, unsigned int* response_size);
char* getenv(const char* name);

#define CMD_PRACTICE_CPU 0xF6   /* reply: MuPcConfig; payload when sent: u8 driving, u8 plan */

static MuPcConfig pc_config;
static int pc_config_frame = -1;
static MuPcState pc_state[6];
static void* pc_state_owner[6];
static int pc_logs;

static const MuPcConfig* pc_settings(void)
{
    const int now = (int) gm_GetFrameCount();
    if (now != pc_config_frame) {
        static unsigned char response[4096];
        unsigned int got = 0;
        pc_config_frame = now;
        if (mu_online_abi_command(CMD_PRACTICE_CPU, NULL, 0, response, sizeof response, &got) == 0 &&
            got == sizeof pc_config && response[0] == MU_PC_CONFIG_VERSION)
        {
            __builtin_memcpy(&pc_config, response, sizeof pc_config);
        } else {
            pc_config.enabled = 0;
        }
    }
    return &pc_config;
}

static int pc_can_act(const Fighter* fp)
{
    const int m = (int) fp->motion_id;
    if (fp->dmg.x195c_hitlag_frames > 0.0f || fp->x221C_b6) return 0;
    if (fp->allow_interrupt) return 1;
    return (m >= ftCo_MS_Wait && m <= ftCo_MS_Run) || (m >= ftCo_MS_JumpAerialF && m <= ftCo_MS_FallAerialB) ||
           m == ftCo_MS_Squat || m == ftCo_MS_SquatWait || m == ftCo_MS_DamageFall || m == ftCo_MS_Guard;
}

static void pc_read(const Fighter* fp, MuPcFighter* f)
{
    __builtin_memset(f, 0, sizeof *f);
    if (fp == NULL) return;
    f->present = 1;
    f->kind = (mu_pc_s32) fp->kind;
    f->motion = (mu_pc_s32) fp->motion_id;
    f->facing = fp->facing_dir;
    f->x = fp->cur_pos.x;
    f->y = fp->cur_pos.y;
    f->vx = fp->self_vel.x + fp->x8c_kb_vel.x;
    f->vy = fp->self_vel.y + fp->x8c_kb_vel.y;
    f->airborne = fp->ground_or_air == GA_Air;
    f->percent = fp->dmg.x1830_percent;
    f->shield = fp->shield_health;
    f->hitlag = fp->dmg.x195c_hitlag_frames > 0.0f;
    f->hitstun = fp->x221C_b6 != 0;
    f->jumps_left = (mu_pc_u8) (fp->co_attrs.max_jumps > fp->x1968_jumpsUsed ? fp->co_attrs.max_jumps - fp->x1968_jumpsUsed : 0);
    f->can_act = (mu_pc_u8) pc_can_act(fp);
}

/* The main stage's ledges from the collision lines: of every grabbable floor line, its end farther
 * from the middle. Pokemon Stadium counts only its two main floor lines (as the game's CPU does). */
static void pc_ledges(float* lx, float* ly)
{
    const MapCollData* coll = mpLib_8004D164();
    const CollLine* lines = mpGetGroundCollLine();
    const CollVtx* verts = mpGetGroundCollVtx();
    float best = 0.0f, best_y = 0.0f;
    int i;
    mu_pc_stage_ledge((int) stage_info.grkind, lx, ly);   /* fallback */
    if (coll == NULL || lines == NULL || verts == NULL) return;
    for (i = 0; i < coll->line_count; i++) {
        const MapLine* line = lines[i].x0;
        const CollVtx* a;
        const CollVtx* b;
        const CollVtx* p;
        float ax, bx;
        if (line == NULL || !(line->hi_flags & 0x0001) || !(line->lo_flags & 0x0200)) continue;
        if (stage_info.grkind == Gr_Kind_PStadium && i != 0x33 && i != 0x36) continue;
        a = &verts[line->v0_idx];
        b = &verts[line->v1_idx];
        ax = a->pos.x < 0.0f ? -a->pos.x : a->pos.x;
        bx = b->pos.x < 0.0f ? -b->pos.x : b->pos.x;
        p = ax > bx ? a : b;
        if ((ax > bx ? ax : bx) > best) {
            best = ax > bx ? ax : bx;
            best_y = p->pos.y;
        }
    }
    if (best > 10.0f) {
        *lx = best;
        *ly = best_y;
    }
}

/* Frames until the CPU's collision box meets a floor along its path, -1 when none within 20. */
static int pc_land_frames(const Fighter* fp)
{
    const float vx = fp->self_vel.x + fp->x8c_kb_vel.x;
    const float vy = fp->self_vel.y + fp->x8c_kb_vel.y;
    const float ax = fp->coll_data.cur_pos.x + fp->coll_data.ecb.bottom.x;
    const float ay = fp->coll_data.cur_pos.y + fp->coll_data.ecb.bottom.y + 1.0f;
    Vec3 floor_pos, normal;
    int line = -1;
    u32 flags = 0;
    if (fp->ground_or_air != GA_Air || vy >= 0.0f) return -1;
    if (!mpCheckFloor(ax, ay, ax + vx * 20.0f, ay + vy * 20.0f - 2.0f, 0.0f, &floor_pos, &line, &flags, &normal, -1, -1, -1,
                      NULL, NULL))
    {
        return -1;
    }
    return (int) ((ay - floor_pos.y) / -vy);
}

static int pc_allowed(void)
{
    static int any_mode = -1;
    if (any_mode < 0) any_mode = getenv("MELEE_PC_ANY_MODE") != NULL;   /* tests only: any offline match */
    return (gm_GetCurrentGameMode() == GM_TRAINING || any_mode) && !mu_online_active() && !mu_online_pending() &&
           !mu_replay_abi_active();
}

static int pc_drive(Fighter* fp, Fighter* opp, const MuPcConfig* c, MuLabInput* out, int report);
static int ghost_drive(Fighter* fp, Fighter* opp, const MuPcConfig* c, MuLabInput* out);

/* Nonzero when the practice CPU set this fighter's controller for the frame. */
int mu_practice_cpu_drive(void* fighter, MuLabInput* out)
{
    Fighter* fp = fighter;
    const MuPcConfig* c;
    Fighter* opp = NULL;
    int k;
    if (fp != NULL && !fp->is_sub_fighter && fp->player_idx == 0 && pc_allowed()) {
        /* MELEE_TEST_AI_P1=<pro> (tests only): player 1 is a pro AI too, against player 2, so two AIs
         * (or an AI and the practice CPU) can play whole matches in a hidden run. */
        static int test_pro = -2;
        if (test_pro == -2) {
            const char* v = getenv("MELEE_TEST_AI_P1");
            test_pro = v != NULL ? (int) (v[0] - '0') : -1;
        }
        if (test_pro >= 0) {
            HSD_GObj* g = Player_GetEntity(1);
            MuPcConfig t;
            __builtin_memset(&t, 0, sizeof t);
            t.enabled = 1;
            t.preset = (mu_pc_u8) MU_PC_PRESET_PRO(test_pro);
            mu_pc_defaults(&t);
            t.enabled = 1;
            t.preset = (mu_pc_u8) MU_PC_PRESET_PRO(test_pro);
            return g != NULL && g->user_data != NULL && pc_drive(fp, g->user_data, &t, out, 0);
        }
    }
    if (fp == NULL || fp->is_sub_fighter || !ftCo_IsCpuControlled(fp) || !pc_allowed()) return 0;
    c = pc_settings();
    if (!c->enabled) return 0;
    {
        /* Tests: MELEE_TEST_PC_PORT=<index> drives only that port, against the other fighter even when it is
         * a CPU (a pro AI against the game's level 9 CPU in a VS match, MELEE_PC_ANY_MODE). */
        static int only = -2;
        if (only == -2) {
            const char* v = getenv("MELEE_TEST_PC_PORT");
            only = v != NULL ? (int) (v[0] - '0') : -1;
        }
        if (only >= 0 && fp->player_idx != only) return 0;
        for (k = 0; k < 4 && opp == NULL; k++) {   /* the first human player (tests: any other player) */
            HSD_GObj* g;
            if (k == fp->player_idx) continue;
            if (only < 0 && Player_GetPlayerSlotType(k) != Gm_PKind_Human) continue;
            g = Player_GetEntity(k);
            if (g != NULL && g->user_data != NULL) opp = g->user_data;
        }
    }
    return pc_drive(fp, opp, c, out, 1);
}

/* The model for any fighter against a given opponent with given settings (replay takeover's
 * defenders, shim/mu_replay_defend.c). Nonzero when it set the controller. */
int mu_practice_cpu_drive_as(void* fighter, void* opponent, const MuPcConfig* c, MuLabInput* out)
{
    Fighter* fp = fighter;
    if (fp == NULL || fp->is_sub_fighter || c == NULL || !c->enabled) return 0;
    return pc_drive(fp, opponent, c, out, 0);
}

/* ---- the pro AI (mu_ghost_ai.h): the host's network plays, from what a replay would record ---- */
#define CMD_GHOST_AI 0xEA
void mu_replay_fighter_flags(const Fighter* fp, unsigned char out[5]);

static void ghost_read(const Fighter* fp, MuGhostFighter* g)
{
    u32 misc;
    __builtin_memset(g, 0, sizeof *g);
    if (fp == NULL) return;
    __builtin_memcpy(&misc, &fp->mv, 4);
    g->num[0] = fp->cur_pos.x;
    g->num[1] = fp->cur_pos.y;
    g->num[2] = fp->facing_dir;
    g->num[3] = fp->dmg.x1830_percent;
    g->num[4] = fp->shield_health;
    g->num[5] = (float) Player_GetStocks(fp->player_idx);
    g->num[6] = fp->cur_anim_frame;
    __builtin_memcpy(&g->num[7], &misc, 4);
    g->num[8] = fp->ground_or_air == GA_Air ? 1.0f : 0.0f;
    g->num[9] = (float) (fp->co_attrs.max_jumps - fp->x1968_jumpsUsed);
    g->num[10] = fp->self_vel.x;
    g->num[11] = fp->self_vel.y;
    g->num[12] = fp->x8c_kb_vel.x;
    g->num[13] = fp->x8c_kb_vel.y;
    g->num[14] = fp->gr_vel;
    g->num[15] = fp->dmg.x195c_hitlag_frames;
    g->motion = (unsigned short) fp->motion_id;
    g->kind = (unsigned char) fp->kind;
    mu_replay_fighter_flags(fp, g->flags);
}

/* What each pro AI fighter really held last frame (pc_drive, after the safety net). */
static MuGhostAct ghost_applied[4];

/* The pro for this fighter: the one picked, or (MU_PC_PRESET_AI) the pro who plays its character. */
static int ghost_pro(const Fighter* fp, const MuPcConfig* c)
{
    if (c->preset == MU_PC_PRESET_AI) {
        switch (fp->kind) {
        case Ft_Kind_Falco: return 0;   /* Mang0 */
        case Ft_Kind_Mars: return 1;    /* Zain */
        case Ft_Kind_Purin: return 2;   /* Hungrybox */
        case Ft_Kind_Fox: return 3;     /* Cody Schwab */
        case Ft_Kind_Yoshi: return 4;   /* aMSa */
        default: return -1;
        }
    }
    return (c->preset & 0x80u) ? (int) (c->preset & 0x7Fu) : -1;
}

static int ghost_stage(void)
{
    switch (stage_info.grkind) {
    case Gr_Kind_Izumi: return 2;
    case Gr_Kind_PStadium: return 3;
    case Gr_Kind_Story: return 8;
    case Gr_Kind_OldPupupu: return 28;
    case Gr_Kind_Battle: return 31;
    case Gr_Kind_Last: return 32;
    default: return 0;
    }
}

static int ghost_drive(Fighter* fp, Fighter* opp, const MuPcConfig* c, MuLabInput* out)
{
    static void* owner[4];
    static unsigned char response[4096];   /* the host's minimum reply capacity */
    MuGhostObs obs;
    MuGhostAct act;
    unsigned int got = 0;
    const int pro = ghost_pro(fp, c);
    const int slot = fp->player_idx & 3;
    if (pro < 0 || opp == NULL) return 0;
    __builtin_memset(&obs, 0, sizeof obs);
    obs.version = MU_GHOST_VERSION;
    obs.pro = (unsigned char) pro;
    obs.slot = (unsigned char) slot;
    obs.reset = owner[slot] != fp->gobj;
    owner[slot] = fp->gobj;
    obs.stage = (short) ghost_stage();
    obs.frame = (unsigned int) gm_GetFrameCount();
    obs.applied = ghost_applied[slot];
    ghost_read(fp, &obs.self);
    ghost_read(opp, &obs.opp);
    if (mu_online_abi_command(CMD_GHOST_AI, (const unsigned char*) &obs, sizeof obs, response, sizeof response, &got) != 0 ||
        got != sizeof act)
    {
        return 0;
    }
    __builtin_memcpy(&act, response, sizeof act);
    out->lx = act.lx;
    out->ly = act.ly;
    out->cx = act.cx;
    out->cy = act.cy;
    out->l = act.l;
    out->r = act.r;
    out->buttons = act.buttons;
    return 1;
}

static int pc_drive(Fighter* fp, Fighter* opp, const MuPcConfig* c, MuLabInput* out, int report)
{
    MuPcView v;
    MuPcInput in;
    int slot, driven;
    slot = fp->player_idx < 6 ? fp->player_idx : 0;
    __builtin_memset(&v, 0, sizeof v);
    pc_read(fp, &v.cpu);
    pc_read(opp, &v.opp);
    if (!v.opp.present) v.opp = v.cpu;
    pc_ledges(&v.ledge_x, &v.ledge_y);
    v.land_frames = pc_land_frames(fp);
    v.frame = (mu_pc_u32) gm_GetFrameCount();
    if (pc_state_owner[slot] != fp->gobj) {
        pc_state_owner[slot] = fp->gobj;
        mu_pc_reset(&pc_state[slot], 0x9E3779B9u ^ ((mu_pc_u32) slot * 2654435761u));
    }
    if (ghost_drive(fp, opp, c, out)) {
        /* The safety net: off the stage, falling at or below the ledge and free to act, the practice CPU's own recovery
         * brings the pro AI back (its network still ran, so its memory of the match goes on).
         * MELEE_GHOST_NO_NET (tests) leaves it to the network alone. */
        static int no_net = -1;
        const float ax = v.cpu.x < 0.0f ? -v.cpu.x : v.cpu.x;
        if (no_net < 0) no_net = getenv("MELEE_GHOST_NO_NET") != NULL;
        /* Off the stage near or below the ledge, no air dodge: it spends every jump and leaves the
         * fighter helpless, and the network's ledge air dodges missed the stage. */
        if (!no_net && v.cpu.airborne && ax > v.ledge_x - 2.0f && v.cpu.y < v.ledge_y + 20.0f) {
            if ((out->buttons & (MU_PC_L | MU_PC_R | MU_PC_Z)) && getenv("MELEE_GHOST_LOG") != NULL) {
                OSReport("[ghost] P%d air dodge dropped at %.0f,%.0f buttons %x\n", slot + 1, v.cpu.x, v.cpu.y, out->buttons);
            }
            out->buttons &= ~(MU_PC_L | MU_PC_R | MU_PC_Z);
            out->l = out->r = 0.0f;
        }
        if (!no_net && v.cpu.airborne && !v.cpu.hitstun && !v.cpu.hitlag && ((ax > v.ledge_x + 2.0f && v.cpu.y < v.ledge_y + 5.0f && v.cpu.vy < 0.0f) ||
             (ax > v.ledge_x - 40.0f && v.cpu.y < v.ledge_y - 10.0f)))
        {
            MuPcConfig rc = *c;
            rc.enabled = 1;
            rc.freeze = 0;
            if (rc.recovery == MU_PC_RECOVER_GAME) rc.recovery = MU_PC_RECOVER_MIX;
            static int net_logs;
            if (getenv("MELEE_GHOST_LOG") != NULL && (v.frame % 6u) == 0 && net_logs < 400) {
                net_logs++;
                OSReport("[ghost] P%d net at %.0f,%.0f motion %d jumps %d ledge %.0f,%.0f\n", slot + 1, v.cpu.x,
                         v.cpu.y, (int) v.cpu.motion, (int) v.cpu.jumps_left, v.ledge_x, v.ledge_y);
            }
            pc_clear(&in);   /* only the recovery: the model's other habits (an air dodge after hitstun) dropped it */
            if (pc_recover(&rc, &pc_state[slot], &v, &in)) {
                out->lx = in.lx;
                out->ly = in.ly;
                out->cx = in.cx;
                out->cy = in.cy;
                out->l = in.l;
                out->r = in.r;
                out->buttons = in.buttons;
            }
        }
        {
            MuGhostAct* ap = &ghost_applied[fp->player_idx & 3];
            ap->lx = out->lx;
            ap->ly = out->ly;
            ap->cx = out->cx;
            ap->cy = out->cy;
            ap->l = out->l;
            ap->r = out->r;
            ap->buttons = out->buttons;
        }
        if (report && (v.frame % 15u) == 0) {
            static unsigned char response[4096];
            unsigned char payload[3] = {1, 0, 0};
            unsigned int got = 0;
            payload[2] = (unsigned char) fp->kind;
            mu_online_abi_command(CMD_PRACTICE_CPU, payload, sizeof payload, response, sizeof response, &got);
        }
        return 1;
    }
    driven = mu_pc_frame(c, &pc_state[slot], &v, &in);
    if (report) {
        static unsigned char response[4096];   /* the host's minimum reply capacity */
        unsigned char payload[3];
        unsigned int got = 0;
        static int last_plan = -2, last_driven = -1;
        const int changed = pc_state[slot].plan != last_plan || driven != last_driven;
        if (changed || (v.frame % 15u) == 0) {   /* the Practice page shows what it is doing */
            payload[0] = (unsigned char) driven;
            payload[1] = (unsigned char) pc_state[slot].plan;
            payload[2] = (unsigned char) v.cpu.kind;   /* the character, for its move names */
            mu_online_abi_command(CMD_PRACTICE_CPU, payload, sizeof payload, response, sizeof response, &got);
        }
        if (changed) {
            last_plan = pc_state[slot].plan;
            last_driven = driven;
            if (getenv("MELEE_PC_LOG") != NULL && pc_logs < 400) {
                pc_logs++;
                OSReport("[practice cpu] frame %d plan %s, motion %d, x %.1f, opp motion %d\n", (int) v.frame,
                         mu_pc_plan_name(pc_state[slot].plan), (int) v.cpu.motion, v.cpu.x, (int) v.opp.motion);
            }
        }
    }
    if (!driven) return 0;
    out->lx = in.lx;
    out->ly = in.ly;
    out->cx = in.cx;
    out->cy = in.cy;
    out->l = in.l;
    out->r = in.r;
    out->buttons = in.buttons;
    return 1;
}

/* Snapshot exclusions: the settings copy and the log count are not game state. */
MU_EXCLUSIONS(practice_cpu,
              MU_EXCLUDE(pc_config),
              MU_EXCLUDE(pc_config_frame),
              MU_EXCLUDE(pc_logs),
              MU_EXCLUDE(ghost_applied))
