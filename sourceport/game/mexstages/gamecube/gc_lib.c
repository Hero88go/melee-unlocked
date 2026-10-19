/* Family wrappers, copied into this stage with unique external names.
 * GrGc +414, +440, +448. */
#include "gc_lib.h"
#include <melee/gr/granime.h>
#include <melee/gr/ground.h>
#include <sysdolphin/baselib/debug.h>
void* mexgr_gc_yakumono(const char* file, int line)
{
    void* p = Ground_GetYakumonoParam();
    if (p == NULL) __assert(file, line, "yakumono not found");
    return p;
}
bool mexgr_gc_unk(Ground_GObj* gobj) { return gobj != NULL; }
bool mexgr_gc_false(Ground_GObj* gobj) { (void) gobj; return false; }
void mexgr_gc_deletion(Ground_GObj* gobj) { (void) gobj; }
void mexgr_gc_anim0_create(Ground_GObj* gobj)
{
    Ground* gp = HSD_GObjGetUserData(gobj);
    grAnime_801C8138(gobj, gp->map_id, 0);
}
