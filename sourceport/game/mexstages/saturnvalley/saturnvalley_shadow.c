/* Saturn Valley's custom stage shadow (+1388 .. +1A38, +0B20 .. +0DD0, +42F4). Offsets refer
 * to GrSv.dat grFunction.
 *
 * The same m-ex shadow library as Sprout Tower's (mexstages/sprouttower/sprouttower_shadow.c),
 * an older build of it: one shadow (each create replaces the list head), the console's own
 * HSD_ShadowStartRender instead of a custom TEV setup, no receiver regions, no render rate, the
 * original match camera called by its address (800301D0) instead of a saved pointer. Sprout
 * Tower's routines are static there and tied to its table and setup, so none can be called from
 * here without editing that folder; this file ports this block's routines. Shared shape, and the
 * one place the two disassemblies agree but sprouttower_shadow.c does not (the light direction
 * test), are noted in the Sprout Tower port. */
#include "saturnvalley.h"

#include <math_native.h>

#include <dolphin/gx.h>
#include <dolphin/mtx.h>
#include <melee/cm/camera.h>
#include <melee/gr/ground.h>
#include <melee/lb/lb_00B0.h>
#include <melee/lb/lbshadow.h>
#include <melee/lb/lbspdisplay.h>
#include <melee/lb/lbvector.h>
#include <sysdolphin/baselib/cobj.h>
#include <sysdolphin/baselib/gobj.h>
#include <sysdolphin/baselib/gobjgxlink.h>
#include <sysdolphin/baselib/gobjobject.h>
#include <sysdolphin/baselib/initialize.h>
#include <sysdolphin/baselib/lobj.h>
#include <sysdolphin/baselib/memory.h>
#include <sysdolphin/baselib/shadow.h>
#include <sysdolphin/baselib/state.h>
#include <sysdolphin/baselib/tev.h>
#include <sysdolphin/baselib/tobj.h>

static SvShadow* sv_shadows;              /* +43DC stage_shadows */
static GObj_RenderFunc sv_original_camera; /* the camera's render callback at on_init */

/* +1388 Stage_CreateCustomShadow. Console detail kept: the new shadow becomes the whole list
 * (next is NULL, the old head is dropped, not freed). */
SvShadow* sv_shadow_create(HSD_JObj* joint, int center_depth, int size, float radius)
{
    SvShadow* s = HSD_MemAlloc(sizeof(*s));
    s->center_depth = center_depth;
    s->joint = joint;
    s->radius = radius;
    s->next = NULL;
    lbShadow_8000ED54(&s->lb, joint);
    HSD_ShadowSetSize(s->lb.shadow, (u16) size, (u16) size);
    sv_shadows = s;
    return s;
}
/* +1438 Match_GetShadowLObj: the first light flagged 0x400, else the last with flag 1 or 2. */
static HSD_LObj* sv_shadow_light(void)
{
    HSD_LObj* found = NULL;
    for (HSD_GObj* g = HSD_GObjGXLinkHead[4]; g != NULL; g = g->next_gx) {
        for (HSD_LObj* l = GET_LOBJ(g); l != NULL; l = l->next) {
            if (l->flags & 3) found = l;
            if (l->flags & 0x400) return l;
        }
    }
    return found;
}
/* +14A4 Shadows_Draw: a light space camera on the caster, the viewing rectangle grown until it
 * holds the caster's box (20 tries, else a fixed 128 box), the shadow texture rendered. */
