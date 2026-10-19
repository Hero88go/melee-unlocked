/* Native stage block, offsets refer to GrSp.dat grFunction. */
#include "sprouttower.h"
#include <math_native.h>
#include <math.h>
#include <string.h>
#include <melee/gr/granime.h>
#include <melee/gr/grdatfiles.h>
#include <melee/gr/grlib.h>
#include <melee/gr/ground.h>
#include <melee/ft/ftdevice.h>
#include <melee/ft/types.h>
#include <melee/lb/lb_00B0.h>
#include <melee/lb/lb_00F9.h>
#include <melee/lb/lbspdisplay.h>
#include <melee/pl/player.h>
#include <sysdolphin/baselib/archive.h>
#include <sysdolphin/baselib/debug.h>
#include <sysdolphin/baselib/random.h>
#include <sysdolphin/baselib/mobj.h>
#include <sysdolphin/baselib/lobj.h>

SpParams* sp_param;
float sp_time;
int sp_player_count;
bool sp_is_gc;

typedef struct SpBackground { HSD_JObj* mons[3]; } SpBackground;
typedef struct SpTower { HSD_AObj* anim[9]; GXColor colors[9]; } SpTower;
typedef struct SpUsokki { u8 state, night; s32 timer, effect_timer; } SpUsokki;
typedef struct SpOotchi { s32 state, timer; } SpOotchi;
typedef struct SpItomaru { u8 state, unused; } SpItomaru;
typedef struct SpDenryu {
    u8 state, delay, effect_timer; float angle_x, angle_y; s32 xEC, xF0;
} SpDenryu;
typedef struct SpHoho {
    u8 state, toggle, hidden; int count; HSD_JObj* anchor;
    Vec3 from, to; float progress; u8 next;
} SpHoho;
typedef struct SpYanyanma {
    u8 state, hidden, next; s32 repeat, timer; HSD_JObj* anchor;
    Vec3 from, to; float progress;
} SpYanyanma;
typedef struct SpGhost {
    u8 state, hidden, flicker; float target_alpha, alpha;
    HSD_JObj* anchor; int count; HSD_JObj* root;
    u8 unused[12]; s32 hold_timer, timer;
} SpGhost;
SP_FITS(SpTower); SP_FITS(SpHoho); SP_FITS(SpYanyanma); SP_FITS(SpGhost);

static Ground* sp_ground(HSD_GObj* g) { return HSD_GObjGetUserData(g); }
static HSD_JObj* sp_joint(HSD_GObj* g) { return GET_JOBJ(g); }
static void sp_anim(HSD_GObj* g, int anim)
{ grAnime_801C8138(g, sp_ground(g)->map_id, anim); }
static bool sp_ended(HSD_GObj* g, int joint)
{ return grAnime_801C83D0(g, joint, 7); }
static void sp_scale(HSD_JObj* j, float s)
{
    j->scale.x *= s; j->scale.y *= s; j->scale.z *= s;
    HSD_JObjSetMtxDirtySub(j);
}
static int sp_range(int lo, int hi) { return lo + HSD_Randi(hi - lo); }
static int sp_children(HSD_JObj* j)
{ int n = 0; for (j = j->child; j; j = j->next) ++n; return n; }
static HSD_JObj* sp_child(HSD_JObj* j, int index)
{ HSD_JObj* out; lb_80011E24(j, &out, index, -1); return out; }
static void sp_bind(HSD_GObj* parent, int child, int depth)
{
    HSD_GObj* g = mu_grTSeak_80223908(child);
    HSD_JObj* j = Ground_801C3FA4(g, 0);
    lb_8000C2F8(j, Ground_801C3FA4(parent, depth));
}
static void sp_lerp(Vec3* out, const Vec3* from, const Vec3* to, float t)
{
    out->x = mu_fmadds(to->x - from->x, t, from->x);
    out->y = mu_fmadds(to->y - from->y, t, from->y);
    out->z = mu_fmadds(to->z - from->z, t, from->z);
}
bool sp_night(void) { return sp_time > 5000.0f && sp_time <= 9000.0f; }
float sp_step(float v, float target, float step)
{
    if (v < target) { v += step; if (v > target) v = target; }
    else if (v > target) { v -= step; if (v < target) v = target; }
    return v;
}
static bool sp_unk(HSD_GObj* g) { return g != NULL; }
static void sp_delete(Ground_GObj* g) { (void) g; }
static void sp_basic_create(HSD_GObj* g) { sp_anim(g, 0); }

