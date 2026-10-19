/* The five small target blocks share OnInit, Points and collision helpers.
 * Offsets: GrTLc +3C, +A8, +E4, +138, +1B4; GrTWf +3C, +158, +194, +1E8, +264.
 * GrTLz and GrTSn have identical code. Map callback1's lone blr returns its argument.
 * Destruction callbacks write r3=0 and return, with no observable effect. */
#include "targets_lib.h"
#include <melee/gr/granime.h>
#include <melee/gr/ground.h>
#include <melee/lb/lb_00F9.h>
#include <sysdolphin/baselib/gobj.h>

void targets_init(void)
{
    int i;
    for (i = 0; i < 3; i++) mu_grTSeak_80223908(i);
    Ground_801C39C0();
    Ground_801C3BB4();
    Ground_801C4210();
    Ground_801C42AC();
}

void targets_points_create(Ground_GObj* gobj)
{
    Ground* gp = HSD_GObjGetUserData(gobj);
    grAnime_801C8138(gobj, gp->map_id, 0);
}

void targets_coll_create(Ground_GObj* gobj)
{
    Ground* gp = HSD_GObjGetUserData(gobj);
    Ground_InitMapColl(GET_JOBJ(gobj), gp->map_id);
    grAnime_801C8138(gobj, gp->map_id, 0);
}

void targets_coll_frame(Ground_GObj* gobj) { Ground_UpdateMapColl(gobj); }

void targets_floor_frame(Ground_GObj* gobj)
{
    lb_800115F4();
    Ground_UpdateMapColl(gobj);
}

bool targets_unk(Ground_GObj* gobj) { return gobj != NULL; }