static void sv_shadow_draw(bool quad, HSD_LObj* light, HSD_Shadow* s, Vec3* origin, float radius)
{
    Vec3 eye, interest, up = { 0.0f, 0.0f, 0.0f }, delta, direction, world_eye;
    HSD_ViewingRect rect;
    float xx, zz, length, r, nr;
    int tries;
    if (!HSD_LObjGetPosition(light, &eye)) return;
    if (HSD_LObjGetInterest(light, &interest)) lbVector_Sub(&eye, &interest);
    lbVector_Diff(&eye, &up, &delta);
    xx = delta.x * delta.x;
    zz = delta.z * delta.z;
    length = sqrtf(mu_fmadds(delta.y, delta.y, xx) + zz);
    /* +15A4 "ble": an unordered length takes the first branch too. */
    if (!(length <= 0.0f)) {
        /* +15B8: cror gt|un, so the up vector is solved unless the sum is 1e-6 or less. */
        if (!(xx + zz <= 0x1.0c6f7cp-20f)) {
            up.y = 1.0f;
            up.x = 0.0f;
            up.z = 0.0f;
            lbVector_Diff(&up, &eye, &direction);
            if (lbVector_Normalize(&direction) < 100.0f) {
                eye.x = mu_fnmsubs(direction.x, 100.0f, up.x);
                eye.y = mu_fnmsubs(direction.y, 100.0f, up.y);
                eye.z = mu_fnmsubs(direction.z, 100.0f, up.z);
            }
            lbVector_CrossprodNormalized(&up, &direction, &rect.origin);
            lbVector_CrossprodNormalized(&direction, &rect.origin, &up);
        } else {
            up.x = 0.0f;
            up.y = 0.0f;
            up.z = -1.0f;
        }
    } else {
        eye.x = 0.0f;
        eye.y = 0.0f;
        up.x = 0.0f;
        up.z = 0.0f;
        eye.z = 1.0f;
        up.y = 1.0f;
    }
    s->intensity = Ground_801C0508();
    PSVECAdd(origin, &eye, &world_eye);
    PSVECAdd(origin, &up, &direction);
    HSD_CObjSetEyePosition(s->camera, &world_eye);
    HSD_CObjSetInterest(s->camera, &direction);
    HSD_CObjSetUpVector(s->camera, &up);
    /* +1708: the block calls HSD_ViewingRectInit through a pointer declared with a float fifth
     * argument (f1 = 0.0); the real fifth argument is the integer `perspective`, which the
     * console leaves in r7 from the calls before. Integer 0 (orthographic), the choice
     * sprouttower_shadow.c made for the same call (its Jev review); the console's r7 is
     * unproven. */
    HSD_ViewingRectInit(&rect, &world_eye, &direction, &up, 0);
    r = (float) ((double) radius * 1.2);
    nr = (float) -((double) radius * 1.2);
    tries = 20;
    for (;;) {
        bool inside;
        HSD_ViewingRectAddRect(&rect, origin, r, nr, nr, r);
        inside = HSD_ViewingRectCheck(&rect) != 0;
        --tries;
        if (inside) {
            HSD_ShadowSetViewingRect(s, rect.top, rect.bottom, rect.left, rect.right);
            break;
        }
        if (tries == 0) {
            HSD_CObjSetOrtho(s->camera, 128.0f, -128.0f, -128.0f, 128.0f);
            break;
        }
    }
    HSD_ShadowInit(s);
    HSD_StartRender(3);
    HSD_ShadowStartRender(s);
    if (quad) lbShadow_8000F214(s);
    HSD_ShadowEndRender(s);
    HSD_Init_803755A8();
}
/* +189C NewCamera_RenderShadows: each listed shadow from its joint (center_depth levels down). */
static void sv_render_shadows(void)
{
    HSD_LObj* light = sv_shadow_light();
    bool quad;
    if (light == NULL || sv_shadows == NULL) return;
    quad = Camera_80030B24();
    for (SvShadow* s = sv_shadows; s != NULL; s = s->next) {
        Vec3 origin = { 0.0f, 0.0f, 0.0f };
        HSD_JObj* j = s->joint;
        if (j != NULL) {
            if (s->center_depth != 0) lb_80011E24(j, &j, s->center_depth, -1);
            lb_8000B1CC(j, NULL, &origin);
        }
        sv_shadow_draw(quad, light, s->lb.shadow, &origin, s->radius);
    }
}
/* +1988 CustomMatch_GXCallback (kept as data words in the file): the shadows, then the match
 * camera's own render. The console jumps to 800301D0 with r4 left by the shadow code; that
 * routine (cm/camera.c fn_800301D0, static) does not read its second argument. */
static void sv_camera(HSD_GObj* g, intptr_t pass)
{
    sv_render_shadows();
    sv_original_camera(g, pass);
}
/* +19C4 IntiCustomShadowCamera (OnInit +24 inlines it). The camera's render callback at this
 * point is fn_800301D0 (Camera setup installs it with GObj_SetupGXLinkMax), the routine the
 * console calls by address; it is kept here to be called the same way. */
void sv_shadow_install(void)
{
    HSD_GObj* camera = Camera_80030A50();
    if (camera->render_cb != sv_camera) sv_original_camera = camera->render_cb;
    camera->render_cb = sv_camera;
    sv_shadows = NULL;
}
/* +42F4 OnCheckShadowRender: above the receiving joint. */
bool sv_shadow_check(Vec3* pos, int arg, HSD_JObj* joint)
{
    Vec3 origin;
    (void) arg;
    lb_8000B1CC(joint, NULL, &origin);
    return pos->y > origin.y;
}

