/* GrLm.dat, exports 1 and 3..9. Native state uses Ground.u and disc parameters
 * retain their big-endian layout. Offsets below refer to the grFunction block. */
#include "../common/mu_gr_common.h"
#include "../common/mu_gr_mex.h"
#include <melee/cm/camera.h>
#include <melee/ft/types.h>
#include <melee/gr/granime.h>
#include <melee/gr/grlib.h>
#include <melee/gr/grmaterial.h>
#include <melee/gr/ground.h>
#include <melee/it/item.h>
#include <melee/it/itcoll.h>
#include <melee/lb/lb_00B0.h>
#include <melee/lb/lb_00F9.h>
#include <melee/mp/mplib.h>
#include <melee/pl/player.h>
#include <sysdolphin/baselib/debug.h>
#include <sysdolphin/baselib/dobj.h>
#include <sysdolphin/baselib/gobj.h>
#include <sysdolphin/baselib/jobj.h>
#include <sysdolphin/baselib/mobj.h>
#include <sysdolphin/baselib/psstructs.h>
#include <sysdolphin/baselib/random.h>
#include <math.h>

typedef struct LmPillarParam { s32 joint; f32 unused, particle_x; } DISC_STRUCT LmPillarParam;
typedef struct LmParam {
    s32 lower_hp, upper_hp;
    f32 inside_left, inside_right, inside_bottom, inside_top;
    s32 rebuild_wait, rebuild_warning, rebuild_frames;
    f32 ghost_left, ghost_right, ghost_bottom, ghost_top, unused34;
    f32 ghost_bounce, ghost_damping, ghost_bounce_scale;
    f32 ghost_life_min, ghost_life_max, ghost_wait_min, ghost_wait_max;
    LmPillarParam pillar[4];
} DISC_STRUCT LmParam;

typedef struct LmFloor {
    HSD_GObj* wall[4];
    HSD_GObj* collapsed[2];
    HSD_GObj* pillar[4];
    u8 state[4];
    int rebuild_wait, rebuild_frames, quake_timer;
    HSD_GObj* ghost[2];
} LmFloor;
typedef struct LmPillar {
    HSD_GObj* self;
    int index;
    f32 hp;
    Item_GObj* item;
    u8 state;
} LmPillar;
typedef struct LmGhost {
    int life, collision, move_timer, wait;
    f32 alpha, left, bottom, width, height, x, y, vx, vy, target_vx, target_vy, bounce;
} LmGhost;
typedef struct LmWall { int unused, index; } LmWall;
/* The console leaves Ground+0xC4..0xDF unused. A native pointer may be wider. */
#define LM_VARS(type, gp) ((type*) (void*) &(gp)->u)
_Static_assert(sizeof(LmFloor) <= sizeof(((Ground*) 0)->u), "floor state fits");
_Static_assert(sizeof(LmGhost) <= sizeof(((Ground*) 0)->u), "ghost state fits");
_Static_assert(sizeof(LmPillar) <= sizeof(((Ground*) 0)->u), "pillar state fits");
static LmParam* lm_param;
static Ground* lm_ground(HSD_GObj* gobj) { return HSD_GObjGetUserData(gobj); }
static LmFloor* lm_floor(void) { return LM_VARS(LmFloor, lm_ground(Ground_GetMapGObj(2))); }
static bool lm_false(Ground_GObj* gobj) { (void) gobj; return false; }

/* +127C: walk this joint, child and sibling, changing every material alpha. */
static void lm_alpha(HSD_JObj* joint, f32 alpha)
{
    HSD_DObj* dobj;
    for (dobj = joint->u.dobj; dobj != NULL; dobj = dobj->next) {
        dobj->mobj->mat->alpha = alpha;
    }
    if (joint->child != NULL) lm_alpha(joint->child, alpha);
    if (joint->next != NULL) lm_alpha(joint->next, alpha);
}