/* +000C: four animation channels write bytes in RGBA order. */
static void sp_color(void* obj, enum_t channel, HSD_ObjData* value)
{
    GXColor* c = obj;
    u8 v = (int) (value->fv * 255.0f);
    switch (channel) {
    case 4: c->r = v; break; case 5: c->g = v; break;
    case 6: c->b = v; break; case 7: c->a = v; break;
    }
}
/* +01D8: the current tower time drives the nine global stage colors. */
static void sp_tower_frame(HSD_GObj* g)
{
    static void (*const setters[9])(GXColor*) = {
        Ground_801C052C, Ground_801C05D4, Ground_801C0544,
        Ground_801C058C, Ground_801C05EC, Ground_801C05A4,
        Ground_801C055C, Ground_801C05BC, Ground_801C0574
    };
    SpTower* v = SP_VARS(SpTower, g);
    sp_time = lbGetJObjCurrFrame(sp_joint(g));
    for (int i = 0; i < 9; ++i) {
        if (v->anim[i]) {
            HSD_AObjReqAnim(v->anim[i], sp_time);
            HSD_AObjInterpretAnim(v->anim[i], &v->colors[i], sp_color);
            setters[i](&v->colors[i]);
        }
    }
    lb_800115F4();
}
/* +0374, +0488: the distant birds hide until their background cue. */
static void sp_background_create(HSD_GObj* g)
{
    SpBackground* v = SP_VARS(SpBackground, g);
    sp_anim(g, 0);
    if (sp_player_count <= 2 && !sp_is_gc) {
        for (int i = 0; i < 3; ++i) {
            HSD_GObj* bird = mu_grTSeak_80223908(13);
            if (bird) {
                HSD_JObj* j = sp_joint(bird);
                float random = HSD_Randf();
                HSD_JObjReqAnimAll(j, random * lbGetJObjEndFrame(sp_joint(g)));
                j = Ground_801C3FA4(bird, 0);
                HSD_JObjSetFlagsAll(j, JOBJ_HIDDEN);
                v->mons[i] = j;
                lb_8000C2F8(j, Ground_801C3FA4(g, sp_param->x000 + i + 1));
            }
        }
    }
}
static void sp_background_frame(HSD_GObj* g)
{
    SpBackground* v = SP_VARS(SpBackground, g);
    if (sp_time == sp_param->x07C) {
        HSD_JObjReqAnim(Ground_801C3FA4(g, sp_param->x000), 0.0f);
        for (int i = 0; i < 3; ++i)
            if (v->mons[i]) HSD_JObjClearFlagsAll(v->mons[i], JOBJ_HIDDEN);
    }
    if (sp_ended(g, sp_param->x000))
        for (int i = 0; i < 3; ++i)
            if (v->mons[i]) HSD_JObjSetFlagsAll(v->mons[i], JOBJ_HIDDEN);
}
static void sp_chonchie_frame(HSD_GObj* g)
{ if (sp_ended(g, 3)) sp_anim(g, HSD_Randf() > 0.1 ? 0 : 1); }

