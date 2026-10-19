/* Targets!Tails (Akaneia 1.0.1 and ACE 2.0.0, the same file: internal 94, external 311), the m-ex
 * grFunction block of GrTTs.dat (496 instructions with symbols). Disassembly: `py -3.12 grdis.py
 * ak GrTTs.dat` (run-source/rel09-stages/out_dis_GrTTs.txt).
 *
 * Exports 1, 3, 5, 6 and 8; the other slots keep the MxDt row's words. Eight map gobjs: points,
 * background, floor, the scroller ("Position") and four cloud models. The scroller places nine
 * clouds on its joints; every frame its cloud joints, the floor's joints 1 to 10 and the siblings of
 * the floor's joint 14 move 0.5 to the left and wrap around between the stage's points 0x97 and
 * 0x98. A cloud stood on for 100 frames shrinks to nothing, its lines go, and a dust effect plays;
 * it comes back whole when it wraps. The targets and the flippers are the retail target stage's
 * (Ground_801C4210, Ground_801C42AC, by the raw addresses the block calls).
 *
 * The row names one stage item (kind 251) and the file has no itFunction block for it: its logic
 * table is empty here as on the console, and nothing in this block spawns it. */
#include "../peachcastle64/wp1_common.h"

#include <math.h>
#include <melee/gr/granime.h>
#include <melee/gr/grlib.h>
#include <melee/gr/ground.h>
#include <melee/gr/types.h>
#include <melee/it/forward.h>
#include <melee/it/itcoll.h>
#include <melee/it/kinds/types.h>
#include <melee/it/it_2725.h>
#include <melee/it/types.h>
#include <melee/lb/lb_00B0.h>
#include <melee/lb/lb_00F9.h>
#include <melee/mp/mplib.h>
#include <sysdolphin/baselib/gobj.h>
#include <sysdolphin/baselib/gobjproc.h>
#include <sysdolphin/baselib/jobj.h>

/* The scroller's state (console gp+E0, gp+E4: union +1C, +20): the left and right wrap points. */
typedef struct MexgrTsScroller {
    u8 x0_pad[0x1C];
    /* +1C gp+E0 */ float left;
    /* +20 gp+E4 */ float right;
} MexgrTsScroller;

/* A cloud's state (gp+E0 .. gp+F0). */
typedef struct MexgrTsCloud {
    u8 x0_pad[0x1C];
    /* +1C gp+E0 */ int gone;       /* 1 once shrunk away, 0 again on the wrap */
    /* +20 gp+E4 */ int x20;        /* 60 when it goes; nothing reads it */
    /* +24 gp+E8 */ int stand;      /* frames of standing left, 100 when whole */
    /* +28 gp+EC */ int line;       /* its collision joint */
    /* +2C gp+F0 */ HSD_JObj* body; /* the joint that scales */
} MexgrTsCloud;
_Static_assert(sizeof(MexgrTsScroller) <= sizeof(((Ground*) 0)->u), "scroller state fits");
_Static_assert(sizeof(MexgrTsCloud) <= sizeof(((Ground*) 0)->u), "cloud state fits");

static MexgrTsScroller* mexgr_ts_scroller(Ground* gp) { return (MexgrTsScroller*) &gp->u; }
static MexgrTsCloud* mexgr_ts_cloud(Ground* gp) { return (MexgrTsCloud*) &gp->u; }

/* .data.clouds (+4F4): per cloud the scroller joint it rides, the cloud model (map gobj 4 + n),
 * its collision joint; then the cloud's gobj and the scroller joint, filled at run time. */
typedef struct MexgrTsCloudSlot {
    u8 joint;
    u8 model;
    u8 line;
    HSD_GObj* gobj;
    HSD_JObj* jobj;
} MexgrTsCloudSlot;

static MexgrTsCloudSlot mexgr_ts_clouds[9] = {
    { 1, 0, 1 }, { 2, 0, 2 }, { 3, 1, 3 }, { 4, 1, 4 }, { 5, 2, 5 },
    { 6, 2, 6 }, { 7, 1, 7 }, { 8, 3, 8 }, { 9, 3, 9 },
};

/* wrap (+694): x into [lo, hi), with fmodf; the sum in double, as float operands give the same. */
static float mexgr_ts_wrap(float x, float lo, float hi)
{
    float span = hi - lo;
    float r = fmodf(x - lo, span);
    r = (float) ((double) lo + (double) r);
    if (lo > r) {
        r = r + span;
    }
    return r;
}

/* One joint 0.5 to the left, wrapped past the left point. True when it wrapped. */
static bool mexgr_ts_scroll(HSD_JObj* jobj, MexgrTsScroller* sc)
{
    float x = jobj->translate.x - 0.5F;
    jobj->translate.x = x;
    if (!(x < sc->left)) {
        return false;
    }
    jobj->translate.x = mexgr_ts_wrap(x, sc->left, sc->right);
    return true;
}