static void lm_anim(Ground_GObj* gobj)
{
    grAnime_801C8138(gobj, lm_ground(gobj)->map_id, 0);
}
static void lm_background(Ground_GObj* gobj)
{
    lm_anim(gobj);
    lm_ground(gobj)->x10_flags.b2 = 0;
}
static void lm_coll_anim(Ground_GObj* gobj)
{
    lm_anim(gobj);
    Ground_InitMapColl(GET_JOBJ(gobj), lm_ground(gobj)->map_id);
}
/* +11D4 writes only scale.y (+30 after the quaternion), not a uniform scale. */
static void lm_scale(HSD_GObj* gobj, f32 scale)
{
    HSD_JObj* joint = Ground_801C3FA4(gobj, 0);
    joint->scale.y = scale;
    HSD_JObjSetMtxDirtySub(joint);
}
static int lm_break_map(int index) { return (unsigned) index < 4 ? index + 6 : 0; }
static void lm_pillar_state(HSD_GObj* gobj, int state)
{
    grAnime_801C8138(gobj, lm_ground(gobj)->map_id, state);
    LM_VARS(LmPillar, lm_ground(gobj))->state = state;
}
static void lm_pillar_init(HSD_GObj* gobj)
{
    LmPillar* pv = LM_VARS(LmPillar, lm_ground(gobj));
    pv->hp = (f32) (pv->index == 0 || pv->index == 2 ? lm_param->lower_hp : lm_param->upper_hp);
    lm_pillar_state(pv->self, 0);
    it_80271508(pv->item, HurtCapsule_Enabled);
}
static void lm_floor_init(HSD_GObj* gobj)
{
    LmFloor* fv = LM_VARS(LmFloor, lm_ground(gobj));
    int i;
    fv->rebuild_frames = 0;
    fv->rebuild_wait = 0;
    for (i = 0; i < 4; i++) {
        fv->state[i] = 0;
        grAnime_801C8138(fv->wall[i], lm_break_map(i), 0);
        lm_pillar_init(fv->pillar[i]);
    }
}
/* +17FC and +18F8: a hurt pillar starts its wall breaking immediately. */
static void lm_update_walls(void)
{
    static const int collision[4] = { 4, 3, 6, 5 };
    static const int order[4] = { 1, 3, 0, 2 };
    LmFloor* fv = lm_floor();
    int i;
    for (i = 0; i < 4; i++) {
        LmPillar* pv = LM_VARS(LmPillar, lm_ground(fv->pillar[i]));
        if (pv->state != 0 && fv->state[i] == 0 && fv->wall[i] != NULL) {
            Vec3 pos;
            lb_8000B1CC(Ground_801C3FA4(fv->pillar[i], 0), NULL, &pos);
            grLib_801C96F8(0x7530, 30, &pos);
            grLib_801C96F8(0x7531, 30, &pos);
            grLib_801C96F8(0x7532, 30, &pos);
            Camera_RequestQuake(2, NULL);
            grAnime_801C8138(fv->wall[i], lm_break_map(i), 1);
            fv->state[i] = 1;
            mu_gr_mex_sfx(HSD_Randi(2) + 5);
        }
    }
    for (i = 0; i < 4; i++) {
        int index = order[i];
        if (fv->state[index] == 0) mpJointListAdd(collision[index]);
        else mpLib_80057BC0(collision[index]);
    }
}
/* +15C4: positive HP flinches (state 4); depleted HP breaks (state 1).
 * Upper pillars lose HP only after the lower pillar on that side is damaged. */
