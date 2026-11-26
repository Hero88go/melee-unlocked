/* 20XX Melee 1.03 by Hax$ and Altimor (b0xx.com), native port of its netplay ruleset.
 *
 * In memory of Hax$. The source is 20XX's own netplay disc ("SSBM 20XX Netplay ISO Builder"): every
 * function here is the C of one function in its code block (0x8181CE00..), named by that address,
 * read from the disassembly and kept to the same steps, so the match plays as it does on 20XX.
 * Where 20XX hooks a game function it either runs first and then the game's own code (a "pre" hook),
 * or takes the function's place (a "replace" hook); the decomp sites say which.
 *
 * Off (mu_20xx103_on() false) nothing here runs and the game is unchanged. It is on only for a match
 * every player agreed to play with 20XX Melee 1.03 (the host's consent, MU_CONTROL_20XX103), offline
 * when this player chose it. While it is on, Melee Unlocked's own UCF 0.84 stays off (20XX's controller
 * fix replaces it). 20XX's widescreen patch is not ported: Melee Unlocked has its own.
 *
 * Per-fighter state 20XX keeps in the Fighter's motion variables (0x23D8..0x23EB, the tail of the
 * union no common state uses on the console) lives in mu_20xx_fs[], one per fighter: part of the
 * image, so rollback snapshots carry it.
 * SPDX-License-Identifier: GPL-2.0-or-later */
#include <dolphin/os.h>
#include <melee/ft/fighter.h>
#include <melee/ft/forward.h>
#include <melee/ft/types.h>
#include <melee/ft/kinds/ftCommon/ftCo_0A01.h>
#include <melee/mp/mpcoll.h>
#include <melee/mp/mplib.h>
#include <melee/ft/kinds/ftCommon/ftCo_Fall.h>
#include <melee/pl/plbonuslib.h>
#include <melee/pl/player.h>
#include <sysdolphin/baselib/controller.h>
#include "mu_native.h"

int mu_20xx103_on(void);
bool gm_GetDbPauseFlag(int);

/* ---- per-fighter state (console: fp+23D8..23EB) */
typedef struct Mu20xxFighter {
    unsigned int sdi_frames;   /* 23D8: hitlag frames counted by the SDI hooks */
    Vec3 sdi_origin;           /* 23DC: where the first SDI frame would have put the fighter */
    unsigned char x23E4;       /* 23E4 */
    unsigned char x23E8;       /* 23E8 */
    unsigned char sdi_held;    /* 23E9 */
    unsigned char x23EA;       /* 23EA */
    unsigned char x23EB;       /* 23EB */
} Mu20xxFighter;
static Mu20xxFighter mu_20xx_fs[6][2];   /* [player slot][0 main, 1 Nana] */

/* ---- raw controller history (console: 0x81820268, 0x81828428) */
typedef struct Mu20xxPad {   /* a PADStatus as 20XX copies it: buttons, sticks, triggers, A/B, err */
    unsigned short button;
    signed char sx, sy, cx, cy;
    unsigned char tl, tr, aa, ab, err, pad;
} Mu20xxPad;
static Mu20xxPad mu_20xx_ring[6][4];       /* per player slot, the last 4 frames, written in procInput */
static unsigned char mu_20xx_ring_idx[6];
static Mu20xxPad mu_20xx_nana_hist[6][30]; /* per player slot, Popo's pad beside Nana's 30-frame buffer */
static unsigned char mu_20xx_was_paused[6];

/* ---- per player: self_vel.x this frame and last, frames counted (console: data+0x10000-0x75C8) */
typedef struct Mu20xxVel { float cur, prev; int count; } Mu20xxVel;
static Mu20xxVel mu_20xx_vel[6];

static Mu20xxFighter* fs(Fighter* fp)
{
    int slot = fp->player_idx;
    if (slot < 0 || slot > 5) slot = 0;
    return &mu_20xx_fs[slot][fp->is_sub_fighter ? 1 : 0];
}

/* ---- helpers */

/* 8181CE48: this frame's raw controller status for a port, from the HSD pad queue: entry qread - 1,
 * or entry 4 when qread is 0 (20XX's own choice of slot then, kept). */
static PADStatus* raw_pad(int port)
{
    int q = HSD_PadLibData.qread;
    return &HSD_PadLibData.queue[q != 0 ? q - 1 : 4].stat[port];
}