static void mexgr_ts_cloud_scale(MexgrTsCloud* cl, float s)
{
    cl->body->scale.x = s;
    cl->body->scale.y = s;
    cl->body->scale.z = s;
    HSD_JObjSetMtxDirtySub(cl->body);
}

/* Cloud_OnStand (+7D4), the cloud's line callback: each frame stood on takes one off; at 0 the
 * cloud goes (lines off, dust 30000 with 30 at its body, scale 0), else it shrinks to
 * stand / 100 * 0.8 + 0.2 (the divide in float, the rest in double). */
static void mexgr_ts_cloud_stand(void* user_data, int joint_id, CollData* coll, int coll_x50,
                                 mpLib_GroundEnum ground_kind, float delta_y)
{
    MexgrTsCloud* cl = mexgr_ts_cloud(user_data);
    (void) joint_id;
    (void) coll;
    (void) coll_x50;
    (void) ground_kind;
    (void) delta_y;
    if (cl->stand <= 0) {
        return;
    }
    if (--cl->stand == 0) {
        Vec3 pos;
        cl->gone = 1;
        cl->x20 = 60;
        mpLib_80057BC0(cl->line);
        lb_8000B1CC(cl->body, NULL, &pos);
        grLib_801C96F8(30000, 30, &pos);
        mexgr_ts_cloud_scale(cl, 0.0F);
        return;
    }
    mexgr_ts_cloud_scale(cl, (float) ((double) ((float) cl->stand / 100.0F) * 0x1.999999999999ap-1 +
                                      0x1.999999999999ap-3));
}

/* Cloud_Create (+568): cloud model `model` (map gobj 4 + model) pinned to scroller joint `jobj`,
 * whole, its lines on `line` with the stand callback. */
static HSD_GObj* mexgr_ts_cloud_create(HSD_JObj* jobj, int model, int line)
{
    HSD_GObj* gobj = mu_grTSeak_80223908(model + 4);
    Ground* gp = HSD_GObjGetUserData(gobj);
    MexgrTsCloud* cl = mexgr_ts_cloud(gp);
    lb_8000C1C0(Ground_801C3FA4(gobj, 0), jobj);
    cl->gone = 0;
    cl->x20 = 0;
    cl->stand = 100;
    cl->line = line;
    cl->body = Ground_801C3FA4(gobj, 1);
    mpJointSetCb1(line, gp, mexgr_ts_cloud_stand);
    mpLib_800552B0(line, GET_JOBJ(gobj), 1);
    mpLib_80055E9C(line);
    mpLib_80057424(line);
    return gobj;
}

/* Cloud_OnWarp (+6EC): whole again, lines back. */
static void mexgr_ts_cloud_warp(HSD_GObj* gobj)
{
    MexgrTsCloud* cl = mexgr_ts_cloud(HSD_GObjGetUserData(gobj));
    cl->gone = 0;
    cl->stand = 100;
    mexgr_ts_cloud_scale(cl, 1.0F);
    mpJointListAdd(cl->line);
}

/* Points_OnCreation (+138), Cloud_OnCreation (+494) */
static void mexgr_ts_anim_create(Ground_GObj* gobj)
{
    Ground* gp = HSD_GObjGetUserData(gobj);
    grAnime_801C8138(gobj, gp->map_id, 0);
}

/* Background_OnCreation (+174), Floor_OnCreation (+1F0) */
static void mexgr_ts_coll_create(Ground_GObj* gobj)
{
    Ground* gp = HSD_GObjGetUserData(gobj);
    Ground_InitMapColl(GET_JOBJ(gobj), gp->map_id);
    grAnime_801C8138(gobj, gp->map_id, 0);
}

/* Background_OnFrame (+1C8) */
static void mexgr_ts_background_frame(Ground_GObj* gobj)
{
    Ground_UpdateMapColl(gobj);
}

/* Floor_OnFrame (+244) */
static void mexgr_ts_floor_frame(Ground_GObj* gobj)
{
    lb_800115F4();
    Ground_UpdateMapColl(gobj);
}

/* Floor_SkipItemInterpolation (+62C), a proc of the scroller at priority 11: every item. */
static void mexgr_ts_items_skip(HSD_GObj* gobj)
{
    HSD_GObj* item;
    (void) gobj;
    for (item = HSD_GObjPLinkHead[HSD_GOBJ_PLINK_ITEM]; item != NULL; item = item->next) {
        it_8027137C(item);
    }
}

/* Position_OnCreation (+280): the wrap points, the nine clouds, the item proc. */
static void mexgr_ts_scroller_create(Ground_GObj* gobj)
{
    Ground* gp = HSD_GObjGetUserData(gobj);
    MexgrTsScroller* sc = mexgr_ts_scroller(gp);
    Vec3 pt;
    int i;
    grAnime_801C8138(gobj, gp->map_id, 0);
    Ground_801C2D24(0x97, &pt);
    sc->left = pt.x;
    Ground_801C2D24(0x98, &pt);
    sc->right = pt.x;
    for (i = 0; i < 9; i++) {
        MexgrTsCloudSlot* c = &mexgr_ts_clouds[i];
        c->jobj = Ground_801C3FA4(gobj, c->joint);
        c->gobj = mexgr_ts_cloud_create(c->jobj, c->model, c->line);
    }
    HSD_GObj_SetupProc(gobj, mexgr_ts_items_skip, 11);
}

