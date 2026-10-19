/* The stage's receiver-selective shadow renderer (+2538 through +3EBC). */
#include "sprouttower.h"
#include <math_native.h>
#include <math.h>
#include <melee/cm/camera.h>
#include <melee/ft/ftlib.h>
#include <melee/ft/types.h>
#include <melee/gr/ground.h>
#include <melee/gr/grdatfiles.h>
#include <melee/lb/lb_00B0.h>
#include <melee/lb/lbshadow.h>
#include <melee/lb/lbspdisplay.h>
#include <melee/lb/lbvector.h>
#include <melee/lb/types.h>
#include <melee/pl/player.h>
#include <sysdolphin/baselib/archive.h>
#include <sysdolphin/baselib/cobj.h>
#include <sysdolphin/baselib/dobj.h>
#include <sysdolphin/baselib/displayfunc.h>
#include <sysdolphin/baselib/initialize.h>
#include <sysdolphin/baselib/lobj.h>
#include <sysdolphin/baselib/memory.h>
#include <sysdolphin/baselib/mobj.h>
#include <sysdolphin/baselib/mtx.h>
#include <sysdolphin/baselib/perf.h>
#include <sysdolphin/baselib/shadow.h>
#include <sysdolphin/baselib/state.h>
#include <sysdolphin/baselib/tev.h>
#include <sysdolphin/baselib/util.h>
#include <dolphin/os.h>

typedef struct SpRegion {
    struct SpRegion* next; HSD_JObj* joint; float width, height;
} SpRegion;
typedef struct SpShadow {
    struct SpShadow* next;
    LbShadow lb;
    HSD_JObj* joint; int center_depth; float radius; u8 custom;
} SpShadow;
typedef struct SpShadowParam {
    u8 map_id, joint_id; u16 flags; const char* symbol; HSD_GObj* gobj;
    u16 center_depth, size; float radius; u8 receivers[8]; SpShadow* shadow;
} SpShadowParam;
typedef struct SpShadowVars { HSD_AObj* opacity; } SpShadowVars;
SP_FITS(SpShadowVars);

/* +4954: runtime pointers are native fields, the rest is the block's table. */
static SpShadowParam sp_shadow_params[] = {
    {5,0,0,"stower_joint",NULL,1,512,90,{4,6,5,9,10,11,0,0},NULL},
    {18,13,0,"stow1_joint",NULL,0,128,30,{1},NULL},
    {18,14,0,"stow2_joint",NULL,0,128,30,{2},NULL},
    {18,15,0,"stow3_joint",NULL,0,128,30,{3},NULL},
    {18,13,0x4000,"srail_joint",NULL,1,128,30,{1},NULL},
    {18,14,0x4000,"srail_joint",NULL,1,128,30,{2},NULL},
    {7,0,0,NULL,NULL,0,128,20,{2},NULL},
    {14,0,0,NULL,NULL,1,128,20,{4},NULL},
    {8,0,0,NULL,NULL,1,128,20,{4},NULL},
    {11,0,0x4000,NULL,NULL,2,128,20,{5},NULL},
    {4,4,0,"sstatue_joint",NULL,1,128,13,{4},NULL},
    {4,5,0,"sstatue_joint",NULL,1,128,13,{5},NULL},
    {4,2,0x8000,"streel_joint",NULL,2,256,55,{4,9,10},NULL},
    {4,3,0x8000,"streer_joint",NULL,2,256,55,{5,11,10},NULL},
    {4,6,0,"ssign_joint",NULL,1,128,20,{5},NULL},
    {2,17,0x8000,"stree_joint",NULL,3,128,30,{12},NULL},
    {2,16,0x8000,"stree_joint",NULL,3,128,30,{12},NULL},
    {2,18,0x8000,"stree_joint",NULL,3,128,30,{12},NULL},
    {2,15,0x8000,"stree_joint",NULL,3,128,30,{12},NULL},
    {2,19,0x8000,"stree_joint",NULL,3,128,30,{12},NULL},
    {2,20,0x8000,"stree_joint",NULL,3,128,30,{12},NULL},
    {2,7,0x4000,"shouse_joint",NULL,1,128,60,{9},NULL},
    {2,8,0,"shouse_joint",NULL,1,128,60,{9,10},NULL},
    {2,6,0,"shouse_joint",NULL,1,128,60,{9,10},NULL},
    {2,9,0,"shouse_joint",NULL,1,128,60,{9,10},NULL},
    {2,10,0,"shouse_joint",NULL,1,128,60,{11,10},NULL},
    {2,11,0,"shouse_joint",NULL,1,128,60,{11},NULL},
    {2,12,0,"shouse_joint",NULL,1,128,60,{11},NULL},
    {2,13,0,"shouse_joint",NULL,1,128,60,{11,10},NULL},
};
static const u8 sp_fighter_receivers[20] = {0,1,1,1,1,1,1,0,0,0,0,0,0,1,1,0,0,0,0,0};
static SpRegion* sp_regions;
static SpShadow* sp_shadows;
static GObj_RenderFunc sp_original_camera;
static int sp_render_timer;
static int sp_shadow_rate;