MU_NO_CONTRACT static void lm_pillar_hurt(Item_GObj* item, Ground* gp, Vec3* pos,
                                         HSD_GObj* source, f32 damage)
{
    LmPillar* pv = LM_VARS(LmPillar, gp);
    LmFloor* fv = lm_floor();
    (void) pos; (void) source;
    if ((pv->index != 1 || fv->state[0] != 0) &&
        (pv->index != 3 || fv->state[2] != 0)) pv->hp = pv->hp - damage;
    if (!(pv->hp <= 0.0f)) {
        lm_pillar_state(pv->self, 4);
        mu_gr_mex_sfx(HSD_Randi(3));
    } else {
        lm_pillar_state(pv->self, 1);
        it_80271508(item, HurtCapsule_Intangible);
        mu_gr_mex_sfx(HSD_Randi(3) + 8);
        lm_update_walls();
    }
}
static void lm_pillar_destroy(HSD_GObj* gobj)
{
    LmPillar* pv = LM_VARS(LmPillar, lm_ground(gobj));
    if (pv->item != NULL) Item_8026A8EC(pv->item);
}
static void lm_pillar_create(Ground_GObj* gobj)
{
    Ground* gp = lm_ground(gobj);
    LmPillar* pv = LM_VARS(LmPillar, gp);
    pv->item = grMaterial_801C8CFC(0, 0, gp, Ground_801C3FA4(gobj, 0), NULL,
                                  lm_pillar_hurt, NULL);
    grMaterial_801C8DE0(pv->item, 0.0f, 0.0f, 8.0f, 0.0f, 28.0f, 8.0f, 2.0f);
    grMaterial_801C8E08(pv->item);
    gp->x1C_callback = lm_pillar_destroy;
    pv->self = gobj;
    lm_pillar_state(gobj, 0);
}
static void lm_pillar_frame(Ground_GObj* gobj)
{
    LmPillar* pv = LM_VARS(LmPillar, lm_ground(gobj));
    if (pv->state == 1 && !lb_8000B09C(Ground_801C3FA4(gobj, 0))) lm_pillar_state(gobj, 2);
    if (pv->state == 4 && !lb_8000B09C(Ground_801C3FA4(gobj, 0))) lm_pillar_state(gobj, 0);
}
/* +CAC: after a wall's break animation, update the two collapsed models.
 * The packed comparison at +DDC ignores both lower-floor state bytes. */