/* Position_MoveJoint (+754): `jobj` and its next siblings. */
static void mexgr_ts_scroll_siblings(MexgrTsScroller* sc, HSD_JObj* jobj)
{
    for (; jobj != NULL; jobj = jobj->next) {
        mexgr_ts_scroll(jobj, sc);
    }
}

/* Position_OnFrame (+340) */
static void mexgr_ts_scroller_frame(Ground_GObj* gobj)
{
    MexgrTsScroller* sc = mexgr_ts_scroller(HSD_GObjGetUserData(gobj));
    HSD_GObj* floor;
    int i;
    for (i = 0; i < 9; i++) {
        MexgrTsCloudSlot* c = &mexgr_ts_clouds[i];
        if (c->gobj == NULL) {
            continue;
        }
        if (mexgr_ts_scroll(c->jobj, sc)) {
            mexgr_ts_cloud_warp(c->gobj);
        }
    }
    floor = Ground_GetMapGObj(2);
    for (i = 1; i < 11; i++) {
        mexgr_ts_scroll(Ground_801C3FA4(floor, i), sc);
    }
    mexgr_ts_scroll_siblings(sc, Ground_801C3FA4(floor, 14));
    HSD_JObjSetMtxDirtySub(GET_JOBJ(floor));
    HSD_JObjSetMtxDirtySub(GET_JOBJ(gobj));
}

/* Cloud_OnFrame (+4C4) */
static void mexgr_ts_cloud_frame(Ground_GObj* gobj)
{
    mpLib_80055E9C(mexgr_ts_cloud(HSD_GObjGetUserData(gobj))->line);
}

/* The OnUnk words (lone blrs, never called). */
static bool mexgr_ts_unk(Ground_GObj* gobj)
{
    (void) gobj;
    return false;
}

/* map_gobjs (+0): OnCreation, OnUnk, OnFrame, OnDeletion ("li r3, 0"), flags. */
static StageCallbacks mexgr_ts_map[8] = {
    { mexgr_ts_anim_create, mexgr_ts_unk, mu_gr_gobj_nop, mu_gr_gobj_nop, 0 },
    { mexgr_ts_coll_create, mexgr_ts_unk, mexgr_ts_background_frame, mu_gr_gobj_nop, 0 },
    { mexgr_ts_coll_create, mexgr_ts_unk, mexgr_ts_floor_frame, mu_gr_gobj_nop, 0xC0000000 },
    { mexgr_ts_scroller_create, mexgr_ts_unk, mexgr_ts_scroller_frame, mu_gr_gobj_nop, 0 },
    { mexgr_ts_anim_create, mexgr_ts_unk, mexgr_ts_cloud_frame, mu_gr_gobj_nop, 0 },
    { mexgr_ts_anim_create, mexgr_ts_unk, mexgr_ts_cloud_frame, mu_gr_gobj_nop, 0 },
    { mexgr_ts_anim_create, mexgr_ts_unk, mexgr_ts_cloud_frame, mu_gr_gobj_nop, 0 },
    { mexgr_ts_anim_create, mexgr_ts_unk, mexgr_ts_cloud_frame, mu_gr_gobj_nop, 0 },
};

/* OnInit (+A0): no cloud yet, map gobjs 0 to 3 (the scroller creates the clouds), the camera and
 * blast zone, then the retail targets and flippers. */
static void mexgr_ts_init(void)
{
    int i;
    for (i = 0; i < 9; i++) {
        mexgr_ts_clouds[i].gobj = NULL;
    }
    mexgr_wp1_init(4);
    Ground_801C4210();
    Ground_801C42AC();
}

/* Stage item 0 (kind 251): no itFunction block in the file. */
static const ItemLogicTable mexit_ts_items[1] = { { 0 } };

const MuGrStage mu_gr_targets_tails = {
    .name = "Targets!Tails",
    .file = "/GrTTs.dat",
    .map_gobjs = mexgr_ts_map,
    .on_init = mexgr_ts_init,
    /* The MxDt row leaves on_demo_init 0 (the file does not export it): the console would
     * call address 0 in the attract demo, which never shows a target stage. Natively a
     * no-op, so the layer has a word to install. */
    .on_demo_init = mu_gr_demo_nop,
    /* The row leaves callback4 and on_check_shadow_render 0 as well. Nothing in the game calls
     * callback4; the shadow check runs only for map joints with the shadow flags. Natively
     * the common no-op and always-render callbacks fill them. */
    .callback4 = mu_gr_false,
    .on_check_shadow_render = mu_gr_shadow_always,
    .on_load = mu_gr_event_nop,
    .on_start = mu_gr_event_nop,
    .on_touch_line = mu_gr_no_touch_line,
    .items = mexit_ts_items,
    .item_count = 1,
    .flags = MU_GR_READY,
};