/* +2DF4: recursive source unrolled nine levels by the compiler. */
void sp_alpha(HSD_JObj* joint, float alpha)
{
    for (; joint; joint = joint->next) {
        for (HSD_DObj* d = joint->u.dobj; d; d = d->next) d->mobj->mat->alpha = alpha;
        sp_alpha(joint->child, alpha);
    }
}
/* +34BC keeps the console slip: only the starting joint's diffuse is written.
 * Its child and sibling subtrees receive the integer as an alpha value. */
void sp_diffuse(HSD_JObj* joint, unsigned int value)
{
    if (joint) {
        for (HSD_DObj* d = joint->u.dobj; d; d = d->next) {
            d->mobj->mat->diffuse.r = value;
            d->mobj->mat->diffuse.g = value;
            d->mobj->mat->diffuse.b = value;
        }
        sp_alpha(joint->child, (float) value); sp_alpha(joint->next, (float) value);
    }
}
/* +4480: these shadow receiver materials use inverse brightness and opacity. */
static void sp_shadow_alpha(HSD_JObj* joint, int value)
{
    for (; joint; joint = joint->next) {
        for (HSD_DObj* d = joint->u.dobj; d; d = d->next) {
            d->mobj->mat->diffuse.r = value;
            d->mobj->mat->diffuse.g = value;
            d->mobj->mat->diffuse.b = value;
            d->mobj->mat->alpha = (float) ((double) value * (1.0 / 255.0));
        }
        sp_shadow_alpha(joint->child, value);
    }
}
static void sp_opacity(void* obj, enum_t channel, HSD_ObjData* value)
{
    (void) channel;
    sp_shadow_alpha(GET_JOBJ((HSD_GObj*) obj), (u8) (int) (value->fv * 255.0f));
}
void sp_shadow_frame(Ground_GObj* g)
{
    SpShadowVars* v = SP_VARS(SpShadowVars, g);
    if (v->opacity) {
        HSD_AObjReqAnim(v->opacity, sp_time);
        HSD_AObjInterpretAnim(v->opacity, g, sp_opacity);
    }
}
/* +2538, +25F4: a region is local to one receiving joint. */
static void sp_region_create(HSD_JObj* joint, float width, float height)
{
    float scale = Ground_801C0498();
    SpRegion* region = HSD_MemAlloc(sizeof(*region));
    region->next = NULL; region->joint = joint;
    region->width = scale * width; region->height = scale * height;
    SpRegion** tail = &sp_regions; while (*tail) tail = &(*tail)->next; *tail = region;
}
bool sp_shadow_check(Vec3* pos, int arg, HSD_JObj* joint)
{
    Vec3 origin; (void) arg;
    SpRegion* region = sp_regions;
    for (; region; region = region->next) if (region->joint == joint) break;
    lb_8000B1CC(joint, NULL, &origin);
    if (!region) return pos->y > origin.y;
    float half = region->width * 0.5f;
    if (!(pos->x > origin.x - half && pos->x < origin.x + half && pos->y > origin.y)) return false;
    return pos->y <= origin.y + region->height;
}
static void sp_fighters_active(bool active)
{
    for (int i = 0; i < 6; ++i) {
        if (Player_GetPlayerSlotType(i) != 3) {
            HSD_GObj* g = Player_GetEntity(i);
            if (g) HSD_ShadowSetActive(((Fighter*) HSD_GObjGetUserData(g))->x20A4.shadow, active);
        }
    }
}
static void sp_shadows_disable(void)
{ for (SpShadow* s = sp_shadows; s; s = s->next) HSD_ShadowSetActive(s->lb.shadow, 0); }

