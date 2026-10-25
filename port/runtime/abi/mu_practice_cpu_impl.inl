/* Practice CPU decision model (see mu_practice_cpu.h). Included once by each engine's adapter:
 * sourceport/game/shim/mu_practice_cpu.c (C) and port/runtime/hle/practice_cpu.cpp (C++), so it is
 * written in the subset both accept. */
#include "mu_practice_cpu.h"

/* Common action states (ftCommon motion ids; the Source Port asserts these at build time). */
enum {
    MU_PC_MS_REBIRTH_WAIT = 13,
    MU_PC_MS_WAIT = 14,
    MU_PC_MS_WALK_FAST = 17,
    MU_PC_MS_TURN = 18,
    MU_PC_MS_DASH = 20,
    MU_PC_MS_RUN = 21,
    MU_PC_MS_KNEE_BEND = 24,
    MU_PC_MS_JUMP_F = 25,
    MU_PC_MS_JUMP_AERIAL_B = 28,
    MU_PC_MS_FALL = 29,
    MU_PC_MS_FALL_AERIAL_B = 34,
    MU_PC_MS_FALL_SPECIAL = 35,
    MU_PC_MS_FALL_SPECIAL_B = 37,
    MU_PC_MS_DAMAGE_FALL = 38,
    MU_PC_MS_SQUAT = 39,
    MU_PC_MS_SQUAT_RV = 41,
    MU_PC_MS_LANDING = 42,
    MU_PC_MS_LANDING_FALL_SPECIAL = 43,
    MU_PC_MS_ATTACK_11 = 44,
    MU_PC_MS_ATTACK_AIR_N = 65,
    MU_PC_MS_ATTACK_AIR_LW = 69,
    MU_PC_MS_LANDING_AIR_LW = 74,
    MU_PC_MS_DAMAGE_HI1 = 75,
    MU_PC_MS_DAMAGE_FLY_ROLL = 91,
    MU_PC_MS_GUARD_ON = 178,
    MU_PC_MS_GUARD = 179,
    MU_PC_MS_GUARD_OFF = 180,
    MU_PC_MS_GUARD_SET_OFF = 181,
    MU_PC_MS_GUARD_REFLECT = 182,
    MU_PC_MS_DOWN_BOUND_U = 183,
    MU_PC_MS_DOWN_WAIT_U = 184,
    MU_PC_MS_DOWN_BOUND_D = 191,
    MU_PC_MS_DOWN_WAIT_D = 192,
    MU_PC_MS_CATCH = 212,
    MU_PC_MS_CATCH_DASH = 214,
    MU_PC_MS_CLIFF_CATCH = 252,
    MU_PC_MS_CLIFF_WAIT = 253,
    MU_PC_MS_ENTRY = 322,
    MU_PC_MS_SPECIAL_FIRST = 341
};

enum {
    PC_P_NONE,
    PC_P_IDLE,
    PC_P_WALK,
    PC_P_DASH_DANCE,
    PC_P_APPROACH,
    PC_P_RUN_AWAY,
    PC_P_HOP,
    PC_P_WAVEDASH,
    PC_P_CROUCH,
    PC_P_GROUND_ATTACK,
    PC_P_AERIAL,
    PC_P_SPECIAL,
    PC_P_SHIELD,
    PC_P_SPOT_DODGE,
    PC_P_ROLL,
    PC_P_JUMP_AWAY,
    PC_P_OOS,
    PC_P_GETUP,
    PC_P_LEDGE,
    PC_P_AFTER,
    PC_P_COUNT
};

static const char* const pc_plan_names[PC_P_COUNT] = {
    "none", "idle", "walk", "dash dance", "approach", "run away", "hop", "wavedash", "crouch",
    "ground attack", "aerial", "special", "shield", "spot dodge", "roll", "jump away",
    "out of shield", "getup", "ledge", "after hitstun"};

const char* mu_pc_plan_name(int plan)
{
    return plan >= 0 && plan < PC_P_COUNT ? pc_plan_names[plan] : "?";
}

static mu_pc_u32 pc_rand(MuPcState* s, mu_pc_u32 n)
{
    s->rng = s->rng * 1103515245u + 12345u;
    return n ? ((s->rng >> 16) & 0x7FFFu) % n : 0;
}

/* A weighted pick from a group of odds; -1 when every weight is 0. */
static int pc_pick(MuPcState* s, const mu_pc_u8* odds, int count)
{
    mu_pc_u32 total = 0, roll;
    int i;
    for (i = 0; i < count; i++) total += odds[i];
    if (total == 0) return -1;
    roll = pc_rand(s, total);
    for (i = 0; i < count; i++) {
        if (roll < odds[i]) return i;
        roll -= odds[i];
    }
    return count - 1;
}

static int pc_chance(MuPcState* s, int percent)
{
    return percent > 0 && (int) pc_rand(s, 100) < percent;
}

void mu_pc_reset(MuPcState* s, mu_pc_u32 seed)
{
    char* p = (char*) s;
    unsigned int i;
    for (i = 0; i < sizeof *s; i++) p[i] = 0;
    s->rng = seed ? seed : 0x2545F491u;
    s->last_logged = -1;
}

static int pc_between(int v, int lo, int hi) { return v >= lo && v <= hi; }
static float pc_sign(float v) { return v < 0.0f ? -1.0f : 1.0f; }
static float pc_abs(float v) { return v < 0.0f ? -v : v; }