/* +060C: a four-state daytime cycle, with a separate nighttime pose. */
static void sp_ootchi_frame(HSD_GObj* g)
{
    SpOotchi* v = SP_VARS(SpOotchi, g);
    switch (v->state) {
    case 0:
        if (sp_ended(g, 2)) {
            v->state = 1; v->timer = sp_range(sp_param->x064, sp_param->x068);
            sp_anim(g, 1);
        }
        break;
    case 1:
        if (sp_night()) { v->state = 3; sp_anim(g, 3); }
        else if (v->timer > 0 && --v->timer == 0) {
            v->state = 2; v->timer = sp_range(sp_param->x064, sp_param->x068);
            sp_anim(g, 2);
        }
        break;
    case 2:
        if (sp_ended(g, 2)) { v->state = 1; sp_anim(g, 1); }
        break;
    case 3:
        if (!sp_night()) {
            HSD_JObjClearFlagsAll(sp_joint(g), JOBJ_HIDDEN);
            v->state = 0; sp_anim(g, 0);
        }
        if (sp_ended(g, 2)) HSD_JObjSetFlagsAll(sp_joint(g), JOBJ_HIDDEN);
        break;
    }
}
/* +0840: state 0/2 share the completion branch, as in the jump table. */
static void sp_itomaru_frame(HSD_GObj* g)
{
    SpItomaru* v = SP_VARS(SpItomaru, g);
    switch (v->state) {
    case 0: case 2:
        if (sp_ended(g, 1)) { v->state = 1; sp_anim(g, 1); }
        break;
    case 1:
        if (sp_ended(g, 1)) {
            if (sp_night()) {
                if (HSD_Randf() <= sp_param->x120) v->state = 2;
            } else v->state = 3;
            sp_anim(g, v->state);
        }
        break;
    case 3:
        if (sp_ended(g, 1)) { v->state = 4; HSD_JObjSetFlagsAll(sp_joint(g), JOBJ_HIDDEN); }
        break;
    case 4:
        if (sp_night()) {
            v->state = 0; sp_anim(g, 0);
            HSD_JObjClearFlagsAll(sp_joint(g), JOBJ_HIDDEN);
        }
        break;
    }
}
/* +0A54: the callback only emits the crossing effect; it returns false. */
static bool sp_water_check(Ground_GObj* ground, Fighter_GObj* fighter)
{
    Fighter* fp = HSD_GObjGetUserData(fighter);
    float water_y = Ground_801C0498() * sp_param->x008;
    Vec3 pos = fp->prev_pos;
    (void) ground;
    if (water_y > fp->cur_pos.y && water_y < pos.y) {
        pos.y = water_y;
        Ground_801C53EC(24);
        grLib_801C96F8(30000, 30, &pos);
    }
    return false;
}
static void sp_usokki_create(HSD_GObj* g)
{
    SpUsokki* v = SP_VARS(SpUsokki, g);
    v->state = v->night = 0;
    v->timer = sp_range(sp_param->x03C, sp_param->x040); v->effect_timer = 0;
    sp_anim(g, 0);
}
/* +0B04: the counter pauses during the nighttime animations. */
static void sp_usokki_frame(HSD_GObj* g)
{
    SpUsokki* v = SP_VARS(SpUsokki, g);
    if (v->timer > 0) {
        if (!v->night) { --v->timer; return; }
        if (!sp_night()) { sp_anim(g, v->state + 4); v->night = 0; return; }
    } else {
        v->timer = sp_range(sp_param->x03C, sp_param->x040);
        v->state = !v->state;
        if (!sp_night()) { sp_anim(g, v->state); v->night = 0; return; }
        sp_anim(g, v->state + 2); v->night = 1;
    }
    if (v->effect_timer > 0) --v->effect_timer;
    else {
        v->effect_timer = sp_param->x044;
        grLib_801C97DC(30007, 30, Ground_801C3FA4(g, sp_param->x048));
    }
}
static void sp_ootchi_create(HSD_GObj* g)
{
    SP_VARS(SpOotchi, g)->state = 3; sp_anim(g, 3);
    HSD_JObjReqAnimAll(sp_joint(g), 1000.0f); sp_scale(sp_joint(g), sp_param->x060);
}
static void sp_hoho_create(HSD_GObj* g)
{
    SpHoho* v = SP_VARS(SpHoho, g);
    v->state = 7; v->hidden = 1; v->anchor = NULL; v->toggle = 0;
    HSD_JObjSetFlagsAll(sp_joint(g), JOBJ_HIDDEN);
    sp_anim(g, 0); sp_scale(sp_joint(g), sp_param->x06C);
}
static void sp_denryu_create(HSD_GObj* g)
{
    SpDenryu* v = SP_VARS(SpDenryu, g);
    sp_anim(g, 0); v->state = 0; v->effect_timer = 0;
    v->delay = sp_range(sp_param->x094, sp_param->x096); v->xEC = v->xF0 = 0;
    sp_scale(sp_joint(g), sp_param->x080);
}
static void sp_ghost_create(HSD_GObj* g)
{
    SpGhost* v = SP_VARS(SpGhost, g);
    Ground* gp = sp_ground(g);
    /* +0EF0: halfword mask clears b8 while setting b9 and b10. */
    gp->x11_flags.b012 = (gp->x11_flags.b012 & ~4) | 3;
    v->state = 0; v->hidden = 1;
    v->timer = sp_range(sp_param->x0DC, sp_param->x0E0);
    HSD_JObjSetFlagsAll(sp_joint(g), JOBJ_HIDDEN);
    v->anchor = Ground_801C3FA4(Ground_GetMapGObj(5), sp_param->x02C);
    v->count = sp_children(v->anchor);
    sp_anim(g, 0);
    v->root = Ground_801C3FA4(g, 0); sp_scale(v->root, sp_param->x0D4);
}
static void sp_yanyanma_create(HSD_GObj* g)
{
    SpYanyanma* v = SP_VARS(SpYanyanma, g);
    v->state = 2; v->timer = sp_range(sp_param->x10C, sp_param->x110); v->hidden = 1;
    HSD_JObjSetFlagsAll(sp_joint(g), JOBJ_HIDDEN);
    v->anchor = Ground_801C3FA4(Ground_GetMapGObj(5), sp_param->x030);
    sp_anim(g, v->state); sp_scale(sp_joint(g), sp_param->x100);
}
static void sp_itomaru_create(HSD_GObj* g)
{
    SpItomaru* v = SP_VARS(SpItomaru, g);
    /* +10DC writes the halfword 1, so byte state is zero on the console. */
    v->state = 0; v->unused = 1;
    HSD_JObjSetFlagsAll(sp_joint(g), JOBJ_HIDDEN);
    sp_anim(g, v->state); sp_scale(sp_joint(g), sp_param->x11C);
}
static void sp_upah_create(HSD_GObj* g)
{
    sp_scale(sp_joint(g), sp_param->x058);
    HSD_JObj* j = Ground_801C3FA4(g, 0); sp_anim(g, 0);
    grLib_801C97DC(30006, 30, j);
}
static void sp_chonchie_create(HSD_GObj* g)
{
    HSD_JObj* j = sp_joint(g); sp_scale(j, sp_param->x05C);
    sp_anim(g, 0); grLib_801C97DC(30006, 30, j->child);
}