/* +2700: select shadow textures separately for each receiving surface. */
static void sp_shadow_display(HSD_GObj* g, intptr_t pass)
{
    Ground* gp = HSD_GObjGetUserData(g);
    if (gp->x11_flags.b012 != Camera_8003108C()) return;
    u32 mask = HSD_GObj_80390EB8(pass);
    HSD_JObj* joint = Ground_801C3FA4(g, 1);
    for (int index = 1; joint; joint = joint->next, ++index) {
        if (sp_is_gc && ((index > 6 && index <= 8) || (index >= 12 && index <= 14))) continue;
        for (int i = 0; i < 6; ++i) {
            if (Player_GetPlayerSlotType(i) != 3) {
                HSD_GObj* fg = Player_GetEntity(i);
                if (fg) {
                    Fighter* fp = HSD_GObjGetUserData(fg);
                    bool active = sp_fighter_receivers[index] && sp_shadow_check(&fp->cur_pos, 0, joint)
                        && ftLib_800872BC(fg) && fp->motion_id != 0x121;
                    HSD_ShadowSetActive(fp->x20A4.shadow, active);
                }
            }
        }
        for (int i = 0; i < 29; ++i) {
            SpShadowParam* p = &sp_shadow_params[i];
            if (p->shadow) {
                bool active = false;
                for (int n = 0; n < 8; ++n) if (p->receivers[n] == index) active = true;
                if (sp_is_gc && (p->flags & 0x4000)) active = false;
                HSD_ShadowSetActive(p->shadow->lb.shadow, active);
            }
        }
        HSD_JObjDisp(joint, NULL, mask, 0);
    }
}
static SpShadow* sp_stage_shadow(HSD_JObj* joint, int depth, int size, float radius)
{
    SpShadow* s = HSD_MemAlloc(sizeof(*s));
    s->next = NULL; s->joint = joint; s->center_depth = depth; s->radius = radius; s->custom = 0;
    lbShadow_8000ED54(&s->lb, joint); HSD_ShadowSetSize(s->lb.shadow, size, size);
    SpShadow** tail = &sp_shadows; while (*tail) tail = &(*tail)->next; *tail = s;
    return s;
}
void sp_shadow_create(Ground_GObj* g)
{
    SpShadowVars* v = SP_VARS(SpShadowVars, g);
    for (int i = 0; i < 29; ++i) {
        SpShadowParam* p = &sp_shadow_params[i];
        if (sp_is_gc && (p->flags & 0x4000)) continue;
        HSD_GObj* source = p->gobj ? p->gobj : Ground_GetMapGObj(p->map_id);
        if (!source) { p->shadow = NULL; continue; }
        HSD_JObj* joint;
        if (p->symbol) {
            joint = HSD_JObjLoadJoint(HSD_ArchiveGetPublicAddress(grDatFiles_GetArchive()->unk0, p->symbol));
            joint->scale.x = Ground_801C0498(); joint->scale.y = Ground_801C0498(); joint->scale.z = Ground_801C0498();
            HSD_JObjSetMtxDirtySub(joint);
            lb_8000C2F8(joint, Ground_801C3FA4(source, p->joint_id));
        } else joint = Ground_801C3FA4(source, p->joint_id);
        p->shadow = sp_stage_shadow(joint, p->center_depth, p->size, p->radius);
        p->shadow->custom = !!(p->flags & 0x8000);
    }
    g->render_cb = sp_shadow_display; HSD_JObjSetMtxDirtySub(GET_JOBJ(g));
    for (int i = 1; i <= 3; ++i) sp_region_create(Ground_801C3FA4(g, i), 400, 44.75f);
    HSD_AnimJoint* data = HSD_ArchiveGetPublicAddress(grDatFiles_801C6330(0)->unk0, "shadow_animjoint");
    if (data) {
        v->opacity = HSD_AObjLoadDesc(DP(data->aobjdesc));
        HSD_AObjReqAnim(v->opacity, sp_time); HSD_AObjInterpretAnim(v->opacity, g, sp_opacity);
    } else v->opacity = NULL;
}

/* The private baselib background clear (8037F65C), spelled locally with its
 * named fields. The stage's custom setup calls it between two channel colors. */
