/* Rainbow Ride (ACE 2.0.0, internal 126, external 343), the m-ex grFunction block of GrNRc.dat:
 * 131 instructions with symbols (disassembly: run-source/rel09-stages/out_dis_GrNRc.txt,
 * `py -3.12 grdis.py ace GrNRc.dat`).
 *
 * Every export is its own. on_init creates every map gobj of the file with Map_Create (the plain
 * collision and animation shape, mu_gr_map_create_plain), then a wind gobj that also runs
 * mpLib_80058560, then the camera range and blast zone. The map gobj table is only read for its
 * flags (no callbacks): entry 1 has b0, b1, b2 (lights, fog, camera of map gobj 1; GrNRc.dat's map
 * gobj 1 has lights and fog, no camera). The shadow check is its own: a shadow is drawn where the
 * surface joint is lower than the fighter plus 2. */
#include "../common/mu_gr_common.h"

#include <melee/gr/ground.h>
#include <melee/gr/types.h>
#include <melee/lb/lb_00B0.h>
#include <melee/mp/mplib.h>
#include <sysdolphin/baselib/gobjproc.h>

/* map_gobjs (+0): ten rows, all callbacks NULL. */
static StageCallbacks mexgr_rr_map[10] = {
    { NULL, NULL, NULL, NULL, 0 },
    { NULL, NULL, NULL, NULL, 0xE0000000 },
    { NULL, NULL, NULL, NULL, 0 },
    { NULL, NULL, NULL, NULL, 0 },
    { NULL, NULL, NULL, NULL, 0 },
    { NULL, NULL, NULL, NULL, 0 },
    { NULL, NULL, NULL, NULL, 0 },
    { NULL, NULL, NULL, NULL, 0 },
    { NULL, NULL, NULL, NULL, 0 },
    { NULL, NULL, NULL, NULL, 0 },
};

static void mexgr_rr_coll_proc(HSD_GObj* gobj)
{
    (void) gobj;
    mpLib_80058560();
}

/* OnInit (+C8) */
static void mexgr_rr_init(void)
{
    const int count = mu_gr_map_count();
    HSD_GObj* wind;
    int i;
    for (i = 0; i < count; i++) {
        mu_gr_map_create_plain(i);
    }
    wind = mu_gr_wind_gobj();
    HSD_GObj_SetupProc(wind, mexgr_rr_coll_proc, 4);
    Ground_801C39C0();
    Ground_801C3BB4();
}

/* OnUnk4 (+180), export 9: the joint's world y against the fighter's y + 2 ("fcmpu; cr7 lt"). */
static bool mexgr_rr_check_shadow(Vec3* pos, int arg, HSD_JObj* jobj)
{
    Vec3 at;
    (void) arg;
    lb_8000B1CC(jobj, NULL, &at);
    return at.y < pos->y + 2.0F;
}

const MuGrStage mu_gr_rainbow_ride = {
    .name = "Rainbow Ride",
    .file = "GrNRc.dat",
    .map_gobjs = mexgr_rr_map,
    .on_init = mexgr_rr_init,
    .on_demo_init = mu_gr_demo_nop,
    .on_load = mu_gr_event_nop,
    .on_start = mu_gr_event_nop,
    .callback4 = mu_gr_false,
    .on_touch_line = mu_gr_no_touch_line,
    .on_check_shadow_render = mexgr_rr_check_shadow,
    .flags = MU_GR_READY,
};