/* +1274: eight animation states, with independently smoothed head tracking. */
static void sp_denryu_frame(HSD_GObj* g)
{
    SpDenryu* v = SP_VARS(SpDenryu, g);
    if (sp_ended(g, 3)) {
        if (v->delay) --v->delay;
        else {
            switch (v->state) {
            case 0:
                v->state = HSD_Randf() <= (sp_night() ? sp_param->x088 : sp_param->x084) ? 1 : 2;
                break;
            case 1: case 4:
                v->state = 0; v->delay = sp_range(sp_param->x094, sp_param->x096); break;
            case 2: case 7:
                v->state = 3; v->delay = sp_range(sp_param->x098, sp_param->x09A); break;
            case 3:
                v->state = HSD_Randf() <= (sp_night() ? sp_param->x090 : sp_param->x08C) ? 5 : 4;
                break;
            case 5:
                v->state = 6; v->effect_timer = 0;
                v->delay = sp_night() ? sp_range(sp_param->x0A0, sp_param->x0A2)
                                     : sp_range(sp_param->x09C, sp_param->x09E);
                break;
            case 6: v->state = 7; break;
            }
            /* State 2's jump target is +12D8, distinct from state 1/7. */
        }
        sp_anim(g, v->state);
    }
    if (v->state == 0) {
        HSD_JObj* pitch = Ground_801C3FA4(g, sp_param->x0B4);
        HSD_JObj* yaw = Ground_801C3FA4(g, sp_param->x0B0);
        float tx, ty;
        if (v->delay <= 1) {
            tx = ((HSD_Joint*) yaw->id)->rotation.y;
            ty = ((HSD_Joint*) pitch->id)->rotation.z;
        }
        else {
            Vec3 origin, closest; bool found = false; float distance = 1000.0f;
            lb_8000B1CC(pitch, NULL, &origin);
            for (int i = 0; i < 6; ++i) {
                if (Player_GetPlayerSlotType(i) != 3) {
                    Vec3 pos; Player_LoadPlayerCoords(i, &pos);
                    float d = origin.x - pos.x; if (d < 0) d = -d;
                    if (sp_param->x0C0 > d && d < distance) { closest = pos; distance = d; found = true; }
                }
            }
            tx = ty = 0.0f;
            if (found) {
                tx = (float) ((double) atan2f(origin.z - closest.z, origin.x - closest.x) + 1.5707963267948966);
                tx = tx > sp_param->x0B8 ? -sp_param->x0B8 : -tx;
                if (sp_param->x0B8 < tx) tx = sp_param->x0B8;
                ty = (float) ((double) atan2f(origin.z - closest.z, origin.y - closest.y) + 1.5707963267948966);
                if (sp_param->x0BC > ty) ty = sp_param->x0BC;
                if (ty > 0) ty = 0;
            }
        }
        v->angle_x = sp_step(v->angle_x, tx, sp_param->x0C4);
        v->angle_y = sp_step(v->angle_y, ty, sp_param->x0C4);
        yaw->rotate.y = v->angle_x; pitch->rotate.z = v->angle_y;
        HSD_JObjSetMtxDirtySub(yaw); HSD_JObjSetMtxDirtySub(pitch);
    } else {
        if (v->state == 6) {
            if (v->effect_timer) --v->effect_timer;
            else { v->effect_timer = sp_param->x0A4;
                grLib_801C97DC(30007, 30, Ground_801C3FA4(g, sp_param->x0A8)); }
        }
        v->angle_x = v->angle_y = 0.0f;
    }
}