MU_NO_CONTRACT static void lm_wall_frame(Ground_GObj* gobj)
{
    LmFloor* fv = lm_floor();
    const int index = LM_VARS(LmWall, lm_ground(gobj))->index;
    Vec3 pos;
    Ground_UpdateMapColl(gobj);
    if (fv->state[index] != 1 || lb_8000B09C(Ground_801C3FA4(gobj, 0))) return;
    grAnime_801C8138(gobj, lm_ground(gobj)->map_id, 3);
    Camera_RequestQuake(2, NULL);
    lb_8000B1CC(Ground_801C3FA4(fv->pillar[index], 0), NULL, &pos);
    pos.x = pos.x + lm_param->pillar[index].particle_x;
    grLib_801C96F8(0x7533, 30, &pos);
    grLib_801C96F8(0x7534, 30, &pos);
    mu_gr_mex_sfx(4);
    fv->state[index] = 2;
    if (index == 0 && fv->state[1] == 0) grAnime_801C8138(fv->wall[1], 7, 4);
    if (index == 2 && fv->state[3] == 0) grAnime_801C8138(fv->wall[3], 9, 4);
    if (fv->state[1] == 2 && fv->state[3] == 2) {
        grAnime_801C8138(fv->collapsed[0], 11, 2);
        grAnime_801C8138(fv->collapsed[1], 10, 2);
        fv->rebuild_wait = lm_param->rebuild_wait;
    } else if (index == 1) grAnime_801C8138(fv->collapsed[0], 11, 1);
    else if (index == 3) grAnime_801C8138(fv->collapsed[1], 10, 1);
}
/* +125C, registered on the moving collision joint. */
static void lm_ghost_stand(void* user, int joint_id, CollData* coll, int arg3,
                           mpLib_GroundEnum event, f32 arg5)
{
    (void) joint_id; (void) coll; (void) arg3; (void) arg5;
    if (event == 1) LM_VARS(LmGhost, (Ground*) user)->bounce = -lm_param->ghost_bounce;
}
MU_NO_CONTRACT static int lm_random(f32 low, f32 high)
{
    f32 product = (high - low) * HSD_Randf();
    return (int) (low + product);
}
static void lm_ghost_create(Ground_GObj* gobj)
{
    Ground* gp = lm_ground(gobj);
    LmGhost* gv = LM_VARS(LmGhost, gp);
    gp->x11_flags.b012 = 3;
    Ground_InitMapColl(GET_JOBJ(gobj), gp->map_id);
    gv->collision = gp->map_id == 3 ? 1 : 2;
    gv->left = gp->map_id == 3 ? -lm_param->ghost_right : lm_param->ghost_left;
    gv->bottom = lm_param->ghost_bottom;
    gv->width = lm_param->ghost_right - lm_param->ghost_left;
    gv->height = lm_param->ghost_top - lm_param->ghost_bottom;
    mpJointSetCb1(gv->collision, gp, lm_ghost_stand);
    lm_alpha(GET_JOBJ(gobj), 0.0f);
    gv->wait = lm_random(lm_param->ghost_wait_min, lm_param->ghost_wait_max);
}
MU_NO_CONTRACT static void lm_ghost_enable(HSD_GObj* gobj)
{
    LmGhost* gv = LM_VARS(LmGhost, lm_ground(gobj));
    HSD_JObj* joint;
    f32 product = gv->width * HSD_Randf();
    gv->x = product + gv->left;
    product = gv->height * HSD_Randf();
    gv->y = product + gv->bottom;
    joint = Ground_801C3FA4(gobj, 0);
    joint->translate.x = gv->x;
    joint->translate.y = gv->y;
    HSD_JObjSetMtxDirtySub(joint);
    mpJointListAdd(gv->collision);
    gv->life = lm_random(lm_param->ghost_life_min, lm_param->ghost_life_max);
}
MU_NO_CONTRACT static void lm_ghost_frame(Ground_GObj* gobj)
{
    LmGhost* gv = LM_VARS(LmGhost, lm_ground(gobj));
    if (gv->life == 0) {
        if (gv->wait != 0) gv->wait--;
        else lm_ghost_enable(gobj);
        if (gv->alpha > 0.0f) {
            gv->alpha = (f32) ((double) gv->alpha - 0.025);
            lm_alpha(Ground_801C3FA4(gobj, 0), gv->alpha);
            if (fabs((double) gv->alpha) < 0.1) mpLib_80057BC0(gv->collision);
        }
    } else {
        gv->life--;
        if (gv->alpha < 1.0f) {
            gv->alpha = (f32) ((double) gv->alpha + 0.025);
            lm_alpha(Ground_801C3FA4(gobj, 0), gv->alpha);
        }
        if (gv->life <= 0) gv->wait = lm_random(lm_param->ghost_wait_min, lm_param->ghost_wait_max);
        if (gv->move_timer > 0) {
            HSD_JObj* joint;
            f32 next_x, next_y, product;
            gv->vx = (gv->target_vx + gv->vx) / 100.0f;
            gv->vy = (gv->target_vy + gv->vy) / 100.0f;
            next_x = gv->vx + gv->x;
            next_y = gv->vy + gv->y;
            if (next_x < gv->left || next_x > gv->left + gv->width) {
                gv->vx = 0.0f; gv->target_vx = -gv->target_vx;
            }
            if (next_y < gv->bottom || next_y > gv->bottom + gv->height) {
                gv->vy = 0.0f; gv->target_vy = -gv->target_vy;
            }
            gv->x = gv->vx + gv->x;
            gv->y = gv->vy + gv->y;
            product = gv->bounce * lm_param->ghost_damping;
            gv->bounce = gv->bounce - product;
            product = gv->bounce * lm_param->ghost_bounce_scale;
            if (fabs((double) product) < -0.05) product = 0.0f;
            /* +974 compares fabs(product) with -0.05, so its zero branch is unreachable. */
            gv->bounce = product;
            joint = Ground_801C3FA4(gobj, 0);
            joint->translate.x = gv->x;
            joint->translate.y = gv->y + gv->bounce;
            HSD_JObjSetMtxDirtySub(joint);
            gv->move_timer--;
        } else {
            f32 draw = HSD_Randf();
            gv->target_vx = ((draw + draw) - 1.0f) * 5.0f;
            draw = HSD_Randf();
            gv->target_vy = ((draw + draw) - 1.0f) * 5.0f;
            gv->move_timer = HSD_Randi(120) + 60;
        }
    }
    Ground_UpdateMapColl(gobj);
}

