/* Saturn Valley routines no export, map table row or callback of the block reaches (GrSv.dat
 * grFunction): the walking Mr. Saturn of an earlier map gobj 7 (+2D94 .. +36AC), the warp area
 * debug draw (+1B18, +1C6C), and small helpers. Ported so the whole block is native; nothing
 * calls them, as on the console. */
#include "saturnvalley.h"

#include <math.h>
#include <math_native.h>
#include <string.h>

#include <dolphin/gx.h>
#include <melee/ft/types.h>
#include <melee/gr/granime.h>
#include <melee/gr/grdisplay.h>
#include <melee/gr/ground.h>
#include <melee/lb/lb_00B0.h>
#include <melee/pl/player.h>
#include <sysdolphin/baselib/cobj.h>
#include <sysdolphin/baselib/random.h>
#include <sysdolphin/baselib/state.h>

/* The walker's Ground user data (console +E0 on); map gobj 7 creates it as SvSimpleVars. */
typedef struct SvSaturnVars {
    int state;              /* console +E0: 0 wait, 1 look, 3 walk */
    int timer;              /* +E4 */
    HSD_JObj* home;         /* +E8: the path joint it was made on */
    HSD_JObj* node;         /* +EC */
    HSD_JObj* next;         /* +F0 */
    int xF4;                /* +F4: written 0, not read */
    float heading;          /* +F8 */
    Vec3 pos;               /* +FC */
    float progress;         /* +108 */
    float angle;            /* +10C */
    float look_angle;       /* +110 */
    HSD_GObj* look_target;  /* +114: written NULL, read as a fighter */
} SvSaturnVars;
SV_FITS(SvSaturnVars);

static int sv_map_id(HSD_GObj* g) { return ((Ground*) HSD_GObjGetUserData(g))->map_id; }

/* +2D54 Vec3_Lerp. */
void sv_lerp(Vec3* out, const Vec3* a, const Vec3* b, float t)
{
    out->x = mu_fmadds(b->x - a->x, t, a->x);
    out->y = mu_fmadds(b->y - a->y, t, a->y);
    out->z = mu_fmadds(b->z - a->z, t, a->z);
}
/* +2D94 Saturn_UpdateModel. */
static void sv_saturn_model(HSD_GObj* g)
{
    SvSaturnVars* v = SV_VARS(SvSaturnVars, g);
    HSD_JObj* j = Ground_801C3FA4(g, 0);
    j->translate = v->pos;
    j->rotate.y = v->angle;
    HSD_JObjSetMtxDirtySub(j);
}
/* +2DE4 Saturn_UpdatePosition: at the node, on the way to the next one, or at it (with the
 * heading), then divided by the stage scale. Console details kept: with progress between 0 and 1
 * and no next node only the division runs (again on the stored position); without a node the
 * node position is a stack value never written (zero here). */
static void sv_saturn_position(HSD_GObj* g)
{
    SvSaturnVars* v = SV_VARS(SvSaturnVars, g);
    Vec3 a = { 0.0f, 0.0f, 0.0f }, b;
    float scale;
    if (v->node != NULL) lb_8000B1CC(v->node, NULL, &a);
    if (v->progress == 0.0f) {
        v->pos = a;
    } else if (v->next != NULL) {
        float dx, dz;
        lb_8000B1CC(v->next, NULL, &b);
        if (v->progress == 1.0f) {
            dx = b.x - a.x;
            dz = b.z - a.z;
            v->pos = b;
        } else {
            float dy;
            dx = b.x - a.x;
            dy = b.y - a.y;
            dz = b.z - a.z;
            v->pos.x = mu_fmadds(v->progress, dx, a.x);
            v->pos.y = mu_fmadds(v->progress, dy, a.y);
            v->pos.z = mu_fmadds(v->progress, dz, a.z);
        }
        v->heading = atan2f(dx, dz);
    }
    scale = Ground_801C0498();
    v->pos.x /= scale;
    v->pos.y /= scale;
    v->pos.z /= scale;
}
/* +2FA4 Saturn_DecrementTimer. */
static bool sv_saturn_timer(HSD_GObj* g)
{
    SvSaturnVars* v = SV_VARS(SvSaturnVars, g);
    int t = v->timer;
    if (t > 0) v->timer = --t;
    return t == 0;
}
/* +2FC8 Saturn_EnterWait. */
static void sv_saturn_wait(HSD_GObj* g)
{
    SvSaturnVars* v = SV_VARS(SvSaturnVars, g);
    int lo;
    v->state = 0;
    lo = sv_param->x2C;
    v->timer = lo + HSD_Randi(sv_param->x30 - lo);
    v->progress = 0.0f;
    if (v->next != NULL) {
        v->node = v->next;
        v->next = NULL;
    }
    grAnime_801C8138(g, sv_map_id(g), 0);
    sv_saturn_position(g);
    sv_saturn_model(g);
}
/* +30FC Saturn_EnterLook. */
static void sv_saturn_look(HSD_GObj* g)
{
    SvSaturnVars* v = SV_VARS(SvSaturnVars, g);
    float range2, random;
    v->look_target = NULL;
    range2 = sv_param->x38 + sv_param->x38;
    random = HSD_Randf();
    v->state = 1;
    v->look_angle = mu_fmsubs(range2, random, sv_param->x38);
    grAnime_801C8138(g, sv_map_id(g), 1);
    sv_saturn_model(g);
}
/* +3188 Saturn_TryEnterWalk: one random choice among the children and, away from home, the
 * parent; a choice past the last child leaves the next node as it was. */