static void sp_water_create(HSD_GObj* g)
{
    sp_anim(g, 0); ftCo_800C07F8(g, 6, sp_water_check);
    if (sp_player_count <= 3) {
        if (HSD_Randi(2)) sp_bind(g, 9, sp_param->x010);
        else sp_bind(g, 10, sp_param->x014);
    }
}
static void sp_create_hoho(HSD_JObj* anchor)
{
    HSD_GObj* g = mu_grTSeak_80223908(12);
    SpHoho* v = SP_VARS(SpHoho, g);
    v->anchor = anchor; v->count = sp_children(anchor);
}
/* +1A24: decoration budget and random ordering are kept. */
static void sp_floor_create(HSD_GObj* g)
{
    sp_anim(g, 0);
    if (sp_player_count <= 3) sp_bind(g, 11, sp_param->x018);
    else HSD_JObjSetFlags(Ground_801C3FA4(g, sp_param->x018), JOBJ_HIDDEN);
    sp_bind(g, HSD_Randf() > sp_param->x028 ? 8 : 14, sp_param->x020);
    if ((!sp_is_gc && sp_player_count <= 3) || (sp_is_gc && sp_player_count <= 1))
        sp_bind(g, 17, sp_param->x024);
    else if (!HSD_Randi(2)) { sp_bind(g, 17, sp_param->x024); return; }
    sp_create_hoho(Ground_801C3FA4(g, sp_param->x01C));
}

