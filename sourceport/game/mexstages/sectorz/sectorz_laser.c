/* Sector Z's laser (m-ex item kind 252): itFunction 0 of GrOSz.dat, 239 instructions with symbols
 * (`py -3.12 grdis.py ak GrOSz.dat it 0`, run-source/rel09-stages/out_dis_GrOSz_it0.txt). The Arwing
 * (sectorz.c, SpawnBullet) creates it aimed at a fighter; the model's joint 0 is the beam, joint 1
 * its tip. It flies on its animation, explodes on fighters and on map lines it meets, and turns
 * around when reflected. */
#include "sectorz.h"

#include <math.h>

#include <dolphin/mtx.h>
#include <melee/ef/efsync.h>
#include <melee/it/inlines.h>
#include <melee/it/it_2725.h>
#include <melee/it/item.h>
#include <melee/it/types.h>
#include <melee/lb/lb_00B0.h>
#include <melee/lb/lbvector.h>
#include <melee/mp/mplib.h>
#include <melee/mp/types.h>
#include <sysdolphin/baselib/jobj.h>

static MexgrSzLaserVars* mexgr_sz_lv(Item* ip) { return (MexgrSzLaserVars*) &ip->xDD4_itemVar; }

/* OnDestroy (+10): a lone blr. */
static void mexgr_sz_laser_destroy(Item_GObj* gobj) { (void) gobj; }

/* OnGiveDamage (+14): the hit effect (0x40E) at the tip and sound 0x74; the item goes. */
static bool mexgr_sz_laser_hit(Item_GObj* gobj)
{
    Item* ip = GET_ITEM(gobj);
    Vec3 tip;
    lb_8000B1CC(ip->xBBC_dynamicBoneTable->bones[1], NULL, &tip);
    efSync_Spawn(0x40E, gobj, &tip);
    Item_8026AE84(ip, 0x74, 0x7F, 0x40);
    return true;
}

/* HSD_JObjSetMtxDirtySub (803732E8) with the value it leaves in r3, which OnReflect returns: the
 * joint itself (non zero) when it is an instance or no child is cleaned, otherwise the dirty test
 * of the last child it looked at (1 dirty, 0 clean) or what its recursive call left. The same
 * writes as the function, in the same order. */
static unsigned int mexgr_sz_dirty_sub_r3(HSD_JObj* jobj)
{
    unsigned int r3 = 1;   /* the joint pointer: non zero */
    HSD_JObj* c;
    jobj->flags |= JOBJ_MTX_DIRTY;
    if (jobj->flags & 0x1000u) {
        return r3;
    }
    for (c = jobj->child; c != NULL; c = c->next) {
        HSD_JObj* g;
        if (c->flags & 0x01000000u) {
            continue;
        }
        r3 = (!(c->flags & JOBJ_USER_DEF_MTX) && (c->flags & JOBJ_MTX_DIRTY)) ? 1u : 0u;
        if (r3 != 0) {
            continue;
        }
        c->flags |= JOBJ_MTX_DIRTY;
        if (c->flags & 0x1000u) {
            continue;
        }
        for (g = c->child; g != NULL; g = g->next) {
            if (g->flags & 0x01000000u) {
                continue;
            }
            r3 = (!(g->flags & JOBJ_USER_DEF_MTX) && (g->flags & JOBJ_MTX_DIRTY)) ? 1u : 0u;
            if (r3 != 0) {
                continue;
            }
            r3 = mexgr_sz_dirty_sub_r3(g);
        }
    }
    return r3;
}

/* OnReflect (+88): the item position from the tip, then the beam turned to its new direction: the
 * velocity with x reversed once it flies, else the tip's last step reversed in x. The block ends on
 * HSD_JObjSetMtxDirtySub and returns what that leaves in r3 (see above). */
static bool mexgr_sz_laser_reflect(Item_GObj* gobj)
{
    Item* ip = GET_ITEM(gobj);
    MexgrSzLaserVars* lv = mexgr_sz_lv(ip);
    HSD_JObj* beam;
    Vec3 dir;
    lb_8000B1CC(ip->xBBC_dynamicBoneTable->bones[1], NULL, &ip->pos);
    if (lv->started != 0) {
        ip->x40_vel.x = -ip->x40_vel.x;
        dir = ip->x40_vel;
    } else {
        PSVECSubtract(&lv->cur, &lv->prev, &dir);
        dir.x = -dir.x;
    }
    lbVector_Normalize(&dir);
    beam = ip->xBBC_dynamicBoneTable->bones[0];
    beam->rotate.x = asinf(dir.y);   /* through the block's data word holding asinf's address */
    beam->rotate.y = atan2f(dir.x, dir.z);
    beam->rotate.z = 0.0F;
    return mexgr_sz_dirty_sub_r3(beam) != 0;
}