/* 8181D204: 1.0 cardinals. A stick held past 79 on one axis and within 22 on the other reads
 * exactly 1.0 on that axis and 0 on the other. Otherwise the game's value stands. */
static void cardinal(int x, int y, Vec2* out)
{
    unsigned ax = (unsigned) (x < 0 ? -x : x) & 0xFF, ay = (unsigned) (y < 0 ? -y : y) & 0xFF;
    if (ax > 79) {
        if (ay > 22) return;
        out->x = x > 0 ? 1.0f : -1.0f;
        out->y = 0.0f;
        return;
    }
    if (ay <= 79 || ax > 22) return;
    out->x = 0.0f;
    out->y = y > 0 ? 1.0f : -1.0f;
}

/* 8181CF18: x*x + y*y >= sdi_min_stick_mag^2. */
static int sdi_mag(const Vec2* v)
{
    /* Separate statements: 20XX multiplies (8181CEF4) and then adds, never fused. */
    float m = p_ftCommonData->sdi_min_stick_mag;
    float xx = v->x * v->x;
    float yy = v->y * v->y;
    float sum = yy + xx;
    float mm = m * m;
    return mm <= sum;
}

/* 8181CE80: the stick out at the rim: ((int)(|x|*80 - 1e-4) + 2)^2 + (... y ...)^2 >= 6401. */
static int at_rim(const Vec2* v)
{
    int a = (int) ((v->x < 0 ? -v->x : v->x) * 80.0f + -9.999999747e-05f);
    int b = (int) ((v->y < 0 ? -v->y : v->y) * 80.0f + -9.999999747e-05f);
    int r = (b + 2) * (b + 2) + (a + 2) * (a + 2);
    return r >= 0x1901;
}

/* 8181D2E8 / 8181D528: where the fighter would stand after moving by its ground speed: a copy of
 * its collision data run through the map collision from the moved position. */
static void predicted_pos(Fighter* fp, Vec3* out)
{
    CollData cd = fp->coll_data;
    Vec3 pos = fp->cur_pos;
    Vec3 speed = { 0.0f, 0.0f, 0.0f };
    if (fp->ground_or_air == GA_Ground && mpGetSpeed(fp->coll_data.floor.index, &pos, &speed)) {
        pos.x += speed.x;
        pos.y += speed.y;
        pos.z += speed.z;
    }
    if (fp->ecb_lock == 1) {
        cd.x130_flags &= ~0x10u;
    }
    cd.last_pos = cd.cur_pos;
    cd.cur_pos = pos;
    if (fp->ground_or_air != GA_Ground) {
        mpColl_800477E0(&cd);
    } else {
        mpColl_8004B108(&cd);
    }
    *out = cd.cur_pos;
}

/* ---- Fighter_procInput+0x1D0 (8181E154): humans' sticks read as 1.0 cardinals. */
void mu_20xx_input(Fighter* fp)
{
    PADStatus* pad;
    if (!mu_20xx103_on() || ftCo_IsCpuControlled(fp)) return;
    pad = raw_pad(fp->pad_port);
    cardinal(pad->stickX, pad->stickY, &fp->input.lstick[0]);
    cardinal(pad->substickX, pad->substickY, &fp->input.cstick[0]);
}

/* ---- ftCo_Damage_OnEveryHitlag (8181E558, replaces it): smash DI. */
void mu_20xx_damage_hitlag(Fighter* fp)
{
    Mu20xxFighter* s = fs(fp);
    unsigned int n;
    Vec2 v;
    if (!fp->allow_sdi) return;
    n = ++s->sdi_frames;
    if (!sdi_mag(&fp->input.lstick[0])) goto none;
    if (n == 2 && s->x23E8) {
        if (fp->active_sticky.lstick.x > 1 && fp->active_sticky.lstick.y > 1) goto none;
    } else if (fp->active_timer.lstick.x >= p_ftCommonData->sdi_stick_window &&
               p_ftCommonData->sdi_stick_window <= fp->active_timer.lstick.y) {
        goto none;
    }
    if (s->sdi_held) {
        s->sdi_held = 0;
    } else if (!sdi_mag(&fp->input.lstick[1]) && !at_rim(&fp->input.lstick[0])) {
        s->sdi_held = 1;
        predicted_pos(fp, &s->sdi_origin);
    }
    fp->active_timer.lstick.x = 0xFE;
    fp->active_timer.lstick.y = 0xFE;
    v.x = fp->input.lstick[0].x * p_ftCommonData->sdi_pos_scale;
    v.y = fp->input.lstick[0].y * p_ftCommonData->sdi_pos_scale;
    fp->cur_pos.x += v.x;
    fp->cur_pos.y += v.y;
    /* 20XX passes a pointer (fp+620) where the game passes is_sub_fighter: kept, as nonzero. */
    pl_800401F0(fp->player_idx, 1, v.x, v.y);
    return;
none:
    s->sdi_held = 0;
}