/* +1C80: daytime flights between the anchor and its first child. */
static void sp_yanyanma_frame(HSD_GObj* g)
{
    SpYanyanma* v = SP_VARS(SpYanyanma, g);
    if (v->state == 2) {
        if (!sp_night()) {
            if (v->timer > 0) --v->timer;
            else {
                v->timer = sp_range(sp_param->x10C, sp_param->x110);
                v->repeat = sp_range(sp_param->x114, sp_param->x118);
                lb_8000B1CC(sp_child(v->anchor, 1), NULL, &v->from);
                lb_8000B1CC(v->anchor, NULL, &v->to);
                v->state = 0; v->progress = 0; v->next = 1; sp_anim(g, 0);
            }
        }
    } else if (v->state == 1) {
        if (sp_ended(g, 1)) {
            if (v->repeat > 0) { --v->repeat; sp_anim(g, v->state); }
            else { Vec3 tmp = v->from; v->from = v->to; v->to = tmp;
                v->progress = 0; v->state = 0; v->next = 2; sp_anim(g, 0); }
        }
    } else if (v->state == 0) {
        if (v->progress < 1) {
            if (sp_ended(g, 1)) sp_anim(g, v->state);
            v->progress += sp_param->x104; if (v->progress > 1) v->progress = 1;
            HSD_JObj* j = sp_joint(g); sp_lerp(&j->translate, &v->from, &v->to, v->progress);
            float angle = atan2f(v->to.x - v->from.x, v->to.z - v->from.z);
            j->rotate.y = sp_step(j->rotate.y, angle, sp_param->x108);
            HSD_JObjSetMtxDirtySub(j);
        } else if (v->progress == 1 && sp_ended(g, 1)) { v->state = v->next; sp_anim(g, v->state); }
    }
    /* +1CB0 compares the first two state bytes as one big-endian halfword. */
    if (v->state == 2 && v->hidden == 0) {
        HSD_JObjSetFlagsAll(sp_joint(g), JOBJ_HIDDEN); v->hidden = 1;
    } else if (v->state != 2 && v->hidden) {
        HSD_JObjClearFlagsAll(sp_joint(g), JOBJ_HIDDEN); v->hidden = 0;
    }
}
/* +20EC: the first random draw is intentionally discarded unless it is -1. */
static void sp_hoho_frame(HSD_GObj* g)
{
    SpHoho* v = SP_VARS(SpHoho, g);
    if (!v->anchor) return;
    if (v->state == 7 && !v->hidden) { HSD_JObjSetFlagsAll(sp_joint(g), JOBJ_HIDDEN); v->hidden = 1; }
    if (v->state == 7) {
        if (!sp_night()) return;
        lb_8000B1CC(v->anchor, NULL, &v->from);
        HSD_JObj* dst = HSD_Randi(v->count) == -1 ? v->anchor : sp_child(v->anchor, HSD_Randi(v->count) + 1);
        lb_8000B1CC(dst, NULL, &v->to);
        v->state = 1; v->progress = 0; v->next = 2; sp_anim(g, 1); return;
    }
    if (v->hidden) { HSD_JObjClearFlagsAll(sp_joint(g), JOBJ_HIDDEN); v->hidden = 0; }
    switch (v->state) {
    case 0:
        if (sp_ended(g, 2)) {
            if (!sp_night()) { v->state = 3; sp_anim(g, 3); }
            else if (HSD_Randf() <= sp_param->x070) { v->state = 6; sp_anim(g, 6); }
            else { v->state = v->toggle + 4; sp_anim(g, v->state); v->toggle = !v->toggle; }
        }
        break;
    case 1:
        if (v->progress < 1) {
            v->progress = mu_fmadds(sp_param->x074, Ground_801C0498(), v->progress);
            if (v->progress > 1) v->progress = 1;
            HSD_JObj* j = sp_joint(g); sp_lerp(&j->translate, &v->from, &v->to, v->progress);
            j->rotate.y = atan2f(v->to.x - v->from.x, v->to.z - v->from.z);
            HSD_JObjSetMtxDirtySub(j);
        } else if (v->progress == 1) {
            sp_joint(g)->rotate.y = 0; HSD_JObjSetMtxDirtySub(sp_joint(g));
            v->state = v->next; sp_anim(g, v->state);
        }
        break;
    case 3:
        if (sp_ended(g, 2)) {
            Vec3 tmp = v->from; v->from = v->to; v->to = tmp;
            v->progress = 0; v->state = 1; v->next = 7; sp_anim(g, 1);
        }
        break;
    case 2: case 4: case 5: case 6:
        if (sp_ended(g, 2)) { v->state = 0; sp_anim(g, 0); }
        break;
    }
}

