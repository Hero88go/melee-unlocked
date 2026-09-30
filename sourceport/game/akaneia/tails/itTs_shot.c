/* Akaneia's Tails, native: the neutral special's projectile (article 0 of PlTs.dat, m-ex
 * itFunction) and the fighter-side spawn.
 *
 * The shot flies straight, falls once it leaves the ground it was fired along, keeps going after
 * it hits someone (dmg_dealt returns false), bounces off shields a limited number of times losing
 * speed each time, and fizzles (state 2) when its lifetime runs out. */
#include "ftTs.h"
#include "ftTs_hooks.h"

#include <melee/db/db.h>
#include <melee/ft/fighter.h>
#include <melee/ft/ftparts.h>
#include <melee/ft/inlines.h>
#include <melee/ft/types.h>
#include <melee/it/inlines.h>
#include <melee/it/it_2725.h>
#include <melee/it/item.h>
#include <melee/it/itgroundcoll.h>
#include <melee/it/itmaplib.h>
#include <melee/it/types.h>
#include <melee/lb/lb_00B0.h>
#include <melee/mp/forward.h>
#include <melee/mp/mplib.h>
#include <sysdolphin/baselib/aobj.h>
#include <sysdolphin/baselib/dobj.h>
#include <sysdolphin/baselib/jobj.h>
#include <sysdolphin/baselib/mobj.h>
#include <sysdolphin/baselib/tobj.h>

#include <string.h>

static inline itTails_ShotVars* itTs_ShotVars(Item* ip)
{
    return (itTails_ShotVars*) &ip->xDD4_itemVar;
}

static inline itTails_ShotAttrs* itTs_ShotAttrs(Item* ip)
{
    return (itTails_ShotAttrs*) DP(ip->xC4_article_data->x4_specialAttributes);
}

/* code+0x3A4 JOBJ_GetAnimFrame (an m-ex helper): the current frame of the first animation found
 * down the joint's first-child chain, looking at the joint, then each DObj, its MObj and TObjs.
 * -1 when nothing is animated. */
static float itTs_JObjGetAnimFrame(HSD_JObj* jobj)
{
    for (; jobj != NULL; jobj = jobj->child) {
        HSD_DObj* dobj;

        if (jobj->aobj != NULL) {
            return jobj->aobj->curr_frame;
        }
        for (dobj = jobj->u.dobj; dobj != NULL; dobj = dobj->next) {
            HSD_TObj* tobj;

            if (dobj->aobj != NULL) {
                return dobj->aobj->curr_frame;
            }
            if (dobj->mobj->aobj != NULL) {
                return dobj->mobj->aobj->curr_frame;
            }
            for (tobj = dobj->mobj->tobj; tobj != NULL; tobj = tobj->next) {
                if (tobj->aobj != NULL) {
                    return tobj->aobj->curr_frame;
                }
            }
        }
    }
    return -1.0f;
}

/* Change state keeping the animation where it was (ITEM_ANIM_UPDATE | ITEM_HIT_PRESERVE, then put
 * the frame back). */
static void itTs_Shot_ChangeStateKeepFrame(Item_GObj* gobj, enum_t msid)
{
    HSD_JObj* jobj = gobj->hsd_obj;
    float frame = itTs_JObjGetAnimFrame(jobj);

    Item_80268E5C(gobj, msid, ITEM_ANIM_UPDATE | ITEM_HIT_PRESERVE);
    HSD_JObjReqAnimAll(gobj->hsd_obj, frame);
    HSD_JObjAnimAll(gobj->hsd_obj);
}

/* code+0x33C SetMovePastLedge: slid off the ground, keep flying. */
static void itTs_Shot_SetMovePastLedge(Item_GObj* gobj)
{
    it_802762BC(GET_ITEM(gobj));
    itTs_Shot_ChangeStateKeepFrame(gobj, itTs_Shot_MS_Air);
}