static int pc_is_attack(int m)
{
    return pc_between(m, MU_PC_MS_ATTACK_11, MU_PC_MS_ATTACK_AIR_LW) || m == MU_PC_MS_CATCH ||
           m == MU_PC_MS_CATCH_DASH || m >= MU_PC_MS_SPECIAL_FIRST;
}
static int pc_is_down(int m)
{
    return pc_between(m, MU_PC_MS_DOWN_BOUND_U, MU_PC_MS_DOWN_WAIT_U) ||
           pc_between(m, MU_PC_MS_DOWN_BOUND_D, MU_PC_MS_DOWN_WAIT_D);
}
static int pc_is_down_wait(int m) { return m == MU_PC_MS_DOWN_WAIT_U || m == MU_PC_MS_DOWN_WAIT_D; }
static int pc_is_tumble(const MuPcFighter* f)
{
    return f->airborne && (f->hitstun || f->motion == MU_PC_MS_DAMAGE_FALL ||
                           pc_between(f->motion, MU_PC_MS_DAMAGE_HI1, MU_PC_MS_DAMAGE_FLY_ROLL));
}
static int pc_is_jumping(int m) { return pc_between(m, MU_PC_MS_JUMP_F, MU_PC_MS_JUMP_AERIAL_B); }
static int pc_is_aerial_attack(int m) { return pc_between(m, MU_PC_MS_ATTACK_AIR_N, MU_PC_MS_ATTACK_AIR_LW); }

static void pc_start(MuPcState* s, int plan, int dir, int arg, int len)
{
    s->plan = plan;
    s->plan_t = 0;
    s->plan_dir = dir;
    s->plan_arg = arg;
    s->plan_len = len;
}

/* Toward the opponent (+1 right, -1 left); facing when they overlap. */
static int pc_toward(const MuPcView* v)
{
    const float dx = v->opp.x - v->cpu.x;
    if (dx > 0.5f) return 1;
    if (dx < -0.5f) return -1;
    return (int) pc_sign(v->cpu.facing);
}

static int pc_offstage(const MuPcView* v)
{
    const float ax = pc_abs(v->cpu.x);
    return v->cpu.airborne && (ax > v->ledge_x + 3.0f || (v->cpu.y < v->ledge_y - 8.0f && ax > v->ledge_x - 12.0f));
}

/* The hit's DI as a stick position. */
static void pc_di_stick(int di, int toward, float cpu_x, float* lx, float* ly)
{
    const float center = cpu_x > 0.0f ? -1.0f : 1.0f;
    *lx = 0.0f;
    *ly = 0.0f;
    switch (di) {
    case MU_PC_DI_IN: *lx = (float) toward; break;
    case MU_PC_DI_OUT: *lx = (float) -toward; break;
    case MU_PC_DI_SURVIVAL: *lx = 0.7f * center; *ly = 0.7f; break;
    case MU_PC_DI_DOWN_IN: *lx = 0.7f * (float) toward; *ly = -0.7f; break;
    case MU_PC_DI_DOWN_OUT: *lx = -0.7f * (float) toward; *ly = -0.7f; break;
    default: break;
    }
}

static void pc_clear(MuPcInput* o)
{
    o->lx = o->ly = o->cx = o->cy = o->l = o->r = 0.0f;
    o->buttons = 0;
}

static void pc_shield(MuPcInput* o)
{
    o->buttons |= MU_PC_R;
    o->r = 1.0f;
}

static void pc_air_dodge(MuPcInput* o, float lx, float ly)
{
    o->buttons |= MU_PC_L;
    o->l = 1.0f;
    o->lx = lx;
    o->ly = ly;
}

/* One aerial: c-stick direction (or A for a neutral air), relative to facing. */
static void pc_aerial_press(int which, float facing, MuPcInput* o)
{
    switch (which) {
    case MU_PC_ATK_FAIR: o->cx = facing; break;
    case MU_PC_ATK_BAIR: o->cx = -facing; break;
    case MU_PC_ATK_UAIR: o->cy = 1.0f; break;
    case MU_PC_ATK_DAIR: o->cy = -1.0f; break;
    default: o->buttons |= MU_PC_A; break;
    }
}