/* +3088: fade in, hold with optional flicker, fade out, then wait for night. */
static void sp_ghost_frame(HSD_GObj* g)
{
    SpGhost* v = SP_VARS(SpGhost, g);
    switch (v->state) {
    case 0:
        if (sp_night()) {
            if (v->timer > 0) --v->timer;
            else {
                v->state = 1;
                v->root->rotate.y = mu_fmsubs(HSD_Randf(), sp_param->x0EC + sp_param->x0EC, sp_param->x0EC);
                HSD_JObj* target = HSD_Randi(v->count) == -1 ? v->anchor : sp_child(v->anchor, HSD_Randi(v->count) + 1);
                lb_8000B1CC(target, NULL, &v->root->translate);
                v->root->translate.x /= Ground_801C0498();
                v->root->translate.y /= Ground_801C0498();
                v->root->translate.z /= Ground_801C0498();
                HSD_JObjSetMtxDirtySub(v->root);
            }
        }
        break;
    case 1:
        v->alpha = sp_step(v->alpha, 1, sp_param->x0D8);
        if (v->alpha >= 1) {
            v->state = 2;
            v->hold_timer = sp_range(sp_param->x0E4, sp_param->x0E8);
            v->flicker = HSD_Randf() <= sp_param->x0F4;
            if (v->flicker) v->target_alpha = mu_fmadds(HSD_Randf(), sp_param->x0FC - sp_param->x0F8, sp_param->x0F8);
        }
        break;
    case 2:
        if (v->hold_timer > 0) --v->hold_timer; else v->state = 3;
        if (v->flicker) {
            v->alpha = sp_step(v->alpha, v->target_alpha, sp_param->x0F0);
            if (v->alpha == v->target_alpha)
                v->target_alpha = mu_fmadds(HSD_Randf(), sp_param->x0FC - sp_param->x0F8, sp_param->x0F8);
        }
        break;
    case 3:
        v->alpha = sp_step(v->alpha, 0, sp_param->x0D8);
        if (v->alpha <= 0) { v->state = 0; v->timer = sp_range(sp_param->x0DC, sp_param->x0E0); }
        break;
    }
    sp_alpha(v->root, v->alpha);
    if (v->hidden && v->alpha > 0) { v->hidden = 0; HSD_JObjClearFlagsAll(sp_joint(g), JOBJ_HIDDEN); }
    if (!v->hidden && v->alpha == 0) { v->hidden = 1; HSD_JObjSetFlagsAll(sp_joint(g), JOBJ_HIDDEN); }
}

/* +40BC and +4198. */
static void sp_tower_create(HSD_GObj* g)
{
    SpTower* v = SP_VARS(SpTower, g);
    sp_anim(g, 0); mu_grTSeak_80223908(15);
    if (!(sp_is_gc && sp_player_count > 2)) {
        if (HSD_Randf() <= sp_param->x038) {
            HSD_JObj* anchor = Ground_801C3FA4(g, sp_param->x034);
            int count = sp_children(anchor);
            if (count) {
                HSD_JObj* target = sp_child(anchor, HSD_Randi(count) + 1);
                if (target) {
                    HSD_GObj* bird = mu_grTSeak_80223908(7);
                    lb_8000C2F8(Ground_801C3FA4(bird, 0), target);
                }
            }
        }
        if (sp_player_count <= 3 && !sp_is_gc) mu_grTSeak_80223908(16);
    }
    const GXColor* colors[9] = { Ground_801C0604(), Ground_801C0690(), Ground_801C0618(),
        Ground_801C0654(), Ground_801C06A4(), Ground_801C0668(), Ground_801C062C(),
        Ground_801C067C(), Ground_801C0640() };
    for (int i = 0; i < 9; ++i) v->colors[i] = *colors[i];
    HSD_MatAnimJoint* data = HSD_ArchiveGetPublicAddress(grDatFiles_801C6330(0)->unk0, "bubble_matanim_joint");
    if (data) {
        HSD_MatAnim* anim = DP(data->matanim);
        for (int i = 0; i < 9; ++i) { v->anim[i] = HSD_AObjLoadDesc(DP(anim->aobjdesc)); anim = DP(anim->next); }
    }
}
static void sp_init(void)
{
    sp_param = Ground_GetYakumonoParam();
    if (!sp_param) __assert("sprouttower.c", 0xABA, "yakumono not found");
    sp_player_count = 0;
    for (int i = 0; i < 6; ++i) if (Player_GetPlayerSlotType(i) != 3) ++sp_player_count;
    /* Jev review, native_hardware_branch: desktop (0.64, confidence 0.46).
     * Question: which console hardware branch applies to a native desktop?
     * Answer: IsGameCube=0, preserving the non-original-hardware decoration path.
     * The request and response are retained in this folder. */
    sp_is_gc = false; sp_time = 0;
    sp_shadow_install();
    for (int i = 0; i < 7; ++i) mu_grTSeak_80223908(i);
    mu_grTSeak_80223908(18);
    Ground_801C39C0(); Ground_801C3BB4();
    Ground_801C438C(sp_param->x124 * Ground_801C0498());
}

