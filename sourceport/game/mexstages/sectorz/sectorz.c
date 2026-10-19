/* Sector Z (Akaneia 1.0.1 and ACE 2.0.0, the same file: internal 95, external 312), the m-ex
 * grFunction block of GrOSz.dat: 1,058 instructions with symbols, plus the laser item (item kind
 * 252, itFunction 0, 233 instructions, sectorz_laser.c). Disassembly: `py -3.12 grdis.py ak
 * GrOSz.dat` and `py -3.12 grdis.py ak GrOSz.dat it 0` (run-source/rel09-stages).
 *
 * Every export is its own (1, 3 to 9). Four map gobjs: Points (+12C), Background (+168, lights of
 * map gobj 1), Floor (+1C0) and the Ship (+1FC), an Arwing that flies the paths of map gobj 2's
 * joints, fires pairs of lasers (item 252) at fighters in front of it and leaves the screen. Its
 * parameters are the file's yakumono param (Ground_GetYakumonoParam, MexgrSzParam below).
 *
 * Block notes:
 *   - Ship_OnFrame is in the block twice (+26C, the one the map table names, and a byte for byte
 *     copy at +C78); the switch table (+9D0) holds offsets from itself, so both copies jump into the
 *     cases of the second one. Same code, same behavior: written once here.
 *   - The m-ex routine 803D7078 (mu_gr_mex_sfx) plays sounds 1 (path start) and 2 (shots) of the
 *     stage's own bank (60).
 *   - Floor_OnCreation only starts animation set 0: the floor's collision is never bound to its
 *     joints (only the Ship calls Ground_InitMapColl), as on the console. */
#include "sectorz.h"

#include "../common/mu_gr_mex.h"

#include <math.h>

#include <dolphin/mtx.h>
#include <melee/gr/granime.h>
#include <melee/gr/ground.h>
#include <melee/gr/types.h>
#include <melee/it/inlines.h>
#include <melee/it/item.h>
#include <melee/it/it_26B1.h>
#include <melee/it/it_2725.h>
#include <melee/lb/lb_00B0.h>
#include <melee/lb/lb_00F9.h>
#include <melee/lb/lbvector.h>
#include <melee/mp/mplib.h>
#include <melee/pl/player.h>
#include <sysdolphin/baselib/debug.h>
#include <sysdolphin/baselib/gobj.h>
#include <sysdolphin/baselib/jobj.h>
#include <sysdolphin/baselib/random.h>

/* The stage's yakumono param (GrOSz.dat, disc data). +10 is not read. */
typedef struct MexgrSzParam {
    /* +0 */ float path_chance;   /* state 9: a path when HSD_Randf() is below it */
    /* +4 */ int burst;           /* shots fired at a fighter in range */
    /* +8 */ int shot_min;        /* frames between shots, [min, max) */
    /* +C */ int shot_max;
    /* +10 */ int x10;
    /* +14 */ float smooth_time;  /* the path follower's smoothing time */
    /* +18 */ float bob_amp;      /* height of the bob */
    /* +1C */ float bob_step;     /* bob phase per frame (of 1) */
    /* +20 */ int state_min;      /* frames in a fly state before a turn, [min, max) */
    /* +24 */ int state_max;
    /* +28 */ int leave_min;      /* frames on a path before it leaves, [min, max) */
    /* +2C */ int leave_max;
    /* +30 */ int step_min;       /* frames between path points, [min, max) */
    /* +34 */ int step_max;
    /* +38 */ int wait_min;       /* frames hidden (state 9), [min, max) */
    /* +3C */ int wait_max;
} DISC_STRUCT MexgrSzParam;

/* .bss.gr_param (+60C) */
static MexgrSzParam* mexgr_sz_param;

/* The Ship's state in its Ground union: console gp+E0 to gp+130. The first 0x1C bytes of the union
 * are not used by the block; the padding keeps the console offsets in the comments readable. */
typedef struct MexgrSzShip {
    u8 x0_pad[0x1C];
    /* +1C gp+E0 */ int state;        /* 0 to 9, also its animation set */
    /* +20 gp+E4 */ int burst;        /* shots left */
    /* +24 gp+E8 */ int shot_timer;
    /* +28 gp+EC */ int wait_timer;   /* state 9 */
    /* +2C gp+F0 */ int state_timer;
    /* +30 gp+F4 */ int step_timer;
    /* +34 gp+F8 */ int leave_timer;
    /* +38 gp+FC */ int leaving;      /* path done or leave timer out: fly to the exit point */
    /* +3C gp+100 */ int x100;
    /* +40 gp+104 */ float phase;     /* bob phase, 0 to 1 */
    /* +44 gp+108 */ const s16* path; /* joint ids of map gobj 2, -1 ends */
    /* +48 gp+10C */ u16 path_idx;
    /* +4C gp+110 */ Vec3 pos;
    /* +58 gp+11C */ Vec3 target;
    /* +64 gp+128 */ Vec3 vel;
} MexgrSzShip;
_Static_assert(sizeof(MexgrSzShip) <= sizeof(((Ground*) 0)->u), "ship state fits the Ground union");