/* ---- the plans: each frame of a running plan, returns 0 when it is over ---- */
static int pc_run_plan(const MuPcConfig* c, MuPcState* s, const MuPcView* v, MuPcInput* o)
{
    const MuPcFighter* f = &v->cpu;
    const int t = s->plan_t;
    const int dir = s->plan_dir;
    const int toward = pc_toward(v);
    switch (s->plan) {
    case PC_P_IDLE:
        return t < s->plan_len;
    case PC_P_WALK:
        o->lx = 0.45f * (float) dir;
        return t < s->plan_len;
    case PC_P_CROUCH:
        o->ly = -1.0f;
        return t < s->plan_len;
    case PC_P_DASH_DANCE: {
        const int period = 8 + s->plan_arg;   /* frames per dash */
        o->lx = ((t / period) & 1) ? (float) -dir : (float) dir;
        return t < s->plan_len;
    }
    case PC_P_APPROACH:
        o->lx = (float) toward;
        return t < s->plan_len && pc_abs(v->opp.x - f->x) > (float) c->attack_range;
    case PC_P_RUN_AWAY:
        if (pc_abs(f->x) > v->ledge_x - 18.0f && pc_sign(f->x) == (float) dir) return 0;   /* at the edge */
        o->lx = (float) dir;
        return t < s->plan_len;
    case PC_P_HOP:
        if (t == 0 || (s->plan_arg && t < 8 && !f->airborne)) o->buttons |= MU_PC_X;
        if (f->airborne) o->lx = 0.6f * (float) dir;
        return t < 4 || f->airborne || t < 6;
    case PC_P_WAVEDASH:
        if (t == 0) {
            o->buttons |= MU_PC_X;
            return 1;
        }
        if (s->plan_len == 0 && (f->airborne || pc_is_jumping(f->motion))) {
            pc_air_dodge(o, 0.95f * (float) dir, -0.31f);
            s->plan_len = 1;   /* dodged */
            return 1;
        }
        if (s->plan_len == 0 && t > 10) return 0;   /* never left the ground */
        if (s->plan_len && !f->airborne && f->motion != MU_PC_MS_LANDING_FALL_SPECIAL) {
            if (s->plan_arg) {   /* the chain: wavedash, then forward tilt */
                pc_start(s, PC_P_GROUND_ATTACK, toward, MU_PC_ATK_FTILT, 0);
                return 1;
            }
            return 0;
        }
        return t < 40;
    case PC_P_GROUND_ATTACK: {
        const int which = s->plan_arg;
        if (which == MU_PC_ATK_DASH_ATTACK || which == MU_PC_ATK_BOOST_GRAB) {
            if (t < 6) {
                o->lx = (float) dir;
                return 1;
            }
            if (t == 6) o->buttons |= MU_PC_A;
            if (which == MU_PC_ATK_BOOST_GRAB && t == 8) o->buttons |= MU_PC_Z;
            return t < 10 || !f->can_act;
        }
        if (t == 0) {
            switch (which) {
            case MU_PC_ATK_FTILT: o->lx = 0.55f * (float) dir; break;
            case MU_PC_ATK_UTILT: o->ly = 0.5f; break;
            case MU_PC_ATK_DTILT: o->ly = -0.55f; break;
            case MU_PC_ATK_FSMASH: o->cx = (float) dir; break;
            case MU_PC_ATK_USMASH: o->cy = 1.0f; break;
            case MU_PC_ATK_DSMASH: o->cy = -1.0f; break;
            case MU_PC_ATK_GRAB: o->buttons |= MU_PC_Z; break;
            default: o->buttons |= MU_PC_A; break;
            }
            return 1;
        }
        if (t == 1) {   /* tilts: the stick is held and A follows, so it never reads as a smash */
            if (which == MU_PC_ATK_FTILT) { o->lx = 0.55f * (float) dir; o->buttons |= MU_PC_A; }
            if (which == MU_PC_ATK_UTILT) { o->ly = 0.5f; o->buttons |= MU_PC_A; }
            if (which == MU_PC_ATK_DTILT) { o->ly = -0.55f; o->buttons |= MU_PC_A; }
            return 1;
        }
        return t < 4 || (!f->can_act && t < 120);
    }
    case PC_P_SPECIAL: {
        if (t == 0) {
            o->buttons |= MU_PC_B;
            if (s->plan_arg == MU_PC_ATK_SIDE_B) o->lx = (float) dir;
            if (s->plan_arg == MU_PC_ATK_UP_B) { o->ly = 1.0f; o->lx = 0.3f * (float) dir; }
            if (s->plan_arg == MU_PC_ATK_DOWN_B) o->ly = -1.0f;
            return 1;
        }
        if (s->plan_arg == MU_PC_ATK_UP_B && t < 30) { o->ly = 0.7f; o->lx = 0.7f * (float) dir; }
        return t < 4 || (!f->can_act && t < 180);
    }
    case PC_P_AERIAL: {
        /* plan_arg: the aerial; plan_len bits: 1 full hop, 2 fast fall, 4 L-cancel, 8 pressed, 16 fell */
        const int full = s->plan_len & 1;
        if (t == 0) {
            o->buttons |= MU_PC_X;
            return 1;
        }
        if (!f->airborne && !(s->plan_len & 8)) {
            if (full && f->motion == MU_PC_MS_KNEE_BEND) o->buttons |= MU_PC_X;   /* held: full hop */
            return t < 12;
        }
        if (f->airborne && !(s->plan_len & 8)) {
            if (full && t < 8) {   /* rise a little before a full hop aerial */
                o->buttons |= MU_PC_X;
                o->lx = 0.6f * (float) toward;
                return 1;
            }
            pc_aerial_press(s->plan_arg, pc_sign(f->facing), o);
            s->plan_len |= 8;
            return 1;
        }
        if (f->airborne) {
            o->lx = 0.5f * (float) toward;
            if ((s->plan_len & 2) && !(s->plan_len & 16) && f->vy < 0.0f) {
                o->lx = 0.0f;
                o->ly = -1.0f;   /* fast fall */
                s->plan_len |= 16;
            }
            if ((s->plan_len & 4) && pc_is_aerial_attack(f->motion) && v->land_frames >= 0 && v->land_frames <= 4) {
                o->buttons |= MU_PC_L;   /* L-cancel */
                o->l = 0.0f;
            }
            return t < 120;
        }
        return !f->can_act && t < 60;
    }
    case PC_P_SHIELD:
        pc_shield(o);
        return t < s->plan_len;
    case PC_P_SPOT_DODGE:
        pc_shield(o);
        o->ly = -1.0f;
        return t < 2;
    case PC_P_ROLL:
        pc_shield(o);
        o->lx = (float) dir;
        return t < 2;
    case PC_P_JUMP_AWAY:
        if (t == 0) o->buttons |= MU_PC_X;
        o->lx = (float) dir;
        return t < 20 && !(t > 4 && !f->airborne);
    case PC_P_OOS: {
        const int which = s->plan_arg;
        switch (which) {
        case MU_PC_OOS_HOLD: pc_shield(o); return t < 15;
        case MU_PC_OOS_DROP: return 0;
        case MU_PC_OOS_ROLL: pc_shield(o); o->lx = (float) dir; return t < 2;
        case MU_PC_OOS_SPOT_DODGE: pc_shield(o); o->ly = -1.0f; return t < 2;
        case MU_PC_OOS_GRAB: pc_shield(o); if (t == 0) o->buttons |= MU_PC_A; return t < 2;
        default: break;
        }
        if (t == 0) {
            pc_shield(o);
            o->buttons |= MU_PC_X;
            return 1;
        }
        if (which == MU_PC_OOS_UP_B && t == 1) { o->ly = 1.0f; o->buttons |= MU_PC_B; return 1; }
        if (which == MU_PC_OOS_SHINE && t == 1) { o->ly = -1.0f; o->buttons |= MU_PC_B; return 1; }
        if (which == MU_PC_OOS_NAIR && f->airborne && !s->plan_len) {
            o->buttons |= MU_PC_A;
            s->plan_len = 1;
            return 1;
        }
        if (which == MU_PC_OOS_WAVEDASH && !s->plan_len && (f->airborne || pc_is_jumping(f->motion))) {
            pc_air_dodge(o, 0.95f * (float) dir, -0.31f);
            s->plan_len = 1;
            return 1;
        }
        return t < 40 && !(t > 6 && f->can_act && !f->airborne);
    }
    case PC_P_GETUP:
        if (t < s->plan_len) return 1;   /* waits a moment on the floor */
        switch (s->plan_arg) {
        case MU_PC_GETUP_ROLL_IN: o->lx = (float) toward; break;
        case MU_PC_GETUP_ROLL_AWAY: o->lx = (float) -toward; break;
        case MU_PC_GETUP_ATTACK: o->buttons |= MU_PC_A; break;
        case MU_PC_GETUP_WAIT: return pc_is_down(f->motion) && t < 300;
        default: o->ly = 1.0f; break;
        }
        return t < s->plan_len + 2;
    case PC_P_LEDGE: {
        const float in = f->x > 0.0f ? -1.0f : 1.0f;   /* toward the stage */
        const int k = t - s->plan_len;
        if (k < 0) return f->motion == MU_PC_MS_CLIFF_WAIT;
        switch (s->plan_arg) {
        case MU_PC_LEDGE_ROLL: pc_shield(o); return k < 2;
        case MU_PC_LEDGE_JUMP: o->buttons |= MU_PC_X; o->lx = 0.5f * in; return k < 2;
        case MU_PC_LEDGE_ATTACK: o->buttons |= MU_PC_A; return k < 2;
        case MU_PC_LEDGE_DROP_JUMP:
            if (k < 2) { o->lx = -in; return 1; }
            if (k == 4) o->buttons |= MU_PC_X;
            o->lx = in;
            return k < 30;
        case MU_PC_LEDGE_DASH:
            if (k < 2) { o->lx = -in; return 1; }
            if (k == 2) { o->buttons |= MU_PC_X; o->lx = in; return 1; }
            if (k == 4) { pc_air_dodge(o, 0.95f * in, -0.3f); return 1; }
            return k < 30 && f->airborne;
        default: o->lx = in; return k < 2;
        }
    }
    case PC_P_AFTER: {
        const float center = f->x > 0.0f ? -1.0f : 1.0f;
        if (t > 0) return 0;
        switch (s->plan_arg) {
        case MU_PC_AFTER_JUMP: o->buttons |= MU_PC_X; o->lx = center; break;
        case MU_PC_AFTER_AIR_DODGE: pc_air_dodge(o, 0.7f * center, 0.4f); break;
        case MU_PC_AFTER_NAIR: o->buttons |= MU_PC_A; break;
        case MU_PC_AFTER_UP_B: o->buttons |= MU_PC_B; o->ly = 1.0f; o->lx = 0.4f * center; break;
        default: break;
        }
        return 1;
    }
    default:
        return 0;
    }
}

