/* Frame data in replays (watching and takeover): what happened on each hit, shield hit, aerial
 * landing, tech and grab, measured from the game's own state and handed to the host's coach
 * (port/runtime/host/replay_coach.h) one event at a time.
 *
 * A hit or shield hit starts a measurement when the defender enters hitlag; it ends when both
 * fighters can act, and the difference of those first frames is the attacker's advantage (the
 * Training Lab's reading in mu_lab.c, for any pair). For an aerial on shield it also notes how many
 * frames after the hit the attacker landed and whether the landing was L-cancelled: each frame
 * the aerial connects later (lower on the shield) is one frame more advantage.
 *
 * The trackers are game state (kept and restored with the replay viewer's states), so a jump back
 * measures again from the same point. */
#include <dolphin/os.h>
#include <melee/ft/fighter.h>
#include <melee/ft/types.h>
#include <melee/ft/kinds/ftCommon/forward.h>
#include <melee/pl/player.h>
#include <sysdolphin/baselib/gobj.h>

#include "mu_native.h"

int mu_online_abi_command(unsigned int command, const unsigned char* payload, unsigned int size,
                          unsigned char* response, unsigned int capacity, unsigned int* response_size);
int mu_replay_frame_index(void);
float sinf(float x);

#define CMD_FRAME_EVENT 0xE8   /* source_host.cpp: payload as put_event below */

enum { EV_HIT = 1, EV_SHIELD, EV_LANDING, EV_TECH, EV_GRAB };
enum { NO_FRAMES = 0xFF };

typedef struct FdTrack {
    int prev_hitlag;
    int prev_motion;
    int active;
    int kind;            /* EV_HIT or EV_SHIELD */
    int attacker;
    int frame;           /* replay frame of the hit */
    int count;           /* frames since the hit */
    int attacker_at;     /* first frame each can act, -1 not yet */
    int defender_at;
    int move;            /* the attacker's motion state at the hit */
    int hitlag;
    int airborne;        /* the attacker was in the air at the hit */
    int to_land;         /* frames from the hit to the attacker's landing, -1 not yet */
    int late;            /* frames after the hit (out of hitlag, still airborne) its hitbox stayed out */
    int lcancel;         /* the attacker's landing: 0 none or auto cancel, 1 in time, 2 missed */
    int tilt;            /* the defender's shield tilt before the hit: -100 down .. 100 up */
    int shield_tilt;     /* the last tilt seen while shielding */
} FdTrack;

static FdTrack fd_track[4];
static int fd_lcancel[4];   /* the last aerial landing's result per port (mu_framedata_landing) */

static Fighter* fighter_of(int port)
{
    HSD_GObj* g = Player_GetEntity(port);
    return g != NULL ? g->user_data : NULL;
}

static int pct(const Fighter* fp)
{
    return fp != NULL ? (int) fp->dmg.x1830_percent : 0;
}

static void put_event(int type, int frame, int actor, int target, int adv, int move, int lcancel,
                      int to_land, int tilt, int hitlag, int late, int free_at)
{
    static unsigned char response[4096];
    unsigned char b[22];
    unsigned int got = 0;
    const Fighter* fa = fighter_of(actor);
    const Fighter* ft = target >= 0 ? fighter_of(target) : NULL;
    b[0] = (unsigned char) ((unsigned int) frame >> 24);
    b[1] = (unsigned char) ((unsigned int) frame >> 16);
    b[2] = (unsigned char) ((unsigned int) frame >> 8);
    b[3] = (unsigned char) frame;
    b[4] = (unsigned char) type;
    b[5] = (unsigned char) actor;
    b[6] = (unsigned char) (target >= 0 ? target : 0xFF);
    b[7] = (unsigned char) (signed char) (adv < -127 ? -127 : adv > 127 ? 127 : adv);
    b[8] = (unsigned char) (move >> 8);
    b[9] = (unsigned char) move;
    b[10] = (unsigned char) lcancel;
    b[11] = (unsigned char) (to_land < 0 || to_land > 254 ? NO_FRAMES : to_land);
    b[12] = (unsigned char) (signed char) tilt;
    b[13] = (unsigned char) (hitlag > 255 ? 255 : hitlag);
    b[14] = (unsigned char) (pct(fa) >> 8);
    b[15] = (unsigned char) pct(fa);
    b[16] = (unsigned char) (pct(ft) >> 8);
    b[17] = (unsigned char) pct(ft);
    b[18] = (unsigned char) (fa != NULL ? fa->kind : 0xFF);
    b[19] = (unsigned char) (ft != NULL ? ft->kind : 0xFF);
    b[20] = (unsigned char) (late < 0 || late > 254 ? NO_FRAMES : late);
    b[21] = (unsigned char) (free_at < 0 || free_at > 254 ? NO_FRAMES : free_at);
    mu_online_abi_command(CMD_FRAME_EVENT, b, sizeof b, response, sizeof response, &got);
}