static bool sv_saturn_try_walk(HSD_GObj* g)
{
    SvSaturnVars* v = SV_VARS(SvSaturnVars, g);
    HSD_JObj* node = v->node;
    int n = node != v->home;
    int r;
    if (node->child == NULL && node == v->home) return false;
    for (HSD_JObj* c = node->child; c != NULL; c = c->next) ++n;
    r = HSD_Randi(n);
    if (node != v->home && r == n - 1) {
        v->next = node->parent;
    } else {
        HSD_JObj* c = v->node->child;
        for (int i = 0; c != NULL && i < r; ++i) c = c->next;
        if (c != NULL) v->next = c;
    }
    if (v->state != 3) grAnime_801C8138(g, sv_map_id(g), 1);
    v->state = 3;
    v->progress = 0.0f;
    sv_saturn_position(g);
    v->angle = v->heading;
    sv_saturn_model(g);
    return true;
}
/* +335C Saturn_DecideAction. */
void sv_saturn_decide(HSD_GObj* g)
{
    if (HSD_Randf() < sv_param->x34 || !sv_saturn_try_walk(g)) sv_saturn_look(g);
}
/* +3070 Saturn_Generate: the start angle is one double fused multiply-add, then rounded. */
HSD_GObj* sv_saturn_generate(HSD_JObj* node)
{
    HSD_GObj* g = mu_grTSeak_80223908(7);
    SvSaturnVars* v = SV_VARS(SvSaturnVars, g);
    float random;
    v->home = node;
    v->node = node;
    v->state = 0;
    v->next = NULL;
    v->xF4 = 0;
    random = HSD_Randf();
    random = random + random;
    v->angle = (float) __builtin_fma((double) random, 0.9, -0.9);
    sv_saturn_wait(g);
    return g;
}
/* +33C4 Saturn_OnFrame. */
void sv_saturn_frame(HSD_GObj* g)
{
    SvSaturnVars* v = SV_VARS(SvSaturnVars, g);
    switch (v->state) {
    case 1:
        if (v->look_target != NULL) {
            Fighter* fp = HSD_GObjGetUserData(v->look_target);
            v->angle = atan2f(fp->cur_pos.x - v->pos.x, fp->cur_pos.z - v->pos.z);
            if (sv_saturn_timer(g)) sv_saturn_wait(g);
            sv_saturn_model(g);
        } else {
            float angle = sv_approach(v->angle, v->look_angle, sv_param->x28);
            float d;
            v->angle = angle;
            d = angle - v->look_angle;
            if (d < 0.0f) d = -d;
            if (!(d >= 0.001)) sv_saturn_wait(g);
            sv_saturn_model(g);
        }
        break;
    case 3:
        v->angle = sv_approach(v->angle, v->heading, 0.2f);
        if (v->progress == 1.0f) {
            HSD_JObj* next = v->next;
            v->progress = 0.0f;
            if (next != NULL) {
                v->node = next;
                v->next = NULL;
            }
            if (HSD_Randf() > sv_param->x48 || !sv_saturn_try_walk(g)) sv_saturn_wait(g);
            sv_saturn_model(g);
        } else {
            v->progress = sv_approach(v->progress, 1.0f, sv_param->x44);
            sv_saturn_position(g);
            sv_saturn_model(g);
        }
        break;
    case 0:
        if (!sv_saturn_timer(g)) return;
        sv_saturn_decide(g);
        break;
    }
}

