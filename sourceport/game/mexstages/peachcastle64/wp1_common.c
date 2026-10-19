/* Shared code of the WP1 stages (wp1_common.h). */
#include "wp1_common.h"

#include <melee/gr/granime.h>
#include <melee/gr/grmaterial.h>
#include <melee/gr/ground.h>
#include <melee/gr/types.h>
#include <sysdolphin/baselib/aobj.h>
#include <sysdolphin/baselib/dobj.h>
#include <sysdolphin/baselib/gobj.h>
#include <sysdolphin/baselib/jobj.h>
#include <sysdolphin/baselib/mobj.h>
#include <sysdolphin/baselib/tobj.h>

/* 802239F0 stageGObj0_OnInit: Ground_StartMapAnim. */
void mexgr_wp1_tseak_anim(Ground_GObj* gobj)
{
    Ground* gp = HSD_GObjGetUserData(gobj);
    grAnime_801C8138(gobj, gp->map_id, 0);
}

/* 80223B14 stageGObj1_GObjProc */
void mexgr_wp1_tseak_coll(Ground_GObj* gobj)
{
    Ground_UpdateMapColl(gobj);
}

/* 80223A1C, 80223A7C, 80223B0C: "li r3, 0; blr" (the never called callback1). */
bool mexgr_wp1_tseak_false(Ground_GObj* gobj)
{
    (void) gobj;
    return false;
}

/* 80223A24, 80223A28, 80223AB8, 80223B34: a lone blr. */
void mexgr_wp1_tseak_nop(Ground_GObj* gobj)
{
    (void) gobj;
}

/* Background_Create: grAnime_801C8138(gobj, map_id, 0); lhz +0x10; rlwinm 0, 27, 23 (clears the
 * top three bits of the byte at +0x11, the console's x11 b012); ori 0x40 (b012 = 2); sth; then
 * grMaterial_801C8858(gobj->hsd_obj, 0x28000000). */
void mexgr_wp1_background_create(Ground_GObj* gobj)
{
    Ground* gp = HSD_GObjGetUserData(gobj);
    grAnime_801C8138(gobj, gp->map_id, 0);
    gp->x11_flags.b012 = 2;
    grMaterial_801C8858(GET_JOBJ(gobj), 0x28000000);
}

void mexgr_wp1_init(int count)
{
    int i;
    for (i = 0; i < count; i++) {
        mu_grTSeak_80223908(i);
    }
    Ground_801C39C0();
    Ground_801C3BB4();
}

/* GrOSf.dat +610: lwz r3, 0x7C (aobj); child (+10) first, then next (+8). */
HSD_AObj* mexgr_wp1_first_aobj(HSD_JObj* jobj)
{
    HSD_AObj* aobj = jobj->aobj;
    if (aobj != NULL) {
        return aobj;
    }
    if (jobj->child != NULL) {
        aobj = mexgr_wp1_first_aobj(jobj->child);
        if (aobj != NULL) {
            return aobj;
        }
    }
    if (jobj->next != NULL) {
        return mexgr_wp1_first_aobj(jobj->next);
    }
    return NULL;
}

/* GrOHy.dat item 0 +5FC. The display object list is read from the joint's union (+18) whatever
 * the joint's kind, as the block does. */
HSD_AObj* mexgr_wp1_first_aobj_all(HSD_JObj* jobj)
{
    HSD_AObj* aobj = jobj->aobj;
    HSD_DObj* dobj;
    if (aobj != NULL) {
        return aobj;
    }
    for (dobj = jobj->u.dobj; dobj != NULL; dobj = dobj->next) {
        HSD_TObj* tobj;
        if (dobj->aobj != NULL) {
            return dobj->aobj;
        }
        if (dobj->mobj->aobj != NULL) {
            return dobj->mobj->aobj;
        }
        for (tobj = dobj->mobj->tobj; tobj != NULL; tobj = tobj->next) {
            if (tobj->aobj != NULL) {
                return tobj->aobj;
            }
        }
    }
    if (jobj->child != NULL) {
        aobj = mexgr_wp1_first_aobj_all(jobj->child);
        if (aobj != NULL) {
            return aobj;
        }
    }
    if (jobj->next != NULL) {
        return mexgr_wp1_first_aobj_all(jobj->next);
    }
    return NULL;
}