static void sp_clear_texture(HSD_Shadow* s)
{
    HSD_CObj* camera = s->camera;
    GXLoadPosMtxImm(HSD_identityMtx, GX_PNMTX0); HSD_PerfCountMtxLoad();
    GXSetCurrentMtx(GX_PNMTX0); HSD_ClearVtxDesc(); GXSetVtxDesc(GX_VA_POS, GX_DIRECT);
    GXSetVtxAttrFmt(GX_VTXFMT0, GX_VA_POS, GX_POS_XYZ, GX_F32, 0); HSD_StateSetCullMode(GX_CULL_BACK);
    float top = HSD_CObjGetTop(camera) * 1.20000004768f;
    float bottom = HSD_CObjGetBottom(camera) * 1.20000004768f;
    float left = HSD_CObjGetLeft(camera) * 1.20000004768f;
    float right = HSD_CObjGetRight(camera) * 1.20000004768f;
    float near = HSD_CObjGetNear(camera) * -1.10000002384f;
    GXBegin(GX_QUADS, GX_VTXFMT0, 4);
    GXPosition3f32(left, top, near); GXPosition3f32(right, top, near);
    GXPosition3f32(right, bottom, near); GXPosition3f32(left, bottom, near); GXEnd();
}
/* +3624: same channel, TEV and PE state as the console's private shadow
 * renderer, except the model mask is selected by the stage's flag. */
static void sp_setup_custom(HSD_Shadow* s, bool custom)
{
    static HSD_Chan channel = {NULL,GX_COLOR0A0,0,{0,0,0,0},{0,0,0,255},GX_FALSE,
        GX_SRC_REG,GX_SRC_REG,GX_LIGHT_NULL,GX_DF_CLAMP,GX_AF_NONE,NULL};
    static HSD_TevDesc tev = {NULL,TEVCONF_MODE,GX_TEVSTAGE0,GX_TEXCOORD_NULL,
        GX_TEXMAP_NULL,GX_COLOR0A0,{GX_TEV_ADD,GX_CC_ZERO,GX_CC_ZERO,GX_CC_ZERO,
        GX_CC_RASC,GX_CS_SCALE_1,GX_TB_ZERO,GX_TRUE,GX_TEVPREV,GX_TEV_ADD,
        GX_CA_ZERO,GX_CA_ZERO,GX_CA_ZERO,GX_CA_RASA,GX_CS_SCALE_1,GX_TB_ZERO,
        GX_TRUE,GX_TEVPREV,GX_TC_LINEAR}};
    static HSD_PEDesc pe = {9,0,0,0,GX_BM_NONE,GX_BL_SRCALPHA,GX_BL_INVSRCALPHA,
        GX_LO_SET,GX_ALWAYS,GX_ALWAYS,GX_AOP_AND,GX_ALWAYS};
    HSD_ASSERT(0xC7, s); HSD_ASSERT(0xCA, s->camera); HSD_ASSERT(0xCD, s->texture);
    HSD_ASSERT(0xD0, s->texture->imagedesc);
    if (!s->objects) return;
    HSD_CObjSetCurrent(s->camera); HSD_StateInitTev(); HSD_SetupTevStageAll(&tev);
    HSD_StateSetNumTexGens(); HSD_SetupPEMode(0, &pe);
    HSD_ImageDesc* image = s->texture->imagedesc;
    channel.mat_color.r = channel.mat_color.g = channel.mat_color.b = 255;
    HSD_SetupChannelAll(&channel); GXSetScissor(0,0,image->width,image->height); sp_clear_texture(s);
    channel.mat_color.r = channel.mat_color.g = channel.mat_color.b = 0;
    HSD_SetupChannelAll(&channel); GXSetScissor(2,2,image->width-4,image->height-4);
    for (HSD_SList* list = s->objects; list; list = list->next)
        HSD_JObjDispAll(list->data, NULL, 5, custom ? 0 : 0x04000000);
    HSD_CObjEndCurrent();
}
static HSD_LObj* sp_light(void)
{
    HSD_LObj* found = NULL;
    for (HSD_GObj* g = HSD_GObjGXLinkHead[4]; g; g = g->next_gx)
        for (HSD_LObj* l = GET_LOBJ(g); l; l = l->next) {
            if (l->flags & 3) found = l;
            if (l->flags & 0x400) return l;
        }
    return found;
}
/* +3924: choose a light-space orthographic camera, smooth its bounds, and
 * render the caster into its own texture before the ordinary match camera. */
