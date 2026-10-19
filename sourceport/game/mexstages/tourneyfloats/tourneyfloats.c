/* GrTFloats exports 1 and 3 to 9. Its Map_Create +358 extends the plain helper with
 * a map camera. A controller cycles six shuffled maps with 1080-frame waits and 720-frame
 * transitions, a warning HUD and camera/blast-zone changes. Arithmetic stays unfused. */
#include "../common/mu_gr_common.h"
#include "../common/mu_gr_mex.h"
#include "map_params.h"
#include <melee/ft/ft_0D31.h>
#include <melee/ft/kinds/ftCommon/ftCo_09F7.h>
#include <melee/ft/types.h>
#include <melee/gm/gm_1601.h>
#include <melee/gr/granime.h>
#include <melee/gr/grdatfiles.h>
#include <melee/gr/grdisplay.h>
#include <melee/gr/ground.h>
#include <melee/gr/stage.h>
#include <melee/if/ifall.h>
#include <melee/lb/lb_00B0.h>
#include <melee/lb/lbspdisplay.h>
#include <melee/mp/mplib.h>
#include <sysdolphin/baselib/aobj.h>
#include <sysdolphin/baselib/debug.h>
#include <sysdolphin/baselib/gobj.h>
#include <sysdolphin/baselib/gobjgxlink.h>
#include <sysdolphin/baselib/gobjobject.h>
#include <sysdolphin/baselib/gobjproc.h>
#include <sysdolphin/baselib/gobjuserdata.h>
#include <sysdolphin/baselib/jobj.h>
#include <sysdolphin/baselib/random.h>
#include <sysdolphin/baselib/memory.h>
#include <math.h>

