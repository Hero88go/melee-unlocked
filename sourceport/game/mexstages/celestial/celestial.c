/* GrClv: five map rows, exports 1 and 3 to 9. +258 and +25C are empty callbacks beyond
 * the inventory tool's inferred code extent. Both floor rows intentionally add all three joints. */
#include "../common/mu_gr_common.h"
#include <melee/gr/granime.h>
#include <melee/gr/ground.h>
#include <melee/mp/mplib.h>
#include <sysdolphin/baselib/debug.h>
#include <sysdolphin/baselib/gobj.h>

static bool celestial_false(Ground_GObj* gobj) { (void) gobj; return false; }
static void celestial_anim(Ground_GObj* gobj)
{
    Ground* gp = HSD_GObjGetUserData(gobj);
    grAnime_801C8138(gobj, gp->map_id, 0);
}
static void celestial_background(Ground_GObj* gobj)
{
    Ground* gp = HSD_GObjGetUserData(gobj);
    celestial_anim(gobj);
    gp->x10_flags.b2 = 0;
}
static void celestial_floor(Ground_GObj* gobj)
{
    Ground* gp = HSD_GObjGetUserData(gobj);
    celestial_anim(gobj);
    Ground_InitMapColl(GET_JOBJ(gobj), gp->map_id);
    mpJointListAdd(0);
    mpJointListAdd(1);
    mpJointListAdd(2);
}
#define CLV_ROW(create, flags) { create, celestial_false, mu_gr_gobj_nop, mu_gr_gobj_nop, flags }
static StageCallbacks celestial_map[5] = {
    CLV_ROW(celestial_anim, 0),
    CLV_ROW(celestial_background, 0xC0000000),
    CLV_ROW(celestial_background, 0),
    CLV_ROW(celestial_floor, 0xC0000000),
    CLV_ROW(celestial_floor, 0xC0000000),
};
static void celestial_init(void)
{
    int i;
    if (Ground_GetYakumonoParam() == NULL) __assert("CelestialValley.c", 0xE0, "yakumono node not found");
    for (i = 0; i < 5; i++) mu_grTSeak_80223908(i);
    Ground_801C39C0();
    Ground_801C3BB4();
}
const MuGrStage mu_gr_celestial_valley = {
    .name = "Celestial Valley", .file = "GrClv.dat", .map_gobjs = celestial_map,
    .on_init = celestial_init, .on_demo_init = mu_gr_demo_nop,
    .on_load = mu_gr_event_nop, .on_start = mu_gr_event_nop,
    .callback4 = mu_gr_false, .on_touch_line = mu_gr_no_touch_line,
    .on_check_shadow_render = mu_gr_shadow_always, .flags = MU_GR_READY,
};