/* The same test as mu_lab.c: an interruptible animation or a free state. */
static int can_act(const Fighter* fp)
{
    const int m = (int) fp->motion_id;
    if (fp->dmg.x195c_hitlag_frames > 0.0f || fp->x221C_b6) {
        return 0;
    }
    if (fp->allow_interrupt) {
        return 1;
    }
    return (m >= ftCo_MS_Wait && m <= ftCo_MS_Run) || (m >= ftCo_MS_JumpAerialF && m <= ftCo_MS_FallAerialB) ||
           m == ftCo_MS_Squat || m == ftCo_MS_SquatWait || m == ftCo_MS_DamageFall || m == ftCo_MS_Guard;
}

/* Taking a hit: a damage state (the attacker's own hitlag leaves it in its attack). */
static int hit_state(int m)
{
    return (m >= ftCo_MS_DamageHi1 && m <= ftCo_MS_DamageFlyRoll) || m == ftCo_MS_DamageScrew ||
           m == ftCo_MS_DamageScrewAir || m == ftCo_MS_DownDamageU || m == ftCo_MS_DownDamageD ||
           m == ftCo_MS_CaptureDamageHi || m == ftCo_MS_CaptureDamageLw || m == ftCo_MS_DamageIce;
}

/* A fighter that can be the one who just hit: frozen in hitlag or with a hitbox out. */
static int attacking(const Fighter* f)
{
    int i;
    if (f->dmg.x195c_hitlag_frames > 0.0f && (f->motion_id < ftCo_MS_GuardOn || f->motion_id > ftCo_MS_GuardReflect)) {
        return 1;
    }
    for (i = 0; i < 4; ++i) {
        if (f->x914[i].state != HitCapsule_Disabled) return 1;
    }
    return 0;
}

/* Who hit `port`: the game's damage source for a hit; for a shield hit (which has none) the
 * nearest other fighter that is attacking. None (-1) for a projectile with nobody close. */
static int attacker_of(int port, const Fighter* fd, int shield, int need_attack)
{
    int i, best = -1;
    float best_d = 0.0f;
    if (!shield && fd->dmg.x18c4_source_ply >= 0 && fd->dmg.x18c4_source_ply < 4 &&
        fd->dmg.x18c4_source_ply != port && fighter_of(fd->dmg.x18c4_source_ply) != NULL) {
        return fd->dmg.x18c4_source_ply;
    }
    for (i = 0; i < 4; ++i) {
        const Fighter* f = i != port ? fighter_of(i) : NULL;
        float dx, dy, d;
        if (f == NULL || (need_attack && !attacking(f))) continue;
        dx = f->cur_pos.x - fd->cur_pos.x;
        dy = f->cur_pos.y - fd->cur_pos.y;
        d = dx * dx + dy * dy;
        if (best < 0 || d < best_d) {
            best = i;
            best_d = d;
        }
    }
    return best;
}