static void sp_shadow_draw(bool quad, HSD_LObj* light, HSD_Shadow* s,
                           Vec3* origin, bool custom, float radius)
{
    Vec3 eye, interest, up = {0,0,0}, delta, direction;
    HSD_ViewingRect rect;
    if (!HSD_LObjGetPosition(light, &eye)) return;
    if (HSD_LObjGetInterest(light, &interest)) lbVector_Sub(&eye, &interest);
    lbVector_Diff(&eye, &up, &delta);
    float xx = delta.x * delta.x, zz = delta.z * delta.z;
    float length = sqrtf(mu_fmadds(delta.y, delta.y, xx) + zz);
    if (length > 0) {
        if (xx + zz >= 0.0000010000001111620804f) {
            up.y = 1;
            lbVector_Diff(&up, &eye, &direction);
            if (lbVector_Normalize(&direction) < 100) {
                eye.x = mu_fnmsubs(direction.x, 100.0f, up.x);
                eye.y = mu_fnmsubs(direction.y, 100.0f, up.y);
                eye.z = mu_fnmsubs(direction.z, 100.0f, up.z);
            }
            lbVector_CrossprodNormalized(&up, &direction, &rect.origin);
            lbVector_CrossprodNormalized(&direction, &rect.origin, &up);
        } else { up.x = 0; up.y = 0; up.z = -1; }
    } else { eye.x = eye.y = 0; eye.z = 1; up.x = up.z = 0; up.y = 1; }
    s->intensity = Ground_801C0508();
    Vec3 world_eye, world_interest;
    PSVECAdd(origin, &eye, &world_eye); PSVECAdd(origin, &up, &world_interest);
    HSD_CObjSetEyePosition(s->camera, &world_eye); HSD_CObjSetInterest(s->camera, &world_interest);
    HSD_CObjSetUpVector(s->camera, &up);
    /* +3B98 passes float zero to a misdeclared function pointer, leaving the
     * real integer fifth argument volatile. Jev question: use orthographic
     * zero, an unrelated caster flag, or perspective one? Answer: ortho,
     * probability 1.0. The exact surviving console r7 remains unproven. */
    HSD_ViewingRectInit(&rect, &world_eye, &world_interest, &up, 0);
    radius = (float) ((double) radius * 1.2);
    int tries = 20;
    do {
        HSD_ViewingRectAddRect(&rect, origin, radius, -radius, -radius, radius);
        if (HSD_ViewingRectCheck(&rect)) break;
    } while (--tries);
    if (tries) HSD_ShadowSetViewingRect(s, rect.top, rect.bottom, rect.left, rect.right);
    else HSD_CObjSetOrtho(s->camera, 128, -128, -128, 128);
    HSD_ShadowInit(s); HSD_StartRender(3); sp_setup_custom(s, custom);
    if (quad) lbShadow_8000F214(s);
    HSD_ShadowEndRender(s); HSD_Init_803755A8();
}
static void sp_render_shadows(void)
{
    HSD_LObj* light = sp_light();
    if (!light) return;
    for (SpShadow* s = sp_shadows; s; s = s->next) {
        Vec3 origin = {0,0,0};
        if (s->joint) {
            HSD_JObj* j = s->joint;
            if (s->center_depth) lb_80011E24(j, &j, s->center_depth, -1);
            lb_8000B1CC(j, NULL, &origin);
        }
        sp_shadow_draw(Camera_80030B24(), light, s->lb.shadow, &origin, s->custom, s->radius);
    }
}
static void sp_camera(HSD_GObj* g, intptr_t pass)
{
    if (sp_render_timer) --sp_render_timer;
    else { sp_render_timer = sp_shadow_rate; sp_render_shadows(); }
    sp_original_camera(g, pass);
}
void sp_shadow_install(void)
{
    HSD_GObj* camera = Camera_80030A50();
    sp_original_camera = camera->render_cb; camera->render_cb = sp_camera;
    sp_regions = NULL; sp_shadows = NULL;
    int counts[20] = {0};
    for (int i = 0; i < 29; ++i) {
        SpShadowParam* p = &sp_shadow_params[i]; p->shadow = NULL; p->gobj = NULL;
        for (int j = 0; j < 8; ++j) ++counts[p->receivers[j]];
    }
    for (int i = 1; i < 20; ++i) if (counts[i] > 8) OSReport("shadow receivers: %d exceeds 8\n", counts[i]);
    /* Jev native_hardware_branch chose desktop. Its non-original-hardware
     * branch stores the zero left by the parameter-count loop as the rate. */
    sp_shadow_rate = sp_is_gc ? 4 : 0;
}