/* Off the stage with no hitstun: jump back, then the up special toward the ledge. */
static int pc_recover(const MuPcConfig* c, MuPcState* s, const MuPcView* v, MuPcInput* o)
{
    const MuPcFighter* f = &v->cpu;
    const float in = f->x > 0.0f ? -1.0f : 1.0f;
    const int high = c->recovery == MU_PC_RECOVER_HIGH ||
                     (c->recovery == MU_PC_RECOVER_MIX && (s->plan_arg & 1));
    o->lx = in;
    if (pc_between(f->motion, MU_PC_MS_FALL_SPECIAL, MU_PC_MS_FALL_SPECIAL_B) || f->motion >= MU_PC_MS_SPECIAL_FIRST) {
        o->ly = high ? 0.7f : 0.3f;   /* steer the up special and the fall after it */
        return 1;
    }
    if (f->jumps_left > 0 && f->vy < 0.0f && f->y < v->ledge_y + 30.0f) {
        if (s->plan_t != 1) o->buttons |= MU_PC_X;   /* one press, then a frame off */
        s->plan_t = s->plan_t == 1 ? 0 : 1;
        return 1;
    }
    if (f->jumps_left == 0 && f->vy < 0.0f && f->y < v->ledge_y + (high ? 25.0f : -15.0f)) {
        o->buttons |= MU_PC_B;
        o->ly = high ? 0.75f : 0.45f;
        o->lx = (high ? 0.65f : 0.9f) * in;
    }
    return 1;
}