/* ---- ftCo_80093240 (8181E6B0, replaces it): smash DI in shield hitlag, on the ground. */
void mu_20xx_guard_sdi(Fighter* fp)
{
    Mu20xxFighter* s = fs(fp);
    float ax, scl;
    unsigned int n;
    if (!fp->allow_sdi || fp->ground_or_air != GA_Ground) return;
    ax = fp->input.lstick[0].x < 0 ? -fp->input.lstick[0].x : fp->input.lstick[0].x;
    n = ++s->sdi_frames;
    if (!(ax >= p_ftCommonData->sdi_min_stick_mag)) goto none;
    if (n == 2 && s->x23E8) {
        if (fp->active_sticky.lstick.x > 1) goto none;
    } else if (fp->active_timer.lstick.x >= p_ftCommonData->sdi_stick_window) {
        goto none;
    }
    if (s->sdi_held) {
        s->sdi_held = 0;
    } else {
        float px = fp->input.lstick[1].x < 0 ? -fp->input.lstick[1].x : fp->input.lstick[1].x;
        if (!(p_ftCommonData->sdi_min_stick_mag <= px) && ax != 1.0f) {
            s->sdi_held = 1;
            predicted_pos(fp, &s->sdi_origin);
        }
    }
    fp->active_timer.lstick.x = 0xFE;
    scl = fp->input.lstick[0].x * p_ftCommonData->sdi_pos_scale * p_ftCommonData->x4C0;
    fp->cur_pos.x = fp->coll_data.floor.normal.y * scl + fp->cur_pos.x;
    fp->cur_pos.y = scl * -fp->coll_data.floor.normal.x + fp->cur_pos.y;
    return;
none:
    s->sdi_held = 0;
}

/* 8181D298: a fighter no player drives here: a CPU slot, or Nana not following her Popo's
 * inputs (x1B82 bit 0 clear). */
static int not_player_driven(Fighter* fp)
{
    if (Player_GetPlayerSlotType(fp->player_idx) != 0) return 1;
    if (fp->kind != Ft_Kind_Nana) return 0;
    return !fp->cpu.xFA_b7;
}

/* ---- ftCo_800939B4 (8181E11C, after it): clears 23E4. */
void mu_20xx_guard_800939B4_after(Fighter* fp)
{
    fs(fp)->x23E4 = 0;
}

/* 8181E080: no spot dodge from shield on a platform while the shield drop's input is still
 * being made: C-stick y above the spot dodge threshold, stick moved for at least x320 frames,
 * the stick's x not 0. */
static int shield_drop_window(Fighter* fp)
{
    if (not_player_driven(fp)) return 0;
    if (fp->input.cstick[0].y <= p_ftCommonData->x314) return 0;
    if (fp->coll_data.floor.index == -1) return 0;
    if (!mpColl_IsOnPlatform(&fp->coll_data)) return 0;
    if ((int) fp->active_timer.lstick.x < p_ftCommonData->x320) return 0;
    return fp->input.lstick[0].x != 0.0f;
}

/* ---- ftCo_80099794 / ftCo_8009980C (8181E834 / 8181E7EC, before them): false in that window. */
int mu_20xx_escape_blocked(Fighter* fp)
{
    return shield_drop_window(fp);
}

/* ftCo_SpecialS_HasInput (8181E87C) and ftCo_800D688C (8181E4AC): 20XX replaces both with the
 * same test the game makes; nothing to port. */

/* ---- ftCo_SquatRv_CheckInput (8181E434, before it): a player holding the stick down at the rim
 * (y at or below -0.59) does not stand up from the crouch. */