static MexgrSzShip* mexgr_sz_ship(Ground_GObj* gobj)
{
    Ground* gp = HSD_GObjGetUserData(gobj);
    return (MexgrSzShip*) &gp->u;
}

/* .data.path1 to path4 (+117C), .rodata.paths (+AD4) */
static const s16 mexgr_sz_path1[] = { 12, 15, 16, 17, 18, 19, 13, -1 };
static const s16 mexgr_sz_path2[] = { 13, 19, 18, 17, 16, 15, 12, -1 };
static const s16 mexgr_sz_path3[] = { 14, 17, 16, 15, 12, -1 };
static const s16 mexgr_sz_path4[] = { 14, 17, 18, 19, 13, -1 };
static const s16* const mexgr_sz_paths[4] = {
    mexgr_sz_path1, mexgr_sz_path2, mexgr_sz_path3, mexgr_sz_path4,
};

/* .rodata.cst8 (+BB0): the frame time the follower uses (0.0167, a double) and pi. */
#define MEXGR_SZ_DT 0.0167
#define MEXGR_SZ_PI 3.141592653589793

/* random in [min, max): min + HSD_Randi(max - min), as the block computes it */
static int mexgr_sz_range(int min, int max)
{
    return min + HSD_Randi(max - min);
}

/* ---- the plain map gobjs ---- */

/* OnUnk of every row (+158, +194, +1EC, +268): a lone blr, its argument as the result. */
static bool mexgr_sz_unk(Ground_GObj* gobj)
{
    return gobj != NULL;
}

/* OnDeletion of every row: "li r3, 0; blr". */
static void mexgr_sz_on_deletion(Ground_GObj* gobj)
{
    (void) gobj;
}

/* Points_OnCreation (+12C), Background_OnCreation (+168), Floor_OnCreation (+1C0): animation
 * set 0. */
static void mexgr_sz_anim_create(Ground_GObj* gobj)
{
    Ground* gp = HSD_GObjGetUserData(gobj);
    grAnime_801C8138(gobj, gp->map_id, 0);
}

/* Background_OnFrame (+198) */
static void mexgr_sz_background_frame(Ground_GObj* gobj)
{
    (void) gobj;
    lb_800115F4();
}

/* ---- the Ship ---- */

static bool mexgr_sz_next_path(Ground_GObj* gobj);

/* Ship_StateChange (+640) */
static void mexgr_sz_state_change(Ground_GObj* gobj, int state)
{
    Ground* gp = HSD_GObjGetUserData(gobj);
    MexgrSzShip* sp = mexgr_sz_ship(gobj);
    HSD_JObj* root;
    HSD_JObj* jobj = Ground_801C3FA4(gobj, 0);

    /* joint 0 back to the pose of its joint description (jobj->id holds the HSD_Joint) */
    lb_8000B804(jobj, (HSD_Joint*) jobj->id);
    root = GET_JOBJ(gobj);
    root->translate.x = 0.0F;
    root->translate.y = 0.0F;
    root->translate.z = 0.0F;
    grAnime_801C8138(gobj, gp->map_id, state);
    sp->state_timer = mexgr_sz_range(mexgr_sz_param->state_min, mexgr_sz_param->state_max);
    sp->state = state;
    if (state != 9) {
        HSD_JObjClearFlagsAll(GET_JOBJ(gobj), JOBJ_HIDDEN);
        if ((u32) state <= 1) {
            mpJointListAdd(1);
            return;
        }
    } else {
        sp->wait_timer = mexgr_sz_range(mexgr_sz_param->wait_min, mexgr_sz_param->wait_max);
        HSD_JObjSetFlagsAll(GET_JOBJ(gobj), JOBJ_HIDDEN);
    }
    mpLib_80057BC0(1);
}

/* Ship_NextPath (+BC0): the next point of the path becomes the target; true when the path has no
 * point left (also when there is no path). */
