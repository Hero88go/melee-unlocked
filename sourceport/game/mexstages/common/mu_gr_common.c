/* Helpers for native m-ex stage code (mu_gr_common.h). */
#include "mu_gr_common.h"

#include <melee/gr/forward.h>
#include <melee/lb/forward.h>

#include <melee/gr/granime.h>
#include <melee/gr/grdatfiles.h>
#include <melee/gr/grdisplay.h>
#include <melee/gr/ground.h>
#include <melee/gr/types.h>
#include <melee/lb/lb_00F9.h>
#include <sysdolphin/baselib/gobj.h>
#include <sysdolphin/baselib/gobjgxlink.h>
#include <sysdolphin/baselib/gobjproc.h>

void mu_gr_event_nop(void) {}

void mu_gr_demo_nop(int arg)
{
    (void) arg;
}

bool mu_gr_false(void)
{
    return false;
}

DynamicsDesc* mu_gr_no_touch_line(int index)
{
    (void) index;
    return NULL;
}

bool mu_gr_shadow_always(Vec3* pos, int arg, HSD_JObj* jobj)
{
    (void) pos;
    (void) arg;
    (void) jobj;
    return true;
}

void mu_gr_gobj_nop(HSD_GObj* gobj)
{
    (void) gobj;
}

int mu_gr_map_count(void)
{
    return grDatFiles_801C6330(0)->unk4->unkC;
}

/* Map_OnCreate (GrNRc.dat +26C, template +138): bind the collision joints, start animation set 0. */
static void mu_gr_plain_create(HSD_GObj* gobj)
{
    Ground* gp = HSD_GObjGetUserData(gobj);
    Ground_InitMapColl(GET_JOBJ(gobj), gp->map_id);
    grAnime_801C8138(gobj, gp->map_id, 0);
}

/* Map_OnFrame (GrNRc.dat +2BC, template +188). */
static void mu_gr_plain_frame(HSD_GObj* gobj)
{
    Ground_UpdateMapColl(gobj);
}

/* Map_Create (GrNRc.dat +1D8, template +A8). The block calls Ground_GetStageGObj through a word
 * that holds its retail address (0x801C14D0) and sets xC to a lone blr ("Map_OnUnk"). */
HSD_GObj* mu_gr_map_create_plain(int map_id)
{
    HSD_GObj* gobj = Ground_GetStageGObj(map_id);
    if (gobj != NULL) {
        Ground* gp = HSD_GObjGetUserData(gobj);
        gp->x8_callback = NULL;
        gp->xC_callback = mu_gr_gobj_nop;
        GObj_SetupGXLink(gobj, grDisplay_801C5DB0, 3, 0);
        mu_gr_plain_create(gobj);
        HSD_GObj_SetupProc(gobj, mu_gr_plain_frame, 4);
    }
    return gobj;
}

static void mu_gr_wind_proc(HSD_GObj* gobj)
{
    (void) gobj;
    lb_800115F4();
}

HSD_GObj* mu_gr_wind_gobj(void)
{
    HSD_GObj* gobj = GObj_Create(3, 5, 0);
    HSD_GObj_SetupProc(gobj, mu_gr_wind_proc, 4);
    return gobj;
}