int mu_20xx_squatrv_blocked(Fighter* fp)
{
    if (not_player_driven(fp)) return 0;
    if (!at_rim(&fp->input.lstick[0])) return 0;
    return !(fp->input.lstick[0].y > -0.5899999738f);
}

/* ---- ftCo_Fall_Enter (8181E4E8, before it): note a fall out of DamageFall (0xFD) and whether
 * the C-stick was out then. */
void mu_20xx_fall_enter(Fighter* fp)
{
    Mu20xxFighter* s = fs(fp);
    int from_tumble = fp->motion_id == 0xFD;
    s->x23EA = (unsigned char) from_tumble;
    s->x23EB = 0;
    s->sdi_held = (unsigned char) (from_tumble && (fp->input.cstick[0].x != 0.0f || fp->input.cstick[0].y != 0.0f));
}

/* ---- ftCo_DamageFall_IASA+0x9C (8181EA5C): wiggle out of tumble. The stick past x210 on x,
 * pushed within x214 frames as the game asks, or (a player) pushed this frame or last from inside
 * x210 the frame before. True: the fighter fell (Fall_Enter ran) and the game's own test is skipped. */
int mu_20xx_damagefall_wiggle(Fighter_GObj* gobj)
{
    Fighter* fp = gobj->user_data;
    float ax = fp->input.lstick[0].x < 0 ? -fp->input.lstick[0].x : fp->input.lstick[0].x;
    float px;
    if (!(ax >= p_ftCommonData->x210)) return 0;
    if ((int) fp->active_timer.lstick.x < p_ftCommonData->x214) goto fall;
    if (not_player_driven(fp)) return 0;
    if (fp->active_timer.lstick.x > 1) return 0;
    px = fp->input.lstick[1].x < 0 ? -fp->input.lstick[1].x : fp->input.lstick[1].x;
    if (px >= p_ftCommonData->x210) return 0;
fall:
    ftCo_Fall_Enter(gobj);
    return 1;
}

static void copy_raw(Mu20xxPad* d, const PADStatus* p)
{
    d->button = p->button;
    d->sx = p->stickX;
    d->sy = p->stickY;
    d->cx = p->substickX;
    d->cy = p->substickY;
    d->tl = p->triggerLeft;
    d->tr = p->triggerRight;
    d->aa = p->analogA;
    d->ab = p->analogB;
    d->err = (unsigned char) p->err;
}

/* ---- Fighter_procInput, at the lb_8000D148 call (8181F124 via the 8000D148 hook): this frame's
 * raw pad into the player's 4-frame ring. */
void mu_20xx_record_pad(Fighter* fp)
{
    int slot = fp->player_idx;
    int i;
    if (!mu_20xx103_on() || slot < 0 || slot > 5) return;
    i = (mu_20xx_ring_idx[slot] + 1) & 3;
    mu_20xx_ring_idx[slot] = (unsigned char) i;
    copy_raw(&mu_20xx_ring[slot][i], raw_pad(fp->pad_port));
}

/* ---- ftCo_800B0918 (8181F02C, after it): Popo's raw pad beside the entry Nana's buffer just
 * took; the first frame after a pause her entry loses Z. */
void mu_20xx_nana_buffer(Fighter* popo, Fighter* nana)
{
    int slot = nana->player_idx;
    int i;
    if (!mu_20xx103_on() || slot < 0 || slot > 5) return;
    i = (int) (nana->cpu.x444 - nana->cpu.xFC);
    copy_raw(&mu_20xx_nana_hist[slot][i], raw_pad(popo->pad_port));
    if (mu_20xx_was_paused[slot]) {
        nana->cpu.x444->x0 &= ~0x10u;
    }
    mu_20xx_was_paused[slot] = (unsigned char) gm_GetDbPauseFlag(0);
}

/* ---- ftCo_Turn_IASA (8181E1D0, before it): dashback. On the turn's frame 2, a player whose stick
 * x moved more than 75 (raw) over the last two frames, now pushed past x3C the new way, finishes
 * the turn at once; Popo hands the same to Nana's buffered input. */