static bool mexgr_sz_next_path(Ground_GObj* gobj)
{
    MexgrSzShip* sp = mexgr_sz_ship(gobj);
    const s16* path = sp->path;
    HSD_JObj* jobj;

    if (path == NULL) {
        return true;
    }
    if (path[sp->path_idx] == -1) {
        return true;
    }
    jobj = Ground_801C3FA4(Ground_GetMapGObj(2), sp->path[sp->path_idx]);
    lb_8000B1CC(jobj, NULL, &sp->target);
    sp->vel.x = 0.0F;
    sp->vel.y = 0.0F;
    sp->vel.z = 0.0F;
    sp->path_idx = (u16) (sp->path_idx + 1);
    return sp->path[sp->path_idx] == -1;
}

/* Ship_SetPath (+AE4) */
static void mexgr_sz_set_path(Ground_GObj* gobj, const s16* path)
{
    MexgrSzShip* sp = mexgr_sz_ship(gobj);

    sp->leaving = 0;
    sp->path = path;
    sp->path_idx = 0;
    mexgr_sz_next_path(gobj);
    sp->pos = sp->target;
    mexgr_sz_next_path(gobj);
    sp->step_timer = mexgr_sz_range(mexgr_sz_param->step_min, mexgr_sz_param->step_max);
    sp->leave_timer = mexgr_sz_range(mexgr_sz_param->leave_min, mexgr_sz_param->leave_max);
    mu_gr_mex_sfx(1);
    mexgr_sz_state_change(gobj, 0);
}

/* SpawnBullet (+1010): a laser from `from` toward `to`. */
static void mexgr_sz_spawn_bullet(Vec3* from, Vec3* to, int no_coll)
{
    SpawnItem spawn = { 0 };
    Item_GObj* gobj;

    spawn.kind = mu_gr_item_kind(0);
    spawn.pos = *to;
    spawn.prev_pos = *to;
    gobj = Item_80268B18(&spawn);
    if (gobj != NULL) {
        Item* ip = GET_ITEM(gobj);
        MexgrSzLaserVars* lv = (MexgrSzLaserVars*) &ip->xDD4_itemVar;
        HSD_JObj* jobj;
        Vec3 dir;

        it_80275158(gobj, 400.0F);
        ip->facing_dir = -1.0F;
        lv->started = 0;
        lv->no_coll = (s16) no_coll;
        dir.x = to->x - from->x;
        dir.y = to->y - from->y;
        dir.z = to->z - from->z;
        lbVector_Normalize(&dir);
        jobj = ip->xBBC_dynamicBoneTable->bones[0];
        /* the block calls asinf through a data word holding its retail address (+11B4) */
        jobj->rotate.x = asinf(dir.y);
        jobj->rotate.y = atan2f(dir.x, dir.z);
        jobj->rotate.z = 0.0F;
        HSD_JObjSetMtxDirtySub(jobj);
        it_8026B3A8(gobj);
        Item_80268E5C(gobj, 0, ITEM_ANIM_UPDATE);
        ip->xDCC_flag.b3 = 0;
    }
}

/* Ship_SpawnBullet (+9F8): the two cannons. Each shot starts at x of joint 2 with y and z of its
 * cannon joint (3, then 4) and flies to the cannon joint: straight along x, as on the console. */
static void mexgr_sz_ship_fire(Ground_GObj* gobj, int no_coll)
{
    Vec3 from;
    Vec3 to;

    mu_gr_mex_sfx(2);
    lb_8000B1CC(Ground_801C3FA4(gobj, 2), NULL, &from);
    lb_8000B1CC(Ground_801C3FA4(gobj, 3), NULL, &to);
    from.y = to.y;
    from.z = to.z;
    mexgr_sz_spawn_bullet(&from, &to, no_coll);
    lb_8000B1CC(Ground_801C3FA4(gobj, 2), NULL, &from);
    lb_8000B1CC(Ground_801C3FA4(gobj, 4), NULL, &to);
    from.y = to.y;
    from.z = to.z;
    mexgr_sz_spawn_bullet(&from, &to, no_coll);
}

/* Ship_ProcessMotion (+770): a critically damped follow of the target (each product and sum
 * rounded on its own, as the block does), the bob, the next path point on a timer. */
