/* The family wrappers, copied locally with unique symbols. GrAc +384, +410, +460. */
#include "village.h"
#include <melee/gr/granime.h>
#include <melee/gr/ground.h>
#include <sysdolphin/baselib/debug.h>

void* mexgr_village_yakumono(const char* file, int line)
{
    void* param = Ground_GetYakumonoParam();
    if (param == NULL) {
        __assert(file, line, "yakumono not found");
    }
    return param;
}

bool mexgr_village_unk(Ground_GObj* gobj) { return gobj != NULL; }
void mexgr_village_deletion(Ground_GObj* gobj) { (void) gobj; }

void mexgr_village_anim0_create(Ground_GObj* gobj)
{
    Ground* gp = HSD_GObjGetUserData(gobj);
    grAnime_801C8138(gobj, gp->map_id, 0);
}

void mexgr_village_background_create(Ground_GObj* gobj)
{
    Ground* gp = HSD_GObjGetUserData(gobj);
    grAnime_801C8138(gobj, gp->map_id, 0);
    gp->x10_flags.b2 = 0;
}