int mu_pc_frame(const MuPcConfig* c, MuPcState* s, const MuPcView* v, MuPcInput* o)
{
    const MuPcFighter* f = &v->cpu;
    const int toward = pc_toward(v);
    const int new_hitlag = f->hitlag && !s->was_hitlag;
    const int hitstun_ended = s->was_hitstun && !f->hitstun && !f->hitlag;
    const int shieldstun_ended = s->was_shieldstun && f->motion == MU_PC_MS_GUARD;
    const int down = pc_is_down_wait(f->motion);
    const int ledge = f->motion == MU_PC_MS_CLIFF_WAIT;
    int busy_reaction = 0;
    pc_clear(o);
    if (c == 0 || !c->enabled || !f->present || f->motion < MU_PC_MS_WAIT || f->motion == MU_PC_MS_ENTRY) {
        s->plan = PC_P_NONE;
        s->was_hitlag = s->was_hitstun = s->was_shieldstun = s->was_down = s->was_ledge = 0;
        return 0;
    }
    s->was_shieldstun = f->motion == MU_PC_MS_GUARD_SET_OFF;

    /* ---- hit reactions (also with "freeze") ---- */
    if (new_hitlag) {
        s->plan = PC_P_NONE;   /* a hit ends whatever it was doing */
        s->di = pc_pick(s, c->di_odds, MU_PC_DI_COUNT);
        if (s->di < 0) s->di = MU_PC_DI_NONE;
        s->sdi_left = c->sdi_amount * 2;
        s->tech_pressed = 0;
    }
    s->was_hitlag = f->hitlag;
    if (f->hitlag) {
        float lx, ly;
        pc_di_stick(s->di, toward, f->x, &lx, &ly);
        if (s->sdi_left > 0) {
            /* SDI: the stick leaves neutral and comes back, every other frame */
            if ((s->sdi_left & 1) == 0) { o->lx = lx != 0.0f || ly != 0.0f ? lx : (float) -toward; o->ly = ly; }
            s->sdi_left--;
        } else {
            o->lx = lx;
            o->ly = ly;
        }
        s->was_hitstun = 1;
        return 1;
    }
    if (pc_is_tumble(f) && !s->tech_pressed && v->land_frames >= 0 && v->land_frames <= 6) {
        int choice = pc_pick(s, c->tech_odds, MU_PC_TECH_COUNT);
        s->tech_pressed = 1;
        s->tech_choice = choice;
        if (choice >= 0 && choice != MU_PC_TECH_MISS) {
            pc_shield(o);
            o->lx = choice == MU_PC_TECH_IN ? (float) toward : choice == MU_PC_TECH_AWAY ? (float) -toward : 0.0f;
            s->was_hitstun = f->hitstun;
            return 1;
        }
    }
    if (s->tech_pressed && f->airborne && s->tech_choice >= 0 && s->tech_choice != MU_PC_TECH_MISS) {
        o->lx = s->tech_choice == MU_PC_TECH_IN ? (float) toward : s->tech_choice == MU_PC_TECH_AWAY ? (float) -toward : 0.0f;
        busy_reaction = 1;
    }
    if (!f->airborne) s->tech_pressed = 0;
    if (hitstun_ended && f->airborne) {
        const int choice = pc_pick(s, c->after_odds, MU_PC_AFTER_COUNT);
        s->was_hitstun = 0;
        if (choice > MU_PC_AFTER_NOTHING) {
            pc_start(s, PC_P_AFTER, toward, choice, 1);
            pc_run_plan(c, s, v, o);
            s->plan = PC_P_NONE;
            return 1;
        }
    }
    s->was_hitstun = f->hitstun;
    if (f->hitstun) {
        if (!busy_reaction) {
            float lx, ly;
            pc_di_stick(s->di, toward, f->x, &lx, &ly);   /* keep the DI side through hitstun */
            o->lx = lx;
            o->ly = ly;
        }
        return 1;
    }
    if (down && !s->was_down) {
        int choice = pc_pick(s, c->getup_odds, MU_PC_GETUP_COUNT);
        pc_start(s, PC_P_GETUP, toward, choice < 0 ? MU_PC_GETUP_STAND : choice, (int) pc_rand(s, 12));
    }
    s->was_down = down;
    if (ledge && !s->was_ledge) {
        int choice = pc_pick(s, c->ledge_odds, MU_PC_LEDGE_COUNT);
        pc_start(s, PC_P_LEDGE, toward, choice < 0 ? MU_PC_LEDGE_GETUP : choice,
                 (int) pc_rand(s, (mu_pc_u32) c->ledge_delay + 1u) + 1);
    }
    s->was_ledge = ledge;
    if (shieldstun_ended && !c->freeze) {
        int choice = pc_pick(s, c->oos_odds, MU_PC_OOS_COUNT);
        pc_start(s, PC_P_OOS, (int) pc_rand(s, 2) ? toward : -toward, choice < 0 ? MU_PC_OOS_HOLD : choice, 0);
    }
    if (f->motion == MU_PC_MS_GUARD_SET_OFF) {
        pc_shield(o);   /* in shieldstun: keep the shield up */
        return 1;
    }

    /* ---- recovery ---- */
    if (pc_offstage(v) && s->plan != PC_P_LEDGE && s->plan != PC_P_AFTER) {
        if (c->recovery == MU_PC_RECOVER_GAME) return 0;
        if (s->plan != PC_P_NONE) {
            s->plan = PC_P_NONE;
            s->plan_arg = (int) pc_rand(s, 2);   /* high or low, for "mix" */
            s->plan_t = 0;
        }
        pc_recover(c, s, v, o);
        return 1;
    }

    /* ---- defense: an attack starts near it ---- */
    if (pc_is_attack(v->opp.motion) && v->opp.motion != s->opp_motion) {
        s->opp_attack_seen = 1;
        s->opp_attack_t = (mu_pc_s32) v->frame;
    }
    s->opp_motion = v->opp.motion;
    if (!pc_is_attack(v->opp.motion)) s->opp_attack_seen = 0;
    if (s->opp_attack_seen && !c->freeze && !f->airborne && f->can_act &&
        (s->plan == PC_P_NONE || s->plan == PC_P_IDLE || s->plan == PC_P_WALK || s->plan == PC_P_CROUCH ||
         s->plan == PC_P_DASH_DANCE || s->plan == PC_P_APPROACH || s->plan == PC_P_RUN_AWAY) &&
        (mu_pc_s32) v->frame - s->opp_attack_t >= (mu_pc_s32) c->reaction_frames &&
        pc_abs(v->opp.x - f->x) < (float) c->attack_range + 25.0f)
    {
        const int choice = pc_pick(s, c->defend_odds, MU_PC_DEF_COUNT);
        s->opp_attack_seen = 0;   /* one answer per attack */
        switch (choice) {
        case MU_PC_DEF_SHIELD: pc_start(s, PC_P_SHIELD, toward, 0, 12 + (int) pc_rand(s, 24)); break;
        case MU_PC_DEF_SPOT_DODGE: pc_start(s, PC_P_SPOT_DODGE, toward, 0, 2); break;
        case MU_PC_DEF_ROLL_IN: pc_start(s, PC_P_ROLL, toward, 0, 2); break;
        case MU_PC_DEF_ROLL_AWAY: pc_start(s, PC_P_ROLL, -toward, 0, 2); break;
        case MU_PC_DEF_JUMP_AWAY: pc_start(s, PC_P_JUMP_AWAY, -toward, 0, 20); break;
        default: break;
        }
    }

    /* ---- the running plan ---- */
    if (s->plan != PC_P_NONE) {
        const int going = pc_run_plan(c, s, v, o);
        s->plan_t++;
        if (going) return 1;
        s->plan = PC_P_NONE;
        s->idle_wait = (int) pc_rand(s, 8);
        return 1;
    }
    if (busy_reaction) return 1;

    /* ---- a free CPU picks what to do next ---- */
    if (c->freeze || !f->can_act || f->airborne) return 1;   /* neutral: drift and land */
    if (s->idle_wait > 0) {
        s->idle_wait--;
        return 1;
    }
    {
        const float dist = pc_abs(v->opp.x - f->x);
        int atk = -1;
        if (dist < (float) c->attack_range && pc_chance(s, c->aggression)) atk = pc_pick(s, c->attack_odds, MU_PC_ATK_COUNT);
        if (atk >= 0) {
            switch (atk) {
            case MU_PC_ATK_NAIR: case MU_PC_ATK_FAIR: case MU_PC_ATK_BAIR: case MU_PC_ATK_UAIR: case MU_PC_ATK_DAIR: {
                int flags = 0;
                if (pc_chance(s, c->full_hop_aerials)) flags |= 1;
                if (pc_chance(s, c->fast_fall)) flags |= 2;
                if (pc_chance(s, c->l_cancel)) flags |= 4;
                /* forward and back airs come out facing the right way: turn first when needed */
                if ((atk == MU_PC_ATK_FAIR && pc_sign(f->facing) != (float) toward) ||
                    (atk == MU_PC_ATK_BAIR && pc_sign(f->facing) == (float) toward))
                {
                    o->lx = 0.35f * (atk == MU_PC_ATK_FAIR ? (float) toward : (float) -toward);
                    return 1;
                }
                pc_start(s, PC_P_AERIAL, toward, atk, flags);
                break;
            }
            case MU_PC_ATK_NEUTRAL_B: case MU_PC_ATK_SIDE_B: case MU_PC_ATK_UP_B: case MU_PC_ATK_DOWN_B:
                pc_start(s, PC_P_SPECIAL, toward, atk, 0);
                break;
            case MU_PC_ATK_WAVEDASH_FTILT:
                pc_start(s, PC_P_WAVEDASH, toward, 1, 0);
                break;
            default:
                if (atk != MU_PC_ATK_FSMASH && atk != MU_PC_ATK_DASH_ATTACK && atk != MU_PC_ATK_BOOST_GRAB &&
                    pc_sign(f->facing) != (float) toward)
                {
                    o->lx = 0.35f * (float) toward;   /* turn to face it first */
                    return 1;
                }
                pc_start(s, PC_P_GROUND_ATTACK, toward, atk, 0);
                break;
            }
        } else {
            const int move = pc_pick(s, c->move_odds, MU_PC_MOVE_COUNT);
            switch (move) {
            case MU_PC_MOVE_WALK: pc_start(s, PC_P_WALK, (int) pc_rand(s, 3) ? toward : -toward, 0, 20 + (int) pc_rand(s, 40)); break;
            case MU_PC_MOVE_DASH_DANCE: pc_start(s, PC_P_DASH_DANCE, toward, (int) pc_rand(s, 5), 40 + (int) pc_rand(s, 60)); break;
            case MU_PC_MOVE_APPROACH: pc_start(s, PC_P_APPROACH, toward, 0, 90); break;
            case MU_PC_MOVE_RUN_AWAY: pc_start(s, PC_P_RUN_AWAY, -toward, 0, 30 + (int) pc_rand(s, 40)); break;
            case MU_PC_MOVE_SHORT_HOP: pc_start(s, PC_P_HOP, (int) pc_rand(s, 2) ? toward : -toward, 0, 0); break;
            case MU_PC_MOVE_FULL_HOP: pc_start(s, PC_P_HOP, (int) pc_rand(s, 2) ? toward : -toward, 1, 0); break;
            case MU_PC_MOVE_WAVEDASH_IN: pc_start(s, PC_P_WAVEDASH, toward, 0, 0); break;
            case MU_PC_MOVE_WAVEDASH_OUT: pc_start(s, PC_P_WAVEDASH, -toward, 0, 0); break;
            case MU_PC_MOVE_CROUCH: pc_start(s, PC_P_CROUCH, toward, 0, 20 + (int) pc_rand(s, 40)); break;
            default: pc_start(s, PC_P_IDLE, toward, 0, 15 + (int) pc_rand(s, 30)); break;
            }
            /* Near a ledge it never walks, dashes, hops or wavedashes outward: that only throws it off
             * the stage (a wavedash out from the ledge slid it off). Running away stops at the edge. */
            if ((s->plan == PC_P_WALK || s->plan == PC_P_DASH_DANCE || s->plan == PC_P_HOP || s->plan == PC_P_WAVEDASH) &&
                pc_abs(f->x) > v->ledge_x - 30.0f && (float) s->plan_dir == pc_sign(f->x))
            {
                s->plan_dir = -s->plan_dir;
            }
        }
    }
    {
        const int going = pc_run_plan(c, s, v, o);
        s->plan_t++;
        if (!going) s->plan = PC_P_NONE;
    }
    return 1;
}