static void mexgr_sz_process_motion(Ground_GObj* gobj)
{
    MexgrSzShip* sp = mexgr_sz_ship(gobj);
    const MexgrSzParam* p = mexgr_sz_param;
    HSD_JObj* jobj;
    float omega;
    float x;
    float ex;
    float t;
    float u;
    float tx, ty, tz;
    float vx, vy, vz;
    float px, py, pz;
    float bob;
    Vec3 d;

    omega = 2.0F / p->smooth_time;
    x = (float) ((double) omega * MEXGR_SZ_DT);
    t = x * 0.48F;
    t = t * x;
    u = x + 1.0F;
    t = t + u;
    u = x * 0.235F;
    u = u * x;
    u = u * x;
    t = t + u;
    ex = 1.0F / t;

    PSVECSubtract(&sp->pos, &sp->target, &d);
    tx = d.x * omega;
    tx = tx + sp->vel.x;
    tx = (float) ((double) tx * MEXGR_SZ_DT);
    ty = d.y * omega;
    ty = ty + sp->vel.y;
    ty = (float) ((double) ty * MEXGR_SZ_DT);
    tz = d.z * omega;
    tz = tz + sp->vel.z;
    tz = (float) ((double) tz * MEXGR_SZ_DT);

    u = tx * omega;
    vx = sp->vel.x - u;
    vx = vx * ex;
    sp->vel.x = vx;
    u = ty * omega;
    vy = sp->vel.y - u;
    vy = vy * ex;
    sp->vel.y = vy;
    u = tz * omega;
    vz = sp->vel.z - u;
    vz = vz * ex;
    sp->vel.z = vz;

    py = d.y + ty;
    py = py * ex;
    py = py + sp->target.y;
    pz = d.z + tz;
    pz = pz * ex;
    pz = pz + sp->target.z;
    px = d.x + tx;
    px = px * ex;
    px = px + sp->target.x;
    sp->pos.x = px;
    sp->pos.y = py;
    sp->pos.z = pz;

    jobj = GET_JOBJ(gobj);
    jobj->translate = sp->pos;
    bob = sp->phase + sp->phase;
    bob = (float) ((double) bob * MEXGR_SZ_PI);
    bob = sinf(bob);
    bob = bob * p->bob_amp;
    jobj->translate.y = jobj->translate.y + bob;
    bob = sp->phase + p->bob_step;
    sp->phase = fmodf(bob, 1.0F);
    HSD_JObjSetMtxDirtySub(jobj);

    if (sp->leaving == 0) {
        if (sp->step_timer > 0) {
            sp->step_timer--;
        } else {
            sp->step_timer = mexgr_sz_range(p->step_min, p->step_max);
            if (mexgr_sz_next_path(gobj)) {
                sp->leaving = 1;
            }
        }
    }
}

/* Ship_OnCreation (+1FC) */
static void mexgr_sz_ship_create(Ground_GObj* gobj)
{
    Ground* gp = HSD_GObjGetUserData(gobj);
    MexgrSzShip* sp = mexgr_sz_ship(gobj);

    sp->burst = 0;
    sp->shot_timer = 0;
    sp->wait_timer = 0;
    sp->leaving = 0;
    sp->state_timer = 0;
    sp->step_timer = 0;
    sp->leave_timer = 0;
    sp->path_idx = 0;
    mexgr_sz_state_change(gobj, 9);
    Ground_InitMapColl(GET_JOBJ(gobj), gp->map_id);
}

/* States 0 and 3 (+D78): fly, fire a burst at a fighter in front and in range. */
static void mexgr_sz_fly(Ground_GObj* gobj, int state)
{
    MexgrSzShip* sp = mexgr_sz_ship(gobj);
    Vec3 ship;
    Vec3 pos;
    int i;

    if (sp->leaving != 0) {
        return;
    }
    if (sp->burst > 0) {
        sp->burst--;
        sp->shot_timer = mexgr_sz_range(mexgr_sz_param->shot_min, mexgr_sz_param->shot_max);
        mexgr_sz_ship_fire(gobj, 0);
        mexgr_sz_state_change(gobj, sp->state == 0 ? 1 : 4);
        return;
    }
    if (sp->state_timer > 0) {
        sp->state_timer--;
    } else {
        /* the turn; `state` is the one read before the switch (r8), still current here */
        mexgr_sz_state_change(gobj, state == 0 ? 2 : 5);
    }
    if (sp->shot_timer > 0) {
        sp->shot_timer--;
        return;
    }
    lb_8000B1CC(Ground_801C3FA4(gobj, 0), NULL, &ship);
    for (i = 0; i < 4; i++) {
        /* the pose of the slot whether or not it has a player, as the console reads it */
        Player_LoadPlayerCoords(i, &pos);
        if (pos.x < ship.x) {
            float dist = ship.x - pos.x;
            float range;
            if (dist < 0.0F) {
                dist = -dist;
            }
            range = Ground_801C0498() * 50.0F;
            if (!(range <= dist)) {
                sp->burst = mexgr_sz_param->burst;
            }
        }
    }
}