/* OnClank (+168): as a hit. */
static bool mexgr_sz_laser_clank(Item_GObj* gobj) { return mexgr_sz_laser_hit(gobj); }

/* OnUnk2, OnHitShieldBounce, OnHitShieldDetermineDestroy (+188, +190, +198): "li r3, 1". */
static bool mexgr_sz_laser_true(Item_GObj* gobj) { (void) gobj; return true; }

/* UpdatePosition (+370): the tip's last position, then its new one. */
static void mexgr_sz_laser_track(Item_GObj* gobj)
{
    Item* ip = GET_ITEM(gobj);
    MexgrSzLaserVars* lv = mexgr_sz_lv(ip);
    lv->prev = lv->cur;
    lb_8000B1CC(ip->xBBC_dynamicBoneTable->bones[1], NULL, &lv->cur);
}

/* Main_Anim (+1A0): while the animation runs and before the first frame is marked, the tip is
 * tracked; at its end the velocity becomes the tip's last step and the item flies on. */
static bool mexgr_sz_laser_anim(Item_GObj* gobj)
{
    Item* ip = GET_ITEM(gobj);
    MexgrSzLaserVars* lv = mexgr_sz_lv(ip);
    if (!it_80272C6C(gobj)) {
        if (lv->started == 0) {
            PSVECSubtract(&lv->cur, &lv->prev, &ip->x40_vel);
        }
        lv->started = 1;
    } else if (lv->started == 0) {
        mexgr_sz_laser_track(gobj);
    }
    return it_80273130(gobj);
}

/* Main_Phys (+228): a lone blr. */
static void mexgr_sz_laser_phys(Item_GObj* gobj) { (void) gobj; }

/* Main_Coll (+22C): the second shot of a pair never collides. Otherwise the beam from joint 0 to
 * joint 1 is tested against the map (mpCheckAllRemap); on a line enabled in the line table (its
 * entry's flag byte +5 bit 0) whose own flags do not pass shots (lo_flags +E bit 0), the hit effect
 * and sound play there and the item goes. */
static bool mexgr_sz_laser_coll(Item_GObj* gobj)
{
    Item* ip = GET_ITEM(gobj);
    Vec3 a, b, at;
    int line;
    if (mexgr_sz_lv(ip)->no_coll != 0) {
        return false;
    }
    lb_8000B1CC(ip->xBBC_dynamicBoneTable->bones[0], NULL, &a);
    lb_8000B1CC(ip->xBBC_dynamicBoneTable->bones[1], NULL, &b);
    if (!mpCheckAllRemap(&at, &line, NULL, NULL, -1, -1, a.x, a.y, b.x, b.y)) {
        return false;
    }
    {
        CollLine* lines = mpGetGroundCollLine();
        if (!((lines[line].flags >> 16) & 1)) {
            return false;
        }
        if ((lines[line].x0->lo_flags >> 8) & 1) {
            return false;
        }
    }
    efSync_Spawn(0x40E, gobj, &at);
    Item_8026AE84(ip, 0x74, 0x7F, 0x40);
    return true;
}

static ItemStateTable mexgr_sz_laser_states[] = {
    { 0, mexgr_sz_laser_anim, mexgr_sz_laser_phys, mexgr_sz_laser_coll },
};

/* The m-ex itFunction slots in ItemLogicTable order: 0 states, 2 OnDestroy, 6 OnGiveDamage,
 * 9 OnReflect, 10 OnClank, 11 OnUnk2, 12 OnHitShieldBounce, 13 OnHitShieldDetermineDestroy. */
ItemLogicTable mexgr_sz_items[1] = {
    {
        .states = mexgr_sz_laser_states,
        .destroyed = mexgr_sz_laser_destroy,
        .dmg_dealt = mexgr_sz_laser_hit,
        .reflected = mexgr_sz_laser_reflect,
        .clanked = mexgr_sz_laser_clank,
        .absorbed = mexgr_sz_laser_true,
        .shield_bounced = mexgr_sz_laser_true,
        .hit_shield = mexgr_sz_laser_true,
    },
};