/* +3F34, +3F68: retained debugging utilities. No exported callback calls them. */
void sp_debug_map_time(int map, float frame)
{ HSD_JObjReqAnimAll(GET_JOBJ(Ground_GetMapGObj(map)), frame); }
void sp_debug_time(float time)
{
    float frame = (float) (int) (time * 11000.0f);
    for (int i = 1; i <= 6; ++i) sp_debug_map_time(i, frame);
    for (HSD_GObj* g = HSD_GObjPLinkHead[3]; g; g = g->next)
        if (g->classifier == 12 || g->classifier == 13)
            for (HSD_LObj* l = GET_LOBJ(g); l; l = l->next) HSD_LObjReqAnimAll(l, frame);
}
/* +42BC: report the day-cycle clock with the original two float multiplies. */
void sp_float_to_time(float time, int* hour, int* minute, char* period)
{
    if (time < 0 || time > 1) { *hour = 6; *minute = 0; *period = 'A'; return; }
    volatile float hours = time * 24.0f;
    int total = (int) (hours * 60.0f);
    int h = total / 60 + 6; *minute = total % 60;
    if (h > 23) h -= 24;
    if (h == 0) { h = 12; *period = 'A'; }
    else if (h > 11) { if (h != 12) h -= 12; *period = 'P'; }
    else *period = 'A';
    *hour = h;
}

#define SP_CB(create, frame, bits) { create, sp_unk, frame, sp_delete, bits }
static StageCallbacks sp_maps[] = {
    SP_CB(mu_gr_gobj_nop, mu_gr_gobj_nop, 0),
    SP_CB(sp_basic_create, mu_gr_gobj_nop, 0),
    SP_CB(sp_background_create, sp_background_frame, 0),
    SP_CB(sp_water_create, mu_gr_gobj_nop, 0),
    SP_CB(sp_floor_create, mu_gr_gobj_nop, 0),
    SP_CB(sp_tower_create, sp_tower_frame, 0x80000000),
    SP_CB(sp_basic_create, mu_gr_gobj_nop, 0),
    SP_CB(sp_basic_create, mu_gr_gobj_nop, 0),
    SP_CB(sp_usokki_create, sp_usokki_frame, 0),
    SP_CB(sp_upah_create, mu_gr_gobj_nop, 0),
    SP_CB(sp_chonchie_create, sp_chonchie_frame, 0),
    SP_CB(sp_ootchi_create, sp_ootchi_frame, 0),
    SP_CB(sp_hoho_create, sp_hoho_frame, 0),
    SP_CB(sp_basic_create, mu_gr_gobj_nop, 0),
    SP_CB(sp_denryu_create, sp_denryu_frame, 0),
    SP_CB(sp_ghost_create, sp_ghost_frame, 0),
    SP_CB(sp_yanyanma_create, sp_yanyanma_frame, 0),
    SP_CB(sp_itomaru_create, sp_itomaru_frame, 0),
    SP_CB(sp_shadow_create, sp_shadow_frame, 0),
};
const MuGrStage mu_gr_sprouttower = {
    .name = "Sprout Tower", .file = "/GrSp.dat", .map_gobjs = sp_maps,
    .on_init = sp_init, .on_demo_init = mu_gr_demo_nop,
    .on_load = mu_gr_event_nop, .on_start = mu_gr_event_nop,
    .callback4 = mu_gr_false, .on_touch_line = mu_gr_no_touch_line,
    .on_check_shadow_render = sp_shadow_check, .flags = MU_GR_READY,
};
