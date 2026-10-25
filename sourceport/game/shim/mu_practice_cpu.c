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

/* Nonzero when the practice CPU set this fighter's controller for the frame. */
int mu_practice_cpu_drive(void* fighter, MuLabInput* out)
{
    Fighter* fp = fighter;
    const MuPcConfig* c;
    HSD_GObj* opp_gobj = NULL;
    Fighter* opp = NULL;
    MuPcView v;
    MuPcInput in;
    int slot, k, driven;
    if (fp == NULL || fp->is_sub_fighter || !ftCo_IsCpuControlled(fp) || !pc_allowed()) return 0;
    c = pc_settings();
    if (!c->enabled) return 0;
    slot = fp->player_idx < 6 ? fp->player_idx : 0;
    for (k = 0; k < 4 && opp == NULL; k++) {   /* the first human player */
        HSD_GObj* g;
        if (k == fp->player_idx || Player_GetPlayerSlotType(k) != Gm_PKind_Human) continue;
        g = Player_GetEntity(k);
        if (g != NULL && g->user_data != NULL) {
            opp_gobj = g;
            opp = g->user_data;
        }
    }
    (void) opp_gobj;
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
    driven = mu_pc_frame(c, &pc_state[slot], &v, &in);
    {
        unsigned char payload[2], response[64];
        unsigned int got = 0;
        static int last_plan = -2, last_driven = -1;
        const int changed = pc_state[slot].plan != last_plan || driven != last_driven;
        if (changed || (v.frame % 15u) == 0) {   /* the Practice page shows what it is doing */
            payload[0] = (unsigned char) driven;
            payload[1] = (unsigned char) pc_state[slot].plan;
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
              MU_EXCLUDE(pc_logs))