/* ---- settings ---- */
static void pc_fill(mu_pc_u8* a, int n, mu_pc_u8 value)
{
    int i;
    for (i = 0; i < n; i++) a[i] = value;
}

void mu_pc_defaults(MuPcConfig* c)
{
    char* p = (char*) c;
    unsigned int i;
    for (i = 0; i < sizeof *c; i++) p[i] = 0;
    c->version = (mu_pc_u8) MU_PC_CONFIG_VERSION;
    c->enabled = 0;
    /* a mixed, mid-level partner: moves around, attacks in range, shields some */
    c->move_odds[MU_PC_MOVE_STAND] = 20;
    c->move_odds[MU_PC_MOVE_WALK] = 15;
    c->move_odds[MU_PC_MOVE_DASH_DANCE] = 25;
    c->move_odds[MU_PC_MOVE_APPROACH] = 25;
    c->move_odds[MU_PC_MOVE_RUN_AWAY] = 10;
    c->move_odds[MU_PC_MOVE_SHORT_HOP] = 10;
    c->move_odds[MU_PC_MOVE_FULL_HOP] = 5;
    c->move_odds[MU_PC_MOVE_WAVEDASH_IN] = 10;
    c->move_odds[MU_PC_MOVE_WAVEDASH_OUT] = 10;
    c->move_odds[MU_PC_MOVE_CROUCH] = 5;
    c->attack_odds[MU_PC_ATK_JAB] = 10;
    c->attack_odds[MU_PC_ATK_FTILT] = 10;
    c->attack_odds[MU_PC_ATK_DTILT] = 10;
    c->attack_odds[MU_PC_ATK_GRAB] = 20;
    c->attack_odds[MU_PC_ATK_NAIR] = 20;
    c->attack_odds[MU_PC_ATK_FAIR] = 15;
    c->attack_odds[MU_PC_ATK_BAIR] = 10;
    c->attack_odds[MU_PC_ATK_DASH_ATTACK] = 5;
    c->attack_odds[MU_PC_ATK_USMASH] = 5;
    c->defend_odds[MU_PC_DEF_NOTHING] = 30;
    c->defend_odds[MU_PC_DEF_SHIELD] = 45;
    c->defend_odds[MU_PC_DEF_SPOT_DODGE] = 10;
    c->defend_odds[MU_PC_DEF_ROLL_AWAY] = 10;
    c->defend_odds[MU_PC_DEF_JUMP_AWAY] = 5;
    c->oos_odds[MU_PC_OOS_HOLD] = 20;
    c->oos_odds[MU_PC_OOS_DROP] = 10;
    c->oos_odds[MU_PC_OOS_NAIR] = 25;
    c->oos_odds[MU_PC_OOS_GRAB] = 25;
    c->oos_odds[MU_PC_OOS_UP_B] = 10;
    c->oos_odds[MU_PC_OOS_ROLL] = 10;
    c->di_odds[MU_PC_DI_IN] = 25;
    c->di_odds[MU_PC_DI_OUT] = 25;
    c->di_odds[MU_PC_DI_SURVIVAL] = 30;
    c->di_odds[MU_PC_DI_DOWN_IN] = 10;
    c->di_odds[MU_PC_DI_NONE] = 10;
    pc_fill(c->tech_odds, MU_PC_TECH_COUNT, 25);
    pc_fill(c->getup_odds, MU_PC_GETUP_COUNT, 20);
    pc_fill(c->ledge_odds, MU_PC_LEDGE_COUNT, 0);
    c->ledge_odds[MU_PC_LEDGE_GETUP] = 30;
    c->ledge_odds[MU_PC_LEDGE_ROLL] = 20;
    c->ledge_odds[MU_PC_LEDGE_JUMP] = 20;
    c->ledge_odds[MU_PC_LEDGE_ATTACK] = 15;
    c->ledge_odds[MU_PC_LEDGE_DROP_JUMP] = 15;
    c->after_odds[MU_PC_AFTER_NOTHING] = 40;
    c->after_odds[MU_PC_AFTER_JUMP] = 30;
    c->after_odds[MU_PC_AFTER_AIR_DODGE] = 15;
    c->after_odds[MU_PC_AFTER_NAIR] = 15;
    c->aggression = 55;
    c->attack_range = 28;
    c->reaction_frames = 15;
    c->full_hop_aerials = 20;
    c->fast_fall = 60;
    c->l_cancel = 80;
    c->sdi_amount = 1;
    c->recovery = MU_PC_RECOVER_MIX;
    c->freeze = 0;
    c->ledge_delay = 30;
}