/* code+0x2B8 Die_Enter */
static void itTs_Shot_Die_Enter(Item_GObj* gobj)
{
    itTails_ShotAttrs* attrs = itTs_ShotAttrs(GET_ITEM(gobj));

    Item_80268E5C(gobj, itTs_Shot_MS_Die, ITEM_ANIM_UPDATE);
    it_80275158(gobj, (float) attrs->die_lifetime);
}

/* code+0xDC Move_Anim */
static bool itTs_Shot_Move_Anim(Item_GObj* gobj)
{
    itTails_ShotVars* vars = itTs_ShotVars(GET_ITEM(gobj));

    if (vars->life > 0) {
        vars->life--;
        return false;
    }
    itTs_Shot_Die_Enter(gobj);
    return false;
}

/* code+0x124 Move_Phys */
static void itTs_Shot_Move_Phys(Item_GObj* gobj)
{
    Item* ip = GET_ITEM(gobj);

    if (ip->ground_or_air == GA_Air) {
        it_80272860(gobj, ip->xCC_item_attr->x10_fall_speed, ip->xCC_item_attr->x14_fall_speed_max);
    } else {
        ip->x40_vel.y = 0.0f;
    }
}

/* code+0x178 Move_Coll */
static bool itTs_Shot_Move_Coll(Item_GObj* gobj)
{
    Item* ip = GET_ITEM(gobj);

    if (ip->ground_or_air == GA_Ground) {
        it_8026D62C(gobj, itTs_Shot_SetMovePastLedge);
        return false;
    }
    if (it_8026DAA8(gobj) && (ip->x378_itemColl.env_flags & Collide_FloorMask)) {
        it_802762B0(ip);
        itTs_Shot_ChangeStateKeepFrame(gobj, itTs_Shot_MS_Ground);
    }
    return false;
}

/* code+0x224 Die_Anim */
static bool itTs_Shot_Die_Anim(Item_GObj* gobj)
{
    return it_80273130(gobj);
}

/* code+0x244 OnHitShieldDetermineDie: slow down; after enough bounces, fizzle. */
static bool itTs_Shot_OnHitShieldDetermineDie(Item_GObj* gobj)
{
    Item* ip = GET_ITEM(gobj);
    itTails_ShotAttrs* attrs = itTs_ShotAttrs(ip);
    itTails_ShotVars* vars = itTs_ShotVars(ip);

    ip->x40_vel.x *= attrs->shield_bounce_vel_mul;
    vars->bounces++;
    if (vars->bounces < attrs->max_shield_bounces) {
        return ip->xC54 == 0.0f;
    }
    itTs_Shot_Die_Enter(gobj);
    return false;
}

/* code+0x30 OnSpawn: empty on the disc. */
static void itTs_Shot_OnSpawn(Item_GObj* gobj) {}

/* code+0x34 OnDestroy: free the owner's one-shot slot. */
static void itTs_Shot_OnDestroy(Item_GObj* gobj)
{
    Fighter* owner = GET_FIGHTER(itTs_ShotVars(GET_ITEM(gobj))->owner);

    if (owner->kind == Ft_Kind_Kirby) {
        /* Kirby's copy of the move keeps its slot at console fp+0x596C, in m-ex's extended Kirby
         * ability area; that belongs to the Kirby copy-ability layer (not ported here). */
        return;
    }
    ftTs_Vars(owner)->shot_gobj = NULL;
}

/* code+0x64 OnGiveDamage: the shot survives hitting someone. */
static bool itTs_Shot_OnGiveDamage(Item_GObj* gobj)
{
    return false;
}

/* code+0x6C OnTakeDamage */
static bool itTs_Shot_OnTakeDamage(Item_GObj* gobj)
{
    return true;
}

/* code+0x74 OnReflect */
static bool itTs_Shot_OnReflect(Item_GObj* gobj)
{
    return it_80273030(gobj);
}

/* code+0x94 OnClank (m-ex "onunk1") */
static bool itTs_Shot_OnClank(Item_GObj* gobj)
{
    return true;
}

