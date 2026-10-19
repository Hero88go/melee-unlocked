/* The "Akaneia family" helpers of WP4 (akfamily_lib.h). Offsets are GrGh.dat's grFunction block
 * (run-source/rel09-stages grdis.py ak GrGh.dat), the same code in GrLm.dat at the offsets given. */
#include "akfamily_lib.h"

#include <melee/gr/granime.h>
#include <melee/gr/grmaterial.h>
#include <melee/gr/ground.h>
#include <melee/gr/types.h>
#include <sysdolphin/baselib/debug.h>
#include <sysdolphin/baselib/gobj.h>

/* OnInit (GrGh +12C, GrLm +104): the param pointer is stored before the check, so a stage without
 * one still writes NULL, then asserts with the block's own file name and line. */
void* mexgr_wp4_yakumono(const char* file, int line)
{
    void* param = Ground_GetYakumonoParam();
    if (param == NULL) {
        __assert(file, line, "yakumono not found");
    }
    return param;
}

bool mexgr_wp4_unk(Ground_GObj* gobj)
{
    return gobj != NULL;
}

void mexgr_wp4_deletion(Ground_GObj* gobj)
{
    (void) gobj;
}

void mexgr_wp4_anim0_create(Ground_GObj* gobj)
{
    Ground* gp = HSD_GObjGetUserData(gobj);
    grAnime_801C8138(gobj, gp->map_id, 0);
}

void mexgr_wp4_background_create(Ground_GObj* gobj)
{
    Ground* gp = HSD_GObjGetUserData(gobj);
    grAnime_801C8138(gobj, gp->map_id, 0);
    gp->x10_flags.b2 = 0;
}

/* Breakable_OnCreation+30 .. +B8 (GrGh), Checkpoint_OnCreation+24 .. +84 (GrGh),
 * Pillar_OnCreation+1C .. +8C (GrLm). */
Item_GObj* mexgr_wp4_hurtbox(Ground* gp, HSD_JObj* jobj,
                             void (*on_hurt)(Item_GObj*, Ground*, Vec3*, HSD_GObj*, f32),
                             float ax, float ay, float az, float bx, float by, float bz,
                             float scale)
{
    Item_GObj* item = grMaterial_801C8CFC(0, 0, gp, jobj, NULL, on_hurt, NULL);
    grMaterial_801C8DE0(item, ax, ay, az, bx, by, bz, scale);
    grMaterial_801C8E08(item);
    return item;
}
