/* GrTDd +160 creates the floor and samples water joint 13. +224 tests downward crossing.
 * +2B8 scales the splash and plays stage bank sound 5, with the console's explicit parameters. */
#include "targets_lib.h"
#include "../common/mu_gr_mex.h"
#include <melee/cm/types.h>
#include <melee/ft/ftdevice.h>
#include <melee/ft/types.h>
#include <melee/gr/grlib.h>
#include <melee/gr/ground.h>
#include <melee/gr/stage.h>
#include <melee/lb/lb_00B0.h>
#include <sysdolphin/baselib/axdriver.h>
#include <sysdolphin/baselib/gobj.h>
#include <sysdolphin/baselib/psstructs.h>

typedef struct DiddyVars { Vec3 water; } DiddyVars;
static DiddyVars* diddy_vars(Ground_GObj* gobj)
{
    Ground* gp = HSD_GObjGetUserData(gobj);
    return (DiddyVars*) &gp->u;
}
MU_NO_CONTRACT static bool diddy_splash(Ground_GObj* gobj, Fighter_GObj* fighter, Vec3* out)
{
    Fighter* fp = HSD_GObjGetUserData(fighter);
    float water = diddy_vars(gobj)->water.y;
    (void) out;
    if (!fp->x221D_b7 && fp->cur_pos.y < water && water < fp->prev_pos.y) {
        Vec3 pos = { fp->cur_pos.x, water, fp->cur_pos.z };
        float scale = fp->x890_cameraBox->target_ext.v.z / 10.0F;
        HSD_Generator* gen = grLib_801C96F8(0x7530, 30, &pos);
        if (gen != NULL) {
            gen->appsrt->scale.x *= scale;
            gen->appsrt->scale.y *= scale;
            gen->appsrt->scale.z *= scale;
        }
        HSD_AudioSFXStartParam(mu_gr_mex_stage_bank(Stage_8022519C(Stage_80225194())) * 10000 + 5,
                               180, 127, 0, 6);
    }
    return false;
}
static void diddy_floor_create(Ground_GObj* gobj)
{
    targets_coll_create(gobj);
    lb_8000B1CC(Ground_801C3FA4(gobj, 13), NULL, &diddy_vars(gobj)->water);
    ftCo_800C07F8(gobj, 6, diddy_splash);
}
static StageCallbacks diddy_map[3] = {
    TARGETS_ROW(targets_points_create, mu_gr_gobj_nop, 0),
    TARGETS_ROW(targets_coll_create, targets_coll_frame, 0),
    TARGETS_ROW(diddy_floor_create, targets_floor_frame, 0xC0000000),
};
const MuGrStage mu_gr_targets_diddy = {
    .name = "Targets!Diddy", .file = "/GrTDd.dat",
    TARGETS_EXPORTS(diddy_map, mu_gr_no_touch_line), .flags = MU_GR_READY,
};