static const char* const pc_preset_names[] = {
    "Balanced",
    "Punching bag (DI and tech only)",
    "Aerial spammer (nair and bair)",
    "Runaway camper",
    "Wavedash and tilt",
    "Shield and punish",
    "Tech chase practice",
    "Ledge trapping practice",
};

int mu_pc_preset_count(void) { return (int) (sizeof pc_preset_names / sizeof pc_preset_names[0]); }

const char* mu_pc_preset_name(int index)
{
    return index >= 0 && index < mu_pc_preset_count() ? pc_preset_names[index] : "";
}

void mu_pc_preset(int index, MuPcConfig* c)
{
    const mu_pc_u8 enabled = c->enabled;
    mu_pc_defaults(c);
    c->enabled = enabled;
    switch (index) {
    case 1:   /* stands still and only reacts to being hit */
        c->freeze = 1;
        break;
    case 2:
        pc_fill(c->attack_odds, MU_PC_ATK_COUNT, 0);
        c->attack_odds[MU_PC_ATK_NAIR] = 50;
        c->attack_odds[MU_PC_ATK_BAIR] = 50;
        c->aggression = 90;
        c->attack_range = 40;
        c->move_odds[MU_PC_MOVE_APPROACH] = 50;
        c->fast_fall = 90;
        c->l_cancel = 100;
        break;
    case 3:
        pc_fill(c->move_odds, MU_PC_MOVE_COUNT, 0);
        c->move_odds[MU_PC_MOVE_RUN_AWAY] = 50;
        c->move_odds[MU_PC_MOVE_FULL_HOP] = 30;
        c->move_odds[MU_PC_MOVE_DASH_DANCE] = 20;
        c->aggression = 20;
        break;
    case 4:
        pc_fill(c->attack_odds, MU_PC_ATK_COUNT, 0);
        c->attack_odds[MU_PC_ATK_WAVEDASH_FTILT] = 50;
        c->attack_odds[MU_PC_ATK_FTILT] = 20;
        c->attack_odds[MU_PC_ATK_BOOST_GRAB] = 20;
        c->attack_odds[MU_PC_ATK_FAIR] = 10;
        c->move_odds[MU_PC_MOVE_WAVEDASH_IN] = 40;
        c->attack_range = 36;
        c->aggression = 75;
        break;
    case 5:
        pc_fill(c->defend_odds, MU_PC_DEF_COUNT, 0);
        c->defend_odds[MU_PC_DEF_SHIELD] = 100;
        c->reaction_frames = 8;
        pc_fill(c->oos_odds, MU_PC_OOS_COUNT, 0);
        c->oos_odds[MU_PC_OOS_GRAB] = 40;
        c->oos_odds[MU_PC_OOS_NAIR] = 30;
        c->oos_odds[MU_PC_OOS_UP_B] = 15;
        c->oos_odds[MU_PC_OOS_SHINE] = 15;
        c->aggression = 25;
        break;
    case 6:
        c->freeze = 1;
        pc_fill(c->tech_odds, MU_PC_TECH_COUNT, 25);
        pc_fill(c->getup_odds, MU_PC_GETUP_COUNT, 20);
        pc_fill(c->di_odds, MU_PC_DI_COUNT, 0);
        c->di_odds[MU_PC_DI_DOWN_IN] = 50;
        c->di_odds[MU_PC_DI_DOWN_OUT] = 50;
        break;
    case 7:
        c->freeze = 1;
        c->ledge_delay = 60;
        pc_fill(c->ledge_odds, MU_PC_LEDGE_COUNT, 0);
        c->ledge_odds[MU_PC_LEDGE_GETUP] = 20;
        c->ledge_odds[MU_PC_LEDGE_ROLL] = 20;
        c->ledge_odds[MU_PC_LEDGE_JUMP] = 20;
        c->ledge_odds[MU_PC_LEDGE_ATTACK] = 15;
        c->ledge_odds[MU_PC_LEDGE_DROP_JUMP] = 10;
        c->ledge_odds[MU_PC_LEDGE_DASH] = 15;
        break;
    default:
        break;
    }
}

void mu_pc_stage_ledge(int internal_stage_id, float* ledge_x, float* ledge_y)
{
    float x = 60.0f, y = 0.0f;
    switch (internal_stage_id) {
    case 0x0A: x = 56.0f; break;     /* Yoshi's Story */
    case 0x0C: x = 63.35f; y = 0.62f; break;   /* Fountain of Dreams */
    case 0x10: x = 87.75f; break;    /* Pokemon Stadium */
    case 0x1C: x = 77.27f; y = 0.01f; break;   /* Dream Land */
    case 0x24: x = 68.4f; break;     /* Battlefield */
    case 0x25: x = 85.57f; break;    /* Final Destination */
    default: break;
    }
    *ledge_x = x;
    *ledge_y = y;
}