/* code+0x9C OnHitShieldBounce and code+0xBC OnHitShieldDetermineDestroy */
static bool itTs_Shot_OnHitShield(Item_GObj* gobj)
{
    return itTs_Shot_OnHitShieldDetermineDie(gobj);
}

/* code+0x0 item_state_table */
ItemStateTable itTs_Shot_StateTable[3] = {
    { itTs_Shot_MS_Ground, itTs_Shot_Move_Anim, itTs_Shot_Move_Phys, itTs_Shot_Move_Coll },
    { itTs_Shot_MS_Air, itTs_Shot_Move_Anim, itTs_Shot_Move_Phys, itTs_Shot_Move_Coll },
    { itTs_Shot_MS_Die, itTs_Shot_Die_Anim, itTs_Shot_Move_Phys, itTs_Shot_Move_Coll },
};

/* The article's m-ex itFunction table, in ItemLogicTable order. */
ItemLogicTable itTs_Shot_Logic = {
    itTs_Shot_StateTable,
    itTs_Shot_OnSpawn,
    itTs_Shot_OnDestroy,
    NULL,                               /* picked_up */
    NULL,                               /* dropped */
    NULL,                               /* thrown */
    itTs_Shot_OnGiveDamage,             /* dmg_dealt */
    itTs_Shot_OnTakeDamage,             /* dmg_received */
    NULL,                               /* entered_air */
    itTs_Shot_OnReflect,                /* reflected */
    itTs_Shot_OnClank,                  /* clanked */
    NULL,                               /* absorbed */
    itTs_Shot_OnHitShield,              /* shield_bounced */
    itTs_Shot_OnHitShield,              /* hit_shield */
    NULL,                               /* evt_unk */
};

/* code+0x6340 SpecialN_SpawnProjectile (fighter side) */
void ftTs_SpecialN_SpawnProjectile(Fighter_GObj* gobj)
{
    Fighter* fp = GET_FIGHTER(gobj);
    Vec3 pos;
    Vec3 wall;
    int line;
    SpawnItem spawn;
    Item_GObj* shot_gobj;

    lb_8000B1CC(fp->parts[ftParts_GetBoneIndex(fp, ftTs_Part_ShotSource)].joint, NULL, &pos);
    pos.z = 0.0f;

    /* Never spawn it through a wall: stop at the first surface between Tails and the hand. */
    if (mpCheckAllRemap(&wall, &line, NULL, NULL, -1, -1, fp->cur_pos.x, fp->cur_pos.y, pos.x,
                        pos.y))
    {
        pos = wall;
    }

    memset(&spawn, 0, sizeof(spawn));
    spawn.x0_parent_gobj = gobj;
    spawn.x4_parent_gobj2 = gobj;
    spawn.kind = mu_ak_article_kind(gobj, 0);
    spawn.pos = pos;
    spawn.prev_pos = pos;
    spawn.facing_dir = fp->facing_dir;

    shot_gobj = Item_80268B18(&spawn);
    if (shot_gobj != NULL) {
        Item* ip = GET_ITEM(shot_gobj);
        itTails_ShotAttrs* attrs;
        itTails_ShotVars* vars;
        HSD_JObj* jobj;

        db_80225DD8(shot_gobj, gobj);
        attrs = itTs_ShotAttrs(ip);
        vars = itTs_ShotVars(ip);
        vars->life = attrs->lifetime;
        vars->owner = gobj;
        vars->bounces = 0;
        ftTs_Vars(fp)->shot_gobj = shot_gobj;

        it_802762BC(ip);
        Item_80268E5C(shot_gobj, itTs_Shot_MS_Air, ITEM_ANIM_UPDATE);
        ip->x40_vel.x = ip->facing_dir * attrs->speed;
        ip->x40_vel.y = 0.0f;

        /* Mirror the model to the facing side. */
        jobj = shot_gobj->hsd_obj;
        jobj->scale.x = spawn.facing_dir;
        HSD_JObjSetMtxDirtySub(jobj);
    }
}
