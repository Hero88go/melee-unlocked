/* GrCasino: exports 1 and 3 to 9. Create helpers +178/+1F4 equal the plain collision/anim
 * helper's body. Cards use joint callbacks and a four-state animation machine (+394, +55C). */
#include "../common/mu_gr_common.h"
#include <melee/ft/types.h>
#include <melee/gr/granime.h>
#include <melee/gr/grdatfiles.h>
#include <melee/gr/ground.h>
#include <melee/lb/lb_00B0.h>
#include <melee/lb/lb_00F9.h>
#include <melee/lb/lbspdisplay.h>
#include <melee/lb/types.h>
#include <melee/mp/mplib.h>
#include <sysdolphin/baselib/aobj.h>
#include <sysdolphin/baselib/dobj.h>
#include <sysdolphin/baselib/gobj.h>
#include <sysdolphin/baselib/jobj.h>
#include <sysdolphin/baselib/mobj.h>
#include <sysdolphin/baselib/random.h>
#include <sysdolphin/baselib/tobj.h>

typedef struct CasinoParam { int occupied_wait, clear_wait, down_wait; } DISC_STRUCT CasinoParam;
typedef struct CardVars { int state, joint, face; u16 occupied, held; int timer; } CardVars;
static CasinoParam* card_param;
static CardVars* card_vars(Ground* gp) { return (CardVars*) &gp->u; }
static bool casino_false(Ground_GObj* gobj) { (void) gobj; return false; }
static void casino_create(Ground_GObj* gobj)
{
    Ground* gp = HSD_GObjGetUserData(gobj);
    Ground_InitMapColl(GET_JOBJ(gobj), gp->map_id);
    grAnime_801C8138(gobj, gp->map_id, 0);
}
static void casino_frame(Ground_GObj* gobj) { Ground_UpdateMapColl(gobj); }
static void casino_stage_create(Ground_GObj* gobj)
{
    Ground* gp = HSD_GObjGetUserData(gobj);
    gp->x10_flags.b5 = 1;
    casino_create(gobj);
}
static void card_land(void* data, int joint, CollData* coll, int kind,
                      mpLib_GroundEnum ground_kind, float delta)
{
    CardVars* v = card_vars(data);
    HSD_GObj* other = coll->x0_gobj;
    (void) joint; (void) kind; (void) ground_kind; (void) delta;
    v->occupied = 1;
    if (other != NULL && other->classifier == 4) {
        Fighter* fp = HSD_GObjGetUserData(other);
        if (fp->x1A5C != NULL && fp->grab_timer == 0.0F) v->held = 1;
    }
}
static void card_change(Ground_GObj* gobj, int state)
{
    Ground* gp = HSD_GObjGetUserData(gobj);
    CardVars* v = card_vars(gp);
    HSD_JObj* joint;
    lb_80011E24(GET_JOBJ(gobj), &joint, 2, -1);
    grAnime_801C8138(gobj, gp->map_id, state);
    v->state = state;
    if (state == 0) v->occupied = 0;
    if (state >= 0 && state <= 3) v->timer = 0;
    if (state == 3) {
        v->face = HSD_Randi((int) (joint->u.dobj->mobj->tobj->aobj->end_frame + 1.0F));
    }
    /* The single-precision bias 0x59800004 exactly represents the console's integer bias. */
    HSD_ForeachAnim(joint, 6, 0x400, HSD_AObjReqAnim, AOBJ_ARG_AF, (double) (float) v->face);
    HSD_JObjAnim(joint);
    HSD_ForeachAnim(joint, 6, 0x400, HSD_AObjStopAnim, AOBJ_ARG_AOV, NULL, NULL);
}
static void card_create(Ground_GObj* gobj)
{
    Ground* gp = HSD_GObjGetUserData(gobj);
    CardVars* v = card_vars(gp);
    struct UnkStageDat_x8_t* maps;
    gp->x10_flags.b5 = 1;
    Ground_InitMapColl(GET_JOBJ(gobj), gp->map_id);
    maps = DP(grDatFiles_801C6330(gp->map_id)->unk4->unk8);
    v->joint = DP(maps[gp->map_id].unk20)[0].x;
    mpJointSetCb1(v->joint, gp, card_land);
    card_change(gobj, 3);
}
static void card_frame(Ground_GObj* gobj)
{
    Ground* gp = HSD_GObjGetUserData(gobj);
    CardVars* v = card_vars(gp);
    switch (v->state) {
    case 0:
        v->timer = v->occupied ? v->timer + 1 : 0;
        if (v->timer >= card_param->occupied_wait) card_change(gobj, 1);
        break;
    case 1:
        v->timer++;
        if (v->timer >= card_param->clear_wait && !v->held) card_change(gobj, 2);
        break;
    case 2:
        if (grAnime_801C83D0(gobj, false, 0xFFFF)) {
            v->timer++;
            if (v->timer >= card_param->down_wait) card_change(gobj, 3);
            break;
        }
        /* +4B0 falls through to the state 3 completion check while animation 2 runs. */
        if (grAnime_801C83D0(gobj, false, 0xFFFF)) card_change(gobj, 0);
        break;
    case 3:
        if (grAnime_801C83D0(gobj, false, 0xFFFF)) card_change(gobj, 0);
        break;
    }
    v->occupied = 0;
    v->held = 0;
    Ground_UpdateMapColl(gobj);
}
#define CASINO_ROW(create, frame) { create, casino_false, frame, mu_gr_gobj_nop, 0 }
static StageCallbacks casino_map[6] = {
    CASINO_ROW(casino_create, casino_frame), CASINO_ROW(casino_create, casino_frame),
    CASINO_ROW(casino_stage_create, casino_frame), CASINO_ROW(card_create, card_frame),
    CASINO_ROW(card_create, card_frame), CASINO_ROW(card_create, card_frame),
};
static void casino_init(void)
{
    int i;
    card_param = stage_info.yakumono_param;
    for (i = 0; i < 6; i++) mu_grTSeak_80223908(i);
    mpLib_80058560();
    mu_gr_wind_gobj();
    Ground_801C39C0(); Ground_801C3BB4();
}
static bool casino_shadow(Vec3* pos, int arg, HSD_JObj* jobj)
{
    Vec3 world;
    (void) arg;
    lb_8000B1CC(jobj, NULL, &world);
    return world.y < pos->y + 2.0F;
}
const MuGrStage mu_gr_crown_casino = {
    .name = "Crown Casino", .file = "GrCasino.dat", .map_gobjs = casino_map,
    .on_init = casino_init, .on_demo_init = mu_gr_demo_nop,
    .on_load = mu_gr_event_nop, .on_start = mu_gr_event_nop, .callback4 = mu_gr_false,
    .on_touch_line = mu_gr_no_touch_line, .on_check_shadow_render = casino_shadow, .flags = MU_GR_READY,
};