/* +2D4C Runner_EnterVanish. */
void sv_runner_vanish(HSD_GObj* g) { sv_runner_state(g, 6); }

/* +1CA8 WarpArea_ContainsPoint. */
bool sv_area_has_point(const SvWarpArea* a, const Vec3* p)
{
    return p->x >= a->min.x && p->y >= a->min.y && p->x <= a->max.x && p->y <= a->max.y;
}
/* +1A38 Stage_CheckFighter: the callback for every fighter of every slot (both of a pair). */
void sv_each_fighter(void* arg, void (*callback)(void* arg, HSD_GObj* fighter))
{
    for (int slot = 0; slot < 6; ++slot) {
        HSD_GObj* f = Player_GetEntityAtIndex(slot, 0);
        if (f != NULL && callback != NULL) callback(arg, f);
        f = Player_GetEntityAtIndex(slot, 1);
        if (f != NULL && callback != NULL) callback(arg, f);
    }
}
/* +0EEC MEXFunction_GetAddrFromSymbol: a symbol of an m-ex function block. */
typedef struct SvMexSymbol {
    u32 offset;
    u32 x4;
    DISC_PTR(char) name;
} DISC_STRUCT SvMexSymbol;
typedef struct SvMexFunction {
    DISC_PTR(u8) code;
    u32 x4[5];
    s32 count;
    DISC_PTR(SvMexSymbol) symbols;
} DISC_STRUCT SvMexFunction;
void* sv_mex_symbol(SvMexFunction* f, const char* symbol)
{
    int n = f->count;
    SvMexSymbol* s;
    if (n <= 0) return NULL;
    s = DP(f->symbols);
    for (int i = 0; i < n; ++i, ++s) {
        if (strcmp(DP(s->name), symbol) == 0) return DP(f->code) + s->offset;
    }
    return NULL;
}
/* +1B18 WarpArea_Draw: the area's outline, yellow. */
static void sv_area_draw(const SvWarpArea* a)
{
    GXClearVtxDesc();
    GXSetVtxAttrFmt(GX_VTXFMT0, GX_VA_POS, GX_POS_XYZ, GX_F32, 0);
    GXSetVtxAttrFmt(GX_VTXFMT0, GX_VA_CLR0, GX_CLR_RGBA, GX_RGBA8, 0);
    GXSetVtxDesc(GX_VA_POS, GX_DIRECT);
    GXSetVtxDesc(GX_VA_CLR0, GX_DIRECT);
    GXLoadPosMtxImm(HSD_CObjGetCurrent()->view_mtx, GX_PNMTX0);
    GXSetCurrentMtx(GX_PNMTX0);
    GXBegin(GX_LINESTRIP, GX_VTXFMT0, 5);
    GXPosition3f32(a->min.x, a->min.y, 0.0f);
    GXColor4u8(0xFF, 0xFF, 0, 0xFF);
    GXPosition3f32(a->min.x, a->max.y, 0.0f);
    GXColor4u8(0xFF, 0xFF, 0, 0xFF);
    GXPosition3f32(a->max.x, a->max.y, 0.0f);
    GXColor4u8(0xFF, 0xFF, 0, 0xFF);
    GXPosition3f32(a->max.x, a->min.y, 0.0f);
    GXColor4u8(0xFF, 0xFF, 0, 0xFF);
    GXPosition3f32(a->min.x, a->min.y, 0.0f);
    GXColor4u8(0xFF, 0xFF, 0, 0xFF);
    GXEnd();
    HSD_StateInvalidate(HSD_STATE_ALL);
}
/* +1C6C Floor_GXCallback: the floor, then both warp areas. */
void sv_floor_debug_draw(HSD_GObj* g, intptr_t pass)
{
    SvFloorVars* v;
    grDisplay_801C5DB0(g, pass);
    v = SV_VARS(SvFloorVars, g);
    sv_area_draw(&v->area[0]);
    sv_area_draw(&v->area[1]);
}