/* Ship_OnFrame (+26C, the cases at +D78 and on) */
static void mexgr_sz_ship_frame(Ground_GObj* gobj)
{
    MexgrSzShip* sp = mexgr_sz_ship(gobj);
    int state = sp->state;

    /* states 0, 2, 3, 5 (the bits of 0x2D): move, and leave when the leave timer runs out */
    if ((u32) state <= 5 && ((0x2D >> state) & 1)) {
        bool check = false;
        if (sp->leave_timer > 0) {
            sp->leave_timer--;
            if (sp->leave_timer == 0) {
                sp->target.y = 300.0F;
                sp->leaving = 1;
                check = true;
            }
        }
        if (!check && sp->leaving != 0) {
            check = true;
        }
        if (check) {
            float dx = sp->target.x - sp->pos.x;
            float dy = sp->target.y - sp->pos.y;
            float dz = sp->target.z - sp->pos.z;
            double sum = (double) dx * dx;
            double sq = (double) dy * dy;
            float d2;
            sum = sum + sq;
            sq = (double) dz * dz;
            sum = sum + sq;
            d2 = (float) sum;
            if (d2 < 25.0F) {
                mexgr_sz_state_change(gobj, 9);
                goto cases;
            }
        }
        mexgr_sz_process_motion(gobj);
    }

cases:
    state = sp->state;
    switch (state) {
    case 0:
    case 3:
        mexgr_sz_fly(gobj, state);
        break;
    case 1:
    case 4:
        if (grAnime_801C83D0(gobj, 3, 7)) {
            mexgr_sz_state_change(gobj, sp->state == 1 ? 0 : 3);
        }
        break;
    case 2:
    case 5:
        if (grAnime_801C83D0(gobj, 2, 7)) {
            mexgr_sz_state_change(gobj, sp->state == 2 ? 3 : 0);
        }
        break;
    case 6:
        if (grAnime_801C83D0(gobj, 0, 7)) {
            mexgr_sz_state_change(gobj, 9);
        }
        break;
    case 9:
        if (sp->wait_timer > 0) {
            sp->wait_timer--;
        } else if (!(HSD_Randf() >= mexgr_sz_param->path_chance)) {
            mexgr_sz_set_path(gobj, mexgr_sz_paths[HSD_Randi(4)]);
        } else {
            mexgr_sz_state_change(gobj, 6);
        }
        break;
    default:
        break;
    }
    Ground_UpdateMapColl(gobj);
}

/* map_gobjs (+0): OnCreation, OnUnk, OnFrame, OnDeletion, flags */
static StageCallbacks mexgr_sz_map[4] = {
    { mexgr_sz_anim_create, mexgr_sz_unk, mu_gr_gobj_nop, mexgr_sz_on_deletion, 0 },
    { mexgr_sz_anim_create, mexgr_sz_unk, mexgr_sz_background_frame, mexgr_sz_on_deletion,
      0x80000000 },
    { mexgr_sz_anim_create, mexgr_sz_unk, mu_gr_gobj_nop, mexgr_sz_on_deletion, 0 },
    { mexgr_sz_ship_create, mexgr_sz_unk, mexgr_sz_ship_frame, mexgr_sz_on_deletion, 0 },
};

/* OnInit (+50). Without a yakumono param the console stops in __assert (the block's own message
 * and line); natively too. */
static void mexgr_sz_init(void)
{
    mexgr_sz_param = Ground_GetYakumonoParam();
    if (mexgr_sz_param == NULL) {
        __assert("sectorz.c", 0x289, "yakumono node was not found in dat");
    }
    mu_grTSeak_80223908(0);
    mu_grTSeak_80223908(1);
    mu_grTSeak_80223908(2);
    mu_grTSeak_80223908(3);
    Ground_801C39C0();
    Ground_801C3BB4();
}

/* OnCheckShadowRender (+E0): a shadow where the fighter is above the surface joint ("fcmpu; GT
 * bit", false on NaN). */
static bool mexgr_sz_check_shadow(Vec3* pos, int arg, HSD_JObj* jobj)
{
    Vec3 at;
    (void) arg;
    lb_8000B1CC(jobj, NULL, &at);
    return pos->y > at.y;
}

const MuGrStage mu_gr_sector_z = {
    .name = "Sector Z",
    .file = "/GrOSz.dat",
    .map_gobjs = mexgr_sz_map,
    .on_init = mexgr_sz_init,
    .on_demo_init = mu_gr_demo_nop,
    .on_load = mu_gr_event_nop,
    .on_start = mu_gr_event_nop,
    .callback4 = mu_gr_false,
    .on_touch_line = mu_gr_no_touch_line,
    .on_check_shadow_render = mexgr_sz_check_shadow,
    .items = mexgr_sz_items,
    .item_count = 1,
    .flags = MU_GR_READY,
};
