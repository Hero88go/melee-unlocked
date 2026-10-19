/* GrWarioCastle: exports 1 and 3 to 9, six rows. The unsymbolized block continues through
 * +564, including the direct animation-frame setter at +4D8. Preserve its direct frame writes. */
#include "../common/mu_gr_common.h"
#include "../common/mu_gr_mex.h"
#include <melee/cm/types.h>
#include <melee/ft/ftdevice.h>
#include <melee/ft/types.h>
#include <melee/gr/granime.h>
#include <melee/gr/grlib.h>
#include <melee/gr/ground.h>
#include <melee/lb/lb_00F9.h>
#include <melee/mp/mplib.h>
#include <sysdolphin/baselib/aobj.h>
#include <sysdolphin/baselib/debug.h>
#include <sysdolphin/baselib/dobj.h>
#include <sysdolphin/baselib/gobj.h>
#include <sysdolphin/baselib/jobj.h>
#include <sysdolphin/baselib/mobj.h>
#include <sysdolphin/baselib/psstructs.h>
#include <sysdolphin/baselib/random.h>
#include <sysdolphin/baselib/tobj.h>

typedef struct CastleVars { int frame; } CastleVars;
static bool castle_false(Ground_GObj* gobj) { (void) gobj; return false; }
static CastleVars* castle_vars(Ground_GObj* gobj)
{
    Ground* gp = HSD_GObjGetUserData(gobj);
    return (CastleVars*) &gp->u;
}
static void castle_anim(Ground_GObj* gobj)
{
    Ground* gp = HSD_GObjGetUserData(gobj);
    grAnime_801C8138(gobj, gp->map_id, 0);
}
static void castle_background(Ground_GObj* gobj)
{
    Ground* gp = HSD_GObjGetUserData(gobj);
    castle_anim(gobj); gp->x10_flags.b2 = 0;
}
MU_NO_CONTRACT static bool castle_splash(Ground_GObj* gobj, Fighter_GObj* fighter, Vec3* out)
{
    Fighter* fp;
    (void) gobj; (void) out;
    if (fighter == NULL || (fp = HSD_GObjGetUserData(fighter)) == NULL) return false;
    if (fp->cur_pos.y < -25.0F && fp->prev_pos.y > -25.0F) {
        Vec3 pos = { fp->cur_pos.x, -25.0F, fp->cur_pos.z };
        float size = fp->x890_cameraBox->target_ext.v.z;
        HSD_Generator* gen = grLib_801C96F8(0x7530, 30, &pos);
        if (gen != NULL) {
            float scale = size / 15.0F;
            gen->appsrt->scale.x *= scale;
            gen->appsrt->scale.y *= scale;
            gen->appsrt->scale.z *= scale;
        }
        mu_gr_mex_sfx(2);
    }
    return false;
}
static void castle_water(Ground_GObj* gobj)
{
    castle_anim(gobj);
    ftCo_800C07F8(gobj, 6, castle_splash);
    ((Ground*) HSD_GObjGetUserData(gobj))->x10_flags.b2 = 0;
    mu_gr_mex_sfx(3);
}
static void castle_coll(Ground_GObj* gobj)
{
    Ground* gp = HSD_GObjGetUserData(gobj);
    castle_anim(gobj);
    Ground_InitMapColl(GET_JOBJ(gobj), gp->map_id);
}
static void castle_floor(Ground_GObj* gobj) { castle_coll(gobj); mpJointListAdd(0); }
static void castle_set_frame(HSD_JObj* joint, float frame)
{
    for (; joint != NULL; joint = joint->child) {
        HSD_DObj* dobj;
        if (joint->aobj != NULL) joint->aobj->curr_frame = frame;
        for (dobj = joint->u.dobj; dobj != NULL; dobj = dobj->next) {
            HSD_MObj* mobj;
            HSD_TObj* tobj;
            if (dobj->aobj != NULL) dobj->aobj->curr_frame = frame;
            mobj = dobj->mobj;
            if (mobj->aobj != NULL) mobj->aobj->curr_frame = frame;
            for (tobj = mobj->tobj; tobj != NULL; tobj = tobj->next) {
                if (tobj->aobj != NULL) tobj->aobj->curr_frame = frame;
            }
        }
    }
}
static void castle_picture_frame(Ground_GObj* gobj)
{
    castle_set_frame(Ground_801C3FA4(gobj, 0), (float) castle_vars(gobj)->frame);
}
static void castle_picture(Ground_GObj* gobj)
{
    int pick = HSD_Randi(100);
    castle_vars(gobj)->frame = pick <= 0 ? 1 : pick <= 10 ? 4 : pick < 31 ? 3 : 2;
    castle_anim(gobj);
    castle_picture_frame(gobj);
}
#define CASTLE_ROW(create, frame, flags) { create, castle_false, frame, mu_gr_gobj_nop, flags }
static StageCallbacks castle_map[6] = {
    CASTLE_ROW(castle_anim, mu_gr_gobj_nop, 0),
    CASTLE_ROW(castle_water, mu_gr_gobj_nop, 0),
    CASTLE_ROW(castle_background, mu_gr_gobj_nop, 0),
    CASTLE_ROW(castle_coll, mu_gr_gobj_nop, 0),
    CASTLE_ROW(castle_picture, castle_picture_frame, 0),
    CASTLE_ROW(castle_floor, mu_gr_gobj_nop, 0xC0000000),
};
static void castle_init(void)
{
    if (Ground_GetYakumonoParam() == NULL) __assert("WariosCastle.c", 0x143, "yakumono node not found");
    mu_grTSeak_80223908(0); mu_grTSeak_80223908(1); mu_grTSeak_80223908(2);
    mu_grTSeak_80223908(3); mu_grTSeak_80223908(5); mu_grTSeak_80223908(4);
    Ground_801C39C0(); Ground_801C3BB4();
}
const MuGrStage mu_gr_warios_castle = {
    .name = "Wario's Castle", .file = "GrWarioCastle.dat", .map_gobjs = castle_map,
    .on_init = castle_init, .on_demo_init = mu_gr_demo_nop,
    .on_load = mu_gr_event_nop, .on_start = mu_gr_event_nop, .callback4 = mu_gr_false,
    .on_touch_line = mu_gr_no_touch_line, .on_check_shadow_render = mu_gr_shadow_always, .flags = MU_GR_READY,
};