/* +0B20 GX_DrawScreenTexture and +19FC StageShadow_CreateDebugger: no export reaches them. The
 * debugger gobj draws the shadow texture as a 20 by 20 quad in front of the camera, pass 2. */
static void sv_shadow_debug_draw(HSD_GObj* g, intptr_t pass)
{
    HSD_TObj* tobj;
    HSD_ImageDesc* image;
    GXTexObj tex;
    Mtx view;
    (void) g;
    if (pass != 2 || sv_shadows == NULL) return;
    HSD_StateSetZMode(GX_TRUE, GX_LEQUAL, GX_FALSE);
    GXSetTexCoordGen2(GX_TEXCOORD0, GX_TG_MTX2x4, GX_TG_TEX0, GX_IDENTITY, GX_FALSE, GX_PTIDENTITY);
    HSD_StateSetNumChans(0);
    GXClearVtxDesc();
    GXSetCullMode(GX_CULL_NONE);
    GXSetNumTexGens(1);
    GXSetNumTevStages(1);
    GXSetVtxAttrFmt(GX_VTXFMT0, GX_VA_POS, GX_POS_XY, GX_F32, 0);
    GXSetVtxDesc(GX_VA_POS, GX_DIRECT);
    GXSetVtxAttrFmt(GX_VTXFMT0, GX_VA_TEX0, GX_TEX_ST, GX_F32, 0);
    GXSetVtxDesc(GX_VA_TEX0, GX_DIRECT);
    GXSetBlendMode(GX_BM_NONE, GX_BL_SRCALPHA, GX_BL_INVSRCALPHA, GX_LO_CLEAR);
    GXSetAlphaCompare(GX_GREATER, 0, GX_AOP_OR, GX_GREATER, 0);
    GXSetColorUpdate(GX_TRUE);
    GXSetAlphaUpdate(GX_FALSE);
    GXSetTevOrder(GX_TEVSTAGE0, GX_TEXCOORD0, GX_TEXMAP0, GX_COLOR_NULL);
    GXSetTevColorIn(GX_TEVSTAGE0, GX_CC_ZERO, GX_CC_ZERO, GX_CC_ZERO, GX_CC_TEXC);
    GXSetTevColorOp(GX_TEVSTAGE0, GX_TEV_ADD, GX_TB_ZERO, GX_CS_SCALE_1, GX_FALSE, GX_TEVPREV);
    GXSetTevAlphaIn(GX_TEVSTAGE0, GX_CA_ZERO, GX_CA_TEXA, GX_CA_KONST, GX_CA_ZERO);
    GXSetTevAlphaOp(GX_TEVSTAGE0, GX_TEV_ADD, GX_TB_ZERO, GX_CS_SCALE_1, GX_FALSE, GX_TEVPREV);
    tobj = sv_shadows->lb.shadow->texture;
    image = tobj->imagedesc;
    GXInitTexObj(&tex, DP(image->image_ptr), image->width, image->height, image->format, tobj->wrap_s,
                 tobj->wrap_t, GX_FALSE);
    GXInitTexObjLOD(&tex, GX_NEAR, GX_NEAR, 0.0f, 10.0f, 0.0f, GX_FALSE, GX_FALSE, GX_ANISO_1);
    GXLoadTexObj(&tex, GX_TEXMAP0);
    HSD_CObjGetViewingMtx(HSD_CObjGetCurrent(), view);
    GXSetCurrentMtx(GX_PNMTX0);
    GXLoadPosMtxImm(view, GX_PNMTX0);
    GXBegin(GX_QUADS, GX_VTXFMT0, 4);
    GXPosition2f32(-10.0f, 10.0f);
    GXTexCoord2f32(0.0f, 0.0f);
    GXPosition2f32(10.0f, 10.0f);
    GXTexCoord2f32(1.0f, 0.0f);
    GXPosition2f32(10.0f, -10.0f);
    GXTexCoord2f32(1.0f, 1.0f);
    GXPosition2f32(-10.0f, -10.0f);
    GXTexCoord2f32(0.0f, 1.0f);
    GXEnd();
    HSD_StateInvalidate(HSD_STATE_ALL);
}
void sv_shadow_debugger(void)
{
    HSD_GObj* g = GObj_Create(3, 5, 0);
    GObj_SetupGXLink(g, sv_shadow_debug_draw, 11, 0);
}