/* +1428 checks both entities of every nonempty player slot. */
static bool lm_fighter_inside(void)
{
    int player, entity;
    for (player = 0; player < 6; player++) {
        if (Player_GetPlayerSlotType(player) == 3) continue;
        for (entity = 0; entity < 2; entity++) {
            HSD_GObj* gobj = Player_GetEntityAtIndex(player, entity);
            if (gobj != NULL) {
                Fighter* fp = HSD_GObjGetUserData(gobj);
                if (fp->cur_pos.x > lm_param->inside_left && fp->cur_pos.x < lm_param->inside_right &&
                    fp->cur_pos.y > lm_param->inside_bottom && fp->cur_pos.y < lm_param->inside_top) return true;
            }
        }
    }
    return false;
}
static void lm_front_create(Ground_GObj* gobj)
{
    *LM_VARS(f32, lm_ground(gobj)) = 0.0f;
    lm_alpha(GET_JOBJ(gobj), 0.0f);
    lm_ground(gobj)->x11_flags.b012 = 3;
}
static void lm_front_frame(Ground_GObj* gobj)
{
    LmFloor* fv = lm_floor();
    f32* alpha = LM_VARS(f32, lm_ground(gobj));
    bool hide = fv->state[0] || fv->state[1] || fv->state[2] || fv->state[3] || lm_fighter_inside();
    if (hide) {
        if (*alpha > 0.0f) {
            *alpha = (f32) ((double) *alpha - 0.05);
            if (*alpha < 0.0f) *alpha = 0.0f;
            lm_alpha(GET_JOBJ(gobj), *alpha);
        }
    } else if (*alpha < 1.0f) {
        *alpha = (f32) ((double) *alpha + 0.05);
        if (*alpha > 1.0f) *alpha = 1.0f;
        lm_alpha(GET_JOBJ(gobj), *alpha);
    }
}
static void lm_floor_create(Ground_GObj* gobj)
{
    LmFloor* fv = LM_VARS(LmFloor, lm_ground(gobj));
    static const int order[4] = { 1, 3, 0, 2 };
    int i;
    lm_anim(gobj);
    mpLib_80057BC0(1); mpLib_80057BC0(2);
    fv->ghost[0] = mu_grTSeak_80223908(3);
    fv->ghost[1] = mu_grTSeak_80223908(4);
    for (i = 0; i < 4; i++) {
        int index = order[i];
        fv->wall[index] = mu_grTSeak_80223908(lm_break_map(index));
        LM_VARS(LmWall, lm_ground(fv->wall[index]))->index = index;
    }
    fv->collapsed[0] = mu_grTSeak_80223908(11);
    fv->collapsed[1] = mu_grTSeak_80223908(10);
    for (i = 0; i < 4; i++) {
        HSD_GObj* pillar = mu_grTSeak_80223908(12);
        LM_VARS(LmPillar, lm_ground(pillar))->index = i;
        lb_8000C1C0(Ground_801C3FA4(pillar, 0),
                    Ground_801C3FA4(fv->wall[i], lm_param->pillar[i].joint));
        lm_pillar_state(pillar, 0);
        fv->pillar[i] = pillar;
    }
    lm_floor_init(gobj);
}
/* +3AC: rebuild is a crossfade in scale.y, with collision re-enabled first. */
MU_NO_CONTRACT static void lm_floor_frame(Ground_GObj* gobj)
{
    LmFloor* fv = LM_VARS(LmFloor, lm_ground(gobj));
    /* Jev review dust_position (jev-state.json): origin, confidence 1.0.
     * Question: how to map the console's uninitialized cosmetic dust vector?
     * Answer: use the origin and retain every particle call. The original
     * position is indeterminate; gameplay and random draws are unaffected. */
    Vec3 dust = { 0.0f, 0.0f, 0.0f };
    int i;
    if (fv->rebuild_frames > 0) {
        fv->rebuild_frames--;
        if (fv->quake_timer == 0) { Camera_RequestQuake(2, NULL); fv->quake_timer = 16; }
        else fv->quake_timer--;
        grLib_801C96F8(0x7535, 30, &dust);
        if (fv->rebuild_frames > 20) {
            Vec3 pos = { 0.0f, 40.0f, 0.0f };
            HSD_Generator* generator = grLib_801C96F8(0x7536, 30, &pos);
            generator->appsrt->rot.x = 1.0f;
        }
        {
            f32 scale = 1.0f - (f32) fv->rebuild_frames / (f32) lm_param->rebuild_frames;
            for (i = 0; i < 4; i++) { lm_scale(fv->wall[i], scale); lm_scale(fv->pillar[i], scale); }
            scale = 1.0f - scale;
            lm_scale(fv->collapsed[0], scale); lm_scale(fv->collapsed[1], scale);
        }
        if (fv->rebuild_frames == 0) {
            grAnime_801C8138(fv->collapsed[0], 11, 0);
            grAnime_801C8138(fv->collapsed[1], 10, 0);
            lm_scale(fv->collapsed[0], 1.0f); lm_scale(fv->collapsed[1], 1.0f);
            lm_floor_init(gobj);
        }
    }
    if (fv->rebuild_wait > 0) {
        fv->rebuild_wait--;
        if (fv->rebuild_wait < lm_param->rebuild_warning) {
            grLib_801C96F8(0x7535, 30, &dust);
            if (fv->quake_timer == 0) { Camera_RequestQuake(2, NULL); fv->quake_timer = 16; }
            else fv->quake_timer--;
        }
        if (fv->rebuild_wait == 0) {
            for (i = 0; i < 4; i++) {
                grAnime_801C8138(fv->wall[i], lm_break_map(i), 0);
                grAnime_801C8138(fv->pillar[i], 12, 0);
                lm_scale(fv->wall[i], 0.0f); lm_scale(fv->pillar[i], 0.0f);
            }
            mpJointListAdd(3); mpJointListAdd(5); mpJointListAdd(4); mpJointListAdd(6);
            fv->rebuild_frames = lm_param->rebuild_frames;
            mu_gr_mex_sfx(12);
        }
    }
    lb_800115F4();
}
static void lm_init(void)
{
    lm_param = Ground_GetYakumonoParam();
    if (lm_param == NULL) __assert("mansion.c", 0x477, "yakumono node not found");
    mu_grTSeak_80223908(0); mu_grTSeak_80223908(1);
    mu_grTSeak_80223908(2); mu_grTSeak_80223908(5);
    Ground_801C39C0(); Ground_801C3BB4();
}
#define LM_ROW(create, frame, flags) { create, lm_false, frame, mu_gr_gobj_nop, flags }
static StageCallbacks lm_map[] = {
    LM_ROW(lm_anim, mu_gr_gobj_nop, 0),
    LM_ROW(lm_background, mu_gr_gobj_nop, 0),
    LM_ROW(lm_floor_create, lm_floor_frame, 0),
    LM_ROW(lm_ghost_create, lm_ghost_frame, 0),
    LM_ROW(lm_ghost_create, lm_ghost_frame, 0),
    LM_ROW(lm_front_create, lm_front_frame, 0x80000000),
    LM_ROW(lm_coll_anim, lm_wall_frame, 0), LM_ROW(lm_coll_anim, lm_wall_frame, 0),
    LM_ROW(lm_coll_anim, lm_wall_frame, 0), LM_ROW(lm_coll_anim, lm_wall_frame, 0),
    LM_ROW(lm_anim, mu_gr_gobj_nop, 0), LM_ROW(lm_anim, mu_gr_gobj_nop, 0),
    LM_ROW(lm_pillar_create, lm_pillar_frame, 0),
};
const MuGrStage mu_gr_luigis_mansion = {
    .name = "Luigi's Mansion", .file = "/GrLm.dat", .map_gobjs = lm_map,
    .on_init = lm_init, .on_demo_init = mu_gr_demo_nop, .on_load = mu_gr_event_nop,
    .on_start = mu_gr_event_nop, .callback4 = mu_gr_false, .on_touch_line = mu_gr_no_touch_line,
    .on_check_shadow_render = mu_gr_shadow_always, .flags = MU_GR_READY,
};