/* Once per frame per fighter, from its input read in replay playback (mu_replay_input). */
void mu_framedata_frame(Fighter* fd)
{
    const int port = fd->player_idx & 3;
    FdTrack* t = &fd_track[port];
    const int m = (int) fd->motion_id;
    const int hitlag = fd->dmg.x195c_hitlag_frames > 0.0f;
    const int frame = mu_replay_frame_index();
    Fighter* fa;

    if (m == ftCo_MS_Guard) {
        /* mv.co.guard: x8 the tilt angle (+10, 90 straight up), x4 how far it is tilted */
        const float deg = fd->mv.co.guard.x8 - 10.0f;
        t->shield_tilt = (int) (sinf(deg * 0.017453292f) * fd->mv.co.guard.x4 * 100.0f);
    } else if (m < ftCo_MS_GuardOn || m > ftCo_MS_GuardReflect) {
        t->shield_tilt = 0;
    }

    /* Techs and grabs: on entering the state. */
    if (m != t->prev_motion) {
        if (m == ftCo_MS_Passive || m == ftCo_MS_PassiveStandF || m == ftCo_MS_PassiveStandB) {
            put_event(EV_TECH, frame, port, -1, 1, m, 0, -1, 0, 0, -1, -1);
        } else if (m == ftCo_MS_DownBoundU || m == ftCo_MS_DownBoundD) {
            put_event(EV_TECH, frame, port, -1, 0, m, 0, -1, 0, 0, -1, -1);
        } else if (m == ftCo_MS_CapturePulledHi || m == ftCo_MS_CapturePulledLw) {
            const int by = attacker_of(port, fd, 1, 0);
            if (by >= 0) put_event(EV_GRAB, frame, by, port, 0, (int) fighter_of(by)->motion_id, 0, -1, 0, 0, -1, -1);
        }
    }
    t->prev_motion = m;

    if (hitlag && !t->prev_hitlag && ((m >= ftCo_MS_GuardOn && m <= ftCo_MS_GuardReflect) || hit_state(m))) {
        const int shield = m >= ftCo_MS_GuardOn && m <= ftCo_MS_GuardReflect;
        const int by = attacker_of(port, fd, shield, 1);
        fa = by >= 0 ? fighter_of(by) : NULL;
        t->active = fa != NULL;
        if (fa != NULL) {
            t->kind = shield ? EV_SHIELD : EV_HIT;
            t->attacker = by;
            t->frame = frame;
            t->count = 0;
            t->attacker_at = t->defender_at = -1;
            t->move = (int) fa->motion_id;
            t->hitlag = (int) fd->dmg.x195c_hitlag_frames;
            t->airborne = fa->ground_or_air == GA_Air;
            t->to_land = -1;
            t->late = 0;
            t->lcancel = 0;
            t->tilt = shield ? t->shield_tilt : 0;
            fd_lcancel[by] = 0;
        }
    }
    t->prev_hitlag = hitlag;
    if (!t->active) {
        return;
    }
    fa = fighter_of(t->attacker);
    t->count++;
    if (fa == NULL || m < ftCo_MS_Wait || fa->motion_id < ftCo_MS_Wait) {
        t->active = 0;   /* a KO or respawn: no reading */
        return;
    }
    if (t->airborne && t->to_land < 0 && fa->ground_or_air == GA_Air && fa->dmg.x195c_hitlag_frames <= 0.0f) {
        int i;
        for (i = 0; i < 4; ++i) {
            if (fa->x914[i].state != HitCapsule_Disabled) {
                t->late++;
                break;
            }
        }
    }
    if (t->airborne && t->to_land < 0 && fa->ground_or_air != GA_Air) {
        t->to_land = t->count;
        t->lcancel = fd_lcancel[t->attacker];
    }
    if (t->attacker_at < 0 && fa->dmg.x195c_hitlag_frames <= 0.0f && can_act(fa)) {
        t->attacker_at = t->count;
    }
    if (t->defender_at < 0 && !hitlag && can_act(fd)) {
        t->defender_at = t->count;
    }
    if (t->attacker_at >= 0 && t->defender_at >= 0) {
        const int adv = t->defender_at - t->attacker_at;
        t->active = 0;
        put_event(t->kind, t->frame, t->attacker, port, adv, t->move, t->lcancel, t->to_land, t->tilt,
                  t->hitlag, t->airborne ? t->late : -1, t->defender_at);
    } else if (t->count > 300) {
        t->active = 0;   /* nobody could act for five seconds: no reading */
    }
}

/* An aerial landing (ftCo_LandingAir.c, with the game's own L-cancel test): status 1 in time,
 * 2 missed. Reported at once; a pending shield measurement picks it up as well. */
void mu_framedata_landing(Fighter* fp, int status)
{
    const int port = fp->player_idx & 3;
    if (fp->is_sub_fighter && fp->kind == Ft_Kind_Nana) return;
    fd_lcancel[port] = status;
    put_event(EV_LANDING, mu_replay_frame_index(), port, -1, 0, (int) fp->motion_id, status, -1, 0, 0, -1, -1);
}