void mu_20xx_turn(Fighter* fp)
{
    const Mu20xxPad *cur, *old;
    int slot = fp->player_idx, d;
    float f0;
    if (!mu_20xx103_on() || ftCo_IsCpuControlled(fp)) return;
    if (fp->cur_anim_frame != 2.0f) return;
    if (fp->mv.co.turn.has_turned) return;
    f0 = -fp->facing_dir;
    if (f0 * fp->input.lstick[0].x < p_ftCommonData->dash_smash_stick_threshold) return;
    if (fp->active_timer.lstick.x > 1) return;
    if (slot < 0 || slot > 5) return;
    if (fp->kind == Ft_Kind_Nana) {
        int i = (int) (fp->cpu.x448 - fp->cpu.xFC);
        cur = &mu_20xx_nana_hist[slot][(i + 29) % 30];
        old = &mu_20xx_nana_hist[slot][(i + 27) % 30];
    } else {
        int i = mu_20xx_ring_idx[slot];
        cur = &mu_20xx_ring[slot][i];
        old = &mu_20xx_ring[slot][(i - 2) & 3];
    }
    d = cur->sx - old->sx;
    if (d * d <= 0x15F9) return;
    fp->mv.co.turn.has_turned = 1;
    fp->mv.co.turn.just_turned = 1;
    fp->facing_dir = f0;
    if (fp->kind == Ft_Kind_Popo) {
        HSD_GObj* ng = Player_GetEntityAtIndex(slot, 1);
        if (ng != NULL) {
            Fighter* nana = ng->user_data;
            struct Fighter_x1A88_xFC_t* e = nana->cpu.x444;
            float x = fp->input.lstick[0].x, y = fp->input.lstick[0].y;
            e->facing_dir = fp->facing_dir;
            e->lstick.x = (signed char) (int) (x * (x <= 0.0f ? 128.0f : 127.0f));
            e->lstick.y = (signed char) (int) (y * (y <= 0.0f ? 128.0f : 127.0f));
        }
    }
}

/* ---- Fighter_procUpdate (8181E8BC, before it): every fighter's self_vel.x history; Samus in her
 * state 0x163 on its first counted frame takes the stick's change as ground speed. */
void mu_20xx_proc_update(Fighter* fp)
{
    Mu20xxVel* v;
    int slot = fp->player_idx;
    if (!mu_20xx103_on() || slot < 0 || slot > 5) return;
    v = &mu_20xx_vel[slot];
    v->prev = v->cur;
    v->cur = fp->self_vel.x;
    v->count++;
    if (fp->kind == Ft_Kind_Samus && fp->motion_id == 0x163 && v->count == 1) {
        float before = fp->input.lstick[1].x;
        float d = fp->input.lstick[0].x - before;
        if (!(before * d <= 0.0f)) {
            float k = ((float*) fp->dat_attrs)[0x64 / 4] * fp->co_attrs.walk_accel_mul;
            fp->gr_vel = d * k + fp->gr_vel;
            fp->cur_pos.x = d * k + fp->cur_pos.x;
        }
    }
}

/* ---- ftCommon_CalcGroundAccel_AccelToLStickX (8181E98C): Samus in state 0x163 gets 20XX's own
 * ground acceleration; every other fighter and state the game's. True: handled. */
int mu_20xx_ground_accel(Fighter* fp, float accel_max, float target_max)
{
    float lsx, target, accel, gr;
    Mu20xxVel* v;
    if (!mu_20xx103_on() || fp->kind != Ft_Kind_Samus || fp->motion_id != 0x163) return 0;
    lsx = fp->input.lstick[0].x;
    target = lsx * target_max;
    gr = fp->gr_vel;
    if (target == 0.0f) {
        fp->xE4_ground_accel_1 = -gr;
        return 1;
    }
    accel = lsx * accel_max;
    v = &mu_20xx_vel[fp->player_idx < 6 ? fp->player_idx : 0];
    if (!(gr * accel >= 0.0f)) {       /* below 0 or NaN */
        v->count = 0;
    } else if (gr == 0.0f && accel * v->prev < 0.0f) {
        accel = accel + v->prev;
        v->count = 0;
    } else {
        float sum = gr + accel;
        float as = sum < 0 ? -sum : sum, at = target < 0 ? -target : target;
        if (as > at) accel = target - gr;
    }
    fp->xE4_ground_accel_1 = accel;
    return 1;
}

/* ftCommon_CalcSelfAccel_DriftFrom (8181EAFC): 20XX computes a drift from last frame's stick and
 * puts x74_self_accel.x back before the game's own drift runs: no effect. Nothing to port. */