typedef struct TfRect { float top, bottom, left, right; } TfRect;
typedef struct TfVars {
    HSD_GObj* current; int in_frame; Vec2 in_start; TfRect in_bounds;
    HSD_GObj* old; int out_frame; Vec2 out_end; TfRect out_bounds;
    u8 state; int wait; HSD_GObj* warning; int warning_frame; s8 index, order[6];
} TfVars;
static StageCallbacks tf_map[18];
static TfVars* tf_vars(HSD_GObj* gobj) { return HSD_GObjGetUserData(gobj); }
static struct UnkStageDat_x8_t* tf_data(int map)
{
    return &DP(grDatFiles_801C6330(map)->unk4->unk8)[map];
}
static int tf_id(HSD_GObj* gobj) { return ((Ground*) HSD_GObjGetUserData(gobj))->map_id; }
static void tf_joint_list(HSD_GObj* gobj, bool enable)
{
    struct UnkStageDat_x8_t* data = tf_data(tf_id(gobj));
    GrJoint* joints = DP(data->unk20);
    int i;
    for (i = 0; i < data->unk24; i++) {
        if (enable) mpJointListAdd(joints[i].x); else mpLib_80057BC0(joints[i].x);
    }
}
static void tf_coll_frame(HSD_GObj* gobj) { Ground_UpdateMapColl(gobj); }
static HSD_GObj* tf_create(int map)
{
    HSD_GObj* gobj = Ground_GetStageGObj(map);
    if (gobj != NULL) {
        Ground* gp = HSD_GObjGetUserData(gobj);
        struct UnkStageDat_x8_t* data = tf_data(map);
        gp->x8_callback = NULL; gp->xC_callback = mu_gr_gobj_nop;
        if (!DISC_NULL(data->x10)) {
            HSD_GObj* camera = GObj_Create(17, 19, 0);
            HSD_CObj* cobj = lb_80013B14(DP(data->x10));
            int i;
            HSD_GObjObject_80390A70(camera, HSD_GObj_CameraKind, cobj);
            GObj_SetupGXLinkMax(camera, grDisplay_801C5F60, 5);
            camera->gxlink_prios = 8;
            gp->x18 = camera;
            /* Ground_801C2BD4, the block's fixed-address helper, in native named fields. */
            for (i = 0; i < 4 && stage_info.x694[i] != cobj; i++) {}
            if (i == 4) {
                for (i = 0; i < 4 && stage_info.x694[i] != NULL; i++) {}
                HSD_ASSERT(2381, i != 4);
                stage_info.x694[i] = cobj;
            }
        }
        GObj_SetupGXLink(gobj, grDisplay_801C5DB0, 3, 0);
        Ground_InitMapColl(GET_JOBJ(gobj), gp->map_id);
        grAnime_801C8138(gobj, gp->map_id, 0);
        HSD_GObj_SetupProc(gobj, tf_coll_frame, 4);
    }
    return gobj;
}
MU_NO_CONTRACT static void tf_params_apply(TfVars* v)
{
    const float* p = tf_params[tf_id(v->current) - 3];
    HSD_JObj* root = Ground_801C3FA4(v->current, 0);
    StageCameraInfo* c = &stage_info.cam_info;
    float sx = root->scale.x, sy = root->scale.y;
    c->cam_x_offset = p[8] * sx; c->cam_y_offset = p[9] * sy;
    c->cam_bounds.top = p[0] * sy - c->cam_y_offset;
    c->cam_bounds.bottom = p[1] * sy - c->cam_y_offset;
    c->cam_bounds.left = p[2] * sx - c->cam_x_offset;
    c->cam_bounds.right = p[3] * sx - c->cam_x_offset;
    stage_info.blast_zone.top = p[4] * sy - c->cam_y_offset;
    stage_info.blast_zone.bottom = p[5] * sy - c->cam_y_offset;
    stage_info.blast_zone.left = p[6] * sx - c->cam_x_offset;
    stage_info.blast_zone.right = p[7] * sx - c->cam_x_offset;
    c->cam_vertical_tilt = p[10]; c->cam_pan_degrees = p[11]; c->x20 = p[12]; c->x24 = p[13];
    c->cam_max_depth = p[14]; c->x3C = p[15]; c->cam_track_ratio = p[16];
    c->cam_fixed_zoom = p[17]; c->cam_track_smooth = p[18];
}
static void tf_wait(TfVars* v)
{
    HSD_JObj* root = Ground_801C3FA4(v->current, 0);
    v->state = 0; v->wait = 1080;
    root->translate.x = root->translate.y = root->translate.z = 0;
    if (v->old != NULL) { tf_joint_list(v->old, false); Ground_801C4A08(v->old); }
    tf_params_apply(v);
}
static void tf_position(int direction, Vec2* out, HSD_GObj* old)
{
    switch (direction) {
    case 0: out->x = 0; out->y = Stage_GetBlastZoneTopOffset() - Ground_801C3FA4(old, 3)->translate.y; break;
    case 1: out->x = 0; out->y = Stage_GetBlastZoneBottomOffset() - Ground_801C3FA4(old, 2)->translate.y; break;
    case 2: out->x = Stage_GetBlastZoneLeftOffset() - Ground_801C3FA4(old, 5)->translate.x; out->y = 0; break;
    case 3: out->x = Stage_GetBlastZoneRightOffset() - Ground_801C3FA4(old, 4)->translate.x; out->y = 0; break;
    }
}
MU_NO_CONTRACT static void tf_bounds(HSD_GObj* gobj, TfRect* out)
{
    HSD_JObj* root = Ground_801C3FA4(gobj, 0);
    /* The console uses scale.x for y bounds and scale.y for x bounds. */
    out->top = Ground_801C3FA4(gobj, 2)->translate.y * root->scale.x + 15.0F;
    out->bottom = Ground_801C3FA4(gobj, 3)->translate.y * root->scale.x - 15.0F;
    out->left = Ground_801C3FA4(gobj, 4)->translate.x * root->scale.y - 15.0F;
    out->right = Ground_801C3FA4(gobj, 5)->translate.x * root->scale.y + 15.0F;
}
static void tf_screen_draw(HSD_GObj* gobj, intptr_t pass)
{
    if (!ifAll_IsHUDHidden()) grDisplay_801C5DB0(gobj, pass);
}
static void tf_warning(TfVars* v)
{
    HSD_GObj* warning = tf_create(2);
    HSD_JObj* icon = Ground_801C3FA4(warning, 3);
    HSD_JObj* root;
    v->warning = warning; v->warning_frame = 0;
    warning->render_cb = tf_screen_draw;
    HSD_ForeachAnim(icon, 6, 0x400, HSD_AObjReqAnim, AOBJ_ARG_AF, (double) (float) (tf_id(v->current) - 3));
    HSD_JObjAnim(icon);
    HSD_ForeachAnim(icon, 6, 0x400, HSD_AObjStopAnim, AOBJ_ARG_AOV, NULL, NULL);
    Ground_801C3FA4(warning, 1)->rotate.z = atan2f(v->in_start.y, v->in_start.x);
    root = Ground_801C3FA4(warning, 0);
    root->translate.y = 18;
    if (v->in_start.y < 0) root->translate.x = 0;
    else if (v->in_start.x < 0) root->translate.x = -18;
    else if (v->in_start.x > 0) root->translate.x = 18;
    HSD_JObjSetMtxDirtySub(root);
    mu_gr_mex_sfx(10);
    stage_info.cam_info.cam_pan_degrees = -10;
    stage_info.cam_info.x20 = stage_info.cam_info.x24 = 0;
    if (v->in_start.y < 0) stage_info.cam_info.cam_bounds.bottom = stage_info.blast_zone.bottom + 30.0F;
    else if (v->in_start.x != 0) {
        stage_info.cam_info.cam_bounds.left = stage_info.blast_zone.left + 30.0F;
        stage_info.cam_info.cam_bounds.right = stage_info.blast_zone.right - 30.0F;
    }
}
static void tf_transition(TfVars* v)
{
    static const int combos[3][2] = { {1,1}, {3,2}, {2,3} };
    const int* combo = combos[HSD_Randi(3)];
    HSD_JObj* root = Ground_801C3FA4(v->current, 0);
    tf_position(combo[0], &v->out_end, v->old);
    tf_position(combo[1], &v->in_start, v->old);
    tf_bounds(v->old, &v->out_bounds); tf_bounds(v->current, &v->in_bounds);
    v->state = 1; v->in_frame = 0; v->out_frame = -1;
    root->translate.x = v->in_start.x; root->translate.y = v->in_start.y;
    tf_warning(v);
}
static void tf_next(TfVars* v)
{
    int i;
    if (v->index == -1) {
        for (i = 0; i < 6; i++) v->order[i] = i + 3;
        for (i = 0; i < 6; i++) {
            int index = HSD_Randi(6); s8 swap = v->order[index];
            v->order[index] = v->order[i]; v->order[i] = swap;
        }
        v->index = 1;
    } else if (++v->index > 5) v->index = 0;
    v->old = v->current; v->current = tf_create(v->order[v->index]);
    tf_joint_list(v->current, true);
    if (v->old != NULL) tf_transition(v); else tf_wait(v);
}
static void tf_escape_squeeze(void)
{
    HSD_GObj* fighter;
    for (fighter = HSD_GObjPLinkHead[HSD_GOBJ_PLINK_FIGHTER]; fighter != NULL; fighter = fighter->next) {
        Fighter* fp = HSD_GObjGetUserData(fighter);
        unsigned int flags = fp->coll_data.env_flags;
        if (!fp->is_sleeping && !fp->x221F_b1 && (flags & 0x18000) && (flags & 0x6000)) {
            Vec3 zero1 = {0}, zero2 = {0};
            ftCo_8009F834(fighter, 0x1B3, 3, 0, 0, &zero1, &zero2, 0.0F);
            ftCo_800D3BC8(fighter);
        }
    }
}
MU_NO_CONTRACT static void tf_think(HSD_GObj* gobj)
{
    TfVars* v = tf_vars(gobj);
    if (v->state == 0) { if (--v->wait <= 0) tf_next(v); }
    else if (v->state == 1) {
        HSD_JObj* root;
        float fraction;
        if (++v->in_frame > 720) { v->in_frame = 720; fn_801650E8(); }
        if (v->out_frame == -1) {
            Vec3 current, old;
            lb_8000B1CC(Ground_801C3FA4(v->current, 0), NULL, &current);
            lb_8000B1CC(Ground_801C3FA4(v->old, 0), NULL, &old);
            if (current.x + v->in_bounds.right >= old.x + v->out_bounds.left &&
                current.x + v->in_bounds.left <= old.x + v->out_bounds.right &&
                current.y + v->in_bounds.top >= old.y + v->out_bounds.bottom &&
                current.y + v->in_bounds.bottom <= old.y + v->out_bounds.top) {
                v->out_frame = 0; Ground_801C4A08(v->warning); v->warning = NULL;
            }
        }
        /* +53C advances -1 to zero even on the first non-overlap frame. */
        ++v->out_frame;
        root = Ground_801C3FA4(v->old, 0);
        fraction = (float) v->out_frame / 720.0F;
        root->translate.x = v->out_end.x * fraction; root->translate.y = v->out_end.y * fraction;
        root->translate.z = 0; HSD_JObjSetMtxDirtySub(root);
        root = Ground_801C3FA4(v->current, 0);
        fraction = (float) v->in_frame / 720.0F;
        root->translate.x = (0.0F - v->in_start.x) * fraction + v->in_start.x;
        root->translate.y = (0.0F - v->in_start.y) * fraction + v->in_start.y;
        root->translate.z = 0; HSD_JObjSetMtxDirtySub(root);
        if (v->out_frame > 719) tf_wait(v);
    }
    tf_escape_squeeze();
}
static void tf_init(void)
{
    HSD_GObj* background; HSD_GObj* controller;
    TfVars* v;
    int i;
    tf_create(0); background = tf_create(1);
    ((Ground*) HSD_GObjGetUserData(background))->x11_flags.b012 = 2;
    Ground_801C39C0(); Ground_801C3BB4();
    for (i = 0; i < mu_gr_map_count(); i++) {
        struct UnkStageDat_x8_t* data = tf_data(i); GrJoint* joints = DP(data->unk20); int j;
        for (j = 0; j < data->unk24; j++) mpLib_80057BC0(joints[j].x);
    }
    controller = GObj_Create(3, 5, 0); v = mu_gr_mex_calloc(sizeof(*v));
    GObj_InitUserData(controller, 4, HSD_Free, v);
    HSD_GObj_SetupProc(controller, tf_think, 4);
    v->index = -1; tf_next(v);
    mpLib_80058560(); mu_gr_wind_gobj();
}
static bool tf_shadow(Vec3* pos, int arg, HSD_JObj* jobj)
{
    Vec3 world; (void) arg;
    lb_8000B1CC(jobj, NULL, &world);
    return world.y < pos->y + 2.0F;
}
const MuGrStage mu_gr_tourney_floats = {
    .name = "Tourney Floats", .file = "GrTFloats.dat", .map_gobjs = tf_map,
    .on_init = tf_init, .on_demo_init = mu_gr_demo_nop,
    .on_load = mu_gr_event_nop, .on_start = mu_gr_event_nop, .callback4 = mu_gr_false,
    .on_touch_line = mu_gr_no_touch_line, .on_check_shadow_render = tf_shadow, .flags = MU_GR_READY,
};
