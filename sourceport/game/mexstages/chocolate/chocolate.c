/* GrChoc exports 3, 5, 6 and 8. Map_Create +98 matches mu_gr_map_create_plain.
 * Two platform joints move between random points in triangles after 1500 frames, with
 * 300-frame interpolation and 24-frame warning fades. All arithmetic is unfused in the block. */
#include "../common/mu_gr_common.h"
#include <melee/gr/grdatfiles.h>
#include <melee/gr/grdisplay.h>
#include <melee/gr/ground.h>
#include <sysdolphin/baselib/dobj.h>
#include <sysdolphin/baselib/gobj.h>
#include <sysdolphin/baselib/gobjgxlink.h>
#include <sysdolphin/baselib/gobjobject.h>
#include <sysdolphin/baselib/gobjproc.h>
#include <sysdolphin/baselib/gobjplink.h>
#include <sysdolphin/baselib/jobj.h>
#include <sysdolphin/baselib/mobj.h>
#include <sysdolphin/baselib/random.h>
#include <dolphin/os.h>
#include <math.h>

typedef struct ChocMotion { int active, timer; Vec2 start, target, pos; } ChocMotion;
typedef struct ChocVars { int wait; ChocMotion move[2]; int fade_mode, fade; HSD_GObj* warn[2]; } ChocVars;
typedef struct ChocParam { DISC_PTR(HSD_Joint) warning; } DISC_STRUCT ChocParam;
static ChocVars* choc_vars(Ground_GObj* gobj)
{
    Ground* gp = HSD_GObjGetUserData(gobj);
    return (ChocVars*) &gp->u;
}
static struct UnkStageDat_x8_t* choc_map(void)
{
    return &DP(grDatFiles_801C6330(0)->unk4->unk8)[2];
}
static HSD_JObj* choc_joint(Ground_GObj* gobj, int index)
{
    return Ground_801C3FA4(gobj, DP(choc_map()->unk20)[index].z);
}
static void choc_draw(HSD_GObj* gobj, intptr_t pass) { grDisplay_801C5DB0(gobj, pass); }
MU_NO_CONTRACT static void choc_random(int index, Vec2* out)
{
    static const Vec2 triangles[2][3] = {
        { { -25, 12.8F }, { -25, 47.5F }, { -105, 12.8F } },
        { { 25, 12.8F }, { 25, 47.5F }, { 105, 12.8F } },
    };
    const Vec2* p = triangles[index];
    float a = HSD_Randf(), b = HSD_Randf();
    float x, y, delta;
    if (a + b > 1.0F) { a = 1.0F - a; b = 1.0F - b; }
    x = p[1].x - p[0].x; x *= a; x += p[0].x;
    delta = p[2].x - p[0].x; delta *= b; x += delta;
    y = p[1].y - p[0].y; y *= a; y += p[0].y;
    delta = p[2].y - p[0].y; delta *= b; y += delta;
    out->x = x; out->y = y;
}
MU_NO_CONTRACT static HSD_GObj* choc_warning(Vec2* start, Vec2* target)
{
    ChocParam* param = stage_info.yakumono_param;
    HSD_JObj* root = HSD_JObjAlloc();
    HSD_GObj* gobj = GObj_Create(3, 5, 0);
    float dx = target->x - start->x, dy = target->y - start->y;
    float length = dx * dx, product = dy * dy, scale;
    int count, i;
    HSD_GObjObject_80390A70(gobj, HSD_GObj_JObjKind, root);
    GObj_SetupGXLink(gobj, HSD_GObj_JObjCallback, 3, 0);
    length += product;
    count = (int) (sqrtf(length) / 3.5F);
    if (count > 2) {
        dx /= (float) count; dy /= (float) count;
        for (i = 0; i <= count; i++) {
            HSD_JObj* dot = HSD_JObjLoadJoint(DP(param->warning));
            float x = (float) i * dx, y = (float) i * dy;
            x += start->x; y += start->y;
            dot->translate.x = x; dot->translate.y = y;
            HSD_JObjAddChild(root, dot);
            OSReport("warning point %f, %f\n", (double) x, (double) y);
        }
    }
    scale = Ground_801C0498();
    root->scale.x = root->scale.y = root->scale.z = scale;
    HSD_JObjSetMtxDirtySub(root);
    return gobj;
}
static void choc_alpha(HSD_GObj* gobj, float alpha)
{
    HSD_JObj* joint;
    for (joint = GET_JOBJ(gobj)->child; joint != NULL; joint = joint->next) {
        HSD_DObj* dobj = joint->u.dobj;
        if (dobj != NULL && dobj->mobj != NULL && dobj->mobj->mat != NULL) {
            dobj->mobj->mat->alpha = alpha;
        }
    }
}
static void choc_move(Ground_GObj* gobj, int animate)
{
    ChocVars* v = choc_vars(gobj);
    int i;
    if (choc_map()->unk24 <= 1) return;
    Ground_801C0498();
    for (i = 0; i < 2; i++) {
        ChocMotion* m = &v->move[i];
        choc_random(i, &m->target);
        if (!animate) {
            HSD_JObj* joint = choc_joint(gobj, i);
            m->active = 0; m->pos = m->target;
            joint->translate.x = m->pos.x; joint->translate.y = m->pos.y;
            HSD_JObjSetMtxDirtySub(joint);
        } else {
            m->active = 1; m->timer = 300; m->start = m->pos;
            v->warn[i] = choc_warning(&m->start, &m->target);
        }
    }
    if (animate) { v->wait = 0; v->fade_mode = 1; v->fade = 24; }
    else v->wait = 1500;
}
MU_NO_CONTRACT static void choc_think(HSD_GObj* gobj)
{
    ChocVars* v = choc_vars(gobj);
    int i;
    if (choc_map()->unk24 <= 1) return;
    if (v->wait == 0) {
        for (i = 0; i < 2; i++) {
            ChocMotion* m = &v->move[i];
            HSD_JObj* joint;
            float fraction, x, y;
            m->timer--;
            if (m->timer <= 0) m->active = 0;
            fraction = (float) (300 - m->timer) / 300.0F;
            x = m->target.x - m->start.x; x *= fraction; x += m->start.x;
            y = m->target.y - m->start.y; y *= fraction; y += m->start.y;
            m->pos.x = x; m->pos.y = y;
            joint = choc_joint(gobj, i);
            joint->translate.x = x; joint->translate.y = y;
            HSD_JObjSetMtxDirtySub(joint);
        }
        if (v->move[0].active != 1 && v->move[1].active != 1) {
            v->wait = 1500; v->fade_mode = 2; v->fade = 24;
        }
    } else if (v->fade_mode == 0) {
        if (--v->wait <= 0) choc_move(gobj, 1);
    }
    if (v->fade_mode == 1) {
        float alpha;
        --v->fade;
        alpha = (float) (24.0 - (double) v->fade) / 24.0F;
        choc_alpha(v->warn[0], alpha); choc_alpha(v->warn[1], alpha);
        if (v->fade <= 0) v->fade_mode = 0;
    } else if (v->fade_mode == 2) {
        float alpha;
        --v->fade;
        alpha = 1.0F - (float) (24.0 - (double) v->fade) / 24.0F;
        choc_alpha(v->warn[0], alpha); choc_alpha(v->warn[1], alpha);
        if (v->fade <= 0) {
            v->fade_mode = 0;
            HSD_GObjFree(v->warn[0]); v->warn[0] = NULL;
            HSD_GObjFree(v->warn[1]); v->warn[1] = NULL;
        }
    }
}
static void choc_init(void)
{
    HSD_GObj* floor;
    int i;
    for (i = 0; i < mu_gr_map_count(); i++) mu_gr_map_create_plain(i);
    floor = Ground_GetMapGObj(2);
    if (choc_map()->unk24 > 1 && floor != NULL) {
        ChocVars* v = choc_vars(floor);
        v->wait = 1500;
        for (i = 0; i < 2; i++) {
            HSD_JObj* joint = choc_joint(floor, i);
            v->move[i].pos.x = joint->translate.x; v->move[i].pos.y = joint->translate.y;
        }
        floor->render_cb = choc_draw;
        HSD_GObj_SetupProc(floor, choc_think, 4);
    }
    mu_gr_wind_gobj(); Ground_801C39C0(); Ground_801C3BB4();
}
const MuGrStage mu_gr_chocolate_factory = {
    .name = "Chocolate Factory", .file = "GrChoc.dat", .on_init = choc_init,
    .on_load = mu_gr_event_nop, .on_start = mu_gr_event_nop,
    .on_touch_line = mu_gr_no_touch_line, .flags = MU_GR_READY,
};
