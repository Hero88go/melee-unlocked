/* PlSh.dat article 0, offsets +0000..+04BC. The spring is registered by load but none of
 * this fighter's code creates it. Its callbacks are nevertheless translated in full. */
#include "shadow.h"

#include <melee/ft/ft_0881.h>
#include <melee/ft/inlines.h>
#include <melee/ft/kinds/ftCommon/ftCo_JumpAerial.h>
#include <melee/it/inlines.h>
#include <melee/it/it_2725.h>
#include <melee/it/item.h>
#include <melee/it/itgroundcoll.h>
#include <melee/it/itmaplib.h>
#include <sysdolphin/baselib/jobj.h>

typedef struct itSh_SpringAttrs {
    float lifetime;
    float bounce_vel;
    float min_bounce_vel;
    float bounce_loss;
    float rebound_lifetime;
    float rebound_spin;
    float rebound_vel;
    float rebound_scale;
} DISC_STRUCT itSh_SpringAttrs;

static itSh_SpringAttrs* itSh_Attrs(Item* ip)
{
    return (itSh_SpringAttrs*) DP(ip->xC4_article_data->x4_specialAttributes);
}
static itSh_SpringVars* itSh_Vars(Item* ip)
{
    return (itSh_SpringVars*) &ip->xDD4_itemVar;
}
static bool itSh_JumpedOn(HSD_GObj* gobj);
static void itSh_Rebound_Enter(HSD_GObj* gobj);

static void itSh_Idle_Enter(HSD_GObj* gobj)
{
    Item* ip = GET_ITEM(gobj);
    Item_80268E5C(gobj, 0, 2);
    it_802762B0(ip);
    ip->xDD0_flag.b0 = true;
    ip->jumped_on = itSh_JumpedOn;
    ip->x40_vel.x = ip->x40_vel.y = 0.0f;
}
static void itSh_Fall_Enter(HSD_GObj* gobj)
{
    GET_ITEM(gobj)->ground_or_air = GA_Air;
    Item_80268E5C(gobj, 1, 2);
}
static void itSh_Rebound_Enter(HSD_GObj* gobj)
{
    Item* ip = GET_ITEM(gobj);
    itSh_SpringAttrs* da = itSh_Attrs(ip);
    Item_80268E5C(gobj, 2, 2);
    ip->xD3C_spinSpeed = da->rebound_spin;
    ip->x40_vel.y = da->rebound_vel;
    it_80275158(gobj, da->rebound_lifetime);
}
static void itSh_OnSpawn(HSD_GObj* gobj)
{
    Item* ip = GET_ITEM(gobj);
    itSh_SpringAttrs* da = itSh_Attrs(ip);
    itSh_Vars(ip)->bounce_vel = da->bounce_vel;
    it_80275158(gobj, da->lifetime);
    ft_80088478(GET_FIGHTER(ip->owner), ftSh_Sfx_Spring, 0xFF, 0x40);
    if (ip->ground_or_air == GA_Ground) {
        itSh_Idle_Enter(gobj);
    } else {
        itSh_Fall_Enter(gobj);
    }
}
static void itSh_OnDestroy(HSD_GObj* gobj)
{
    HSD_GObj* owner = itSh_Vars(GET_ITEM(gobj))->owner;
    if (owner != NULL) {
        ftSh_FV(GET_FIGHTER(owner))->spring_gobj = NULL;
    }
}
static bool itSh_OnDamage(HSD_GObj* gobj)
{
    itColl_BounceOffVictim(gobj);
    return false;
}
static bool itSh_OnShield(HSD_GObj* gobj)
{
    itColl_BounceOffShield(gobj);
    return false;
}
static bool itSh_OnReflect(HSD_GObj* gobj) { return it_80273030(gobj); }
static bool itSh_True(HSD_GObj* gobj) { return true; }
static bool itSh_False(HSD_GObj* gobj) { return false; }
static void itSh_Nothing(HSD_GObj* gobj) {}
static bool itSh_Anim(HSD_GObj* gobj) { return it_80273130(gobj); }

static bool itSh_Idle_Coll(HSD_GObj* gobj)
{
    /* The console passes a null moving-ground callback; the same mod bug is retained. */
    if (!it_8026D8A4(gobj, NULL)) {
        itSh_Fall_Enter(gobj);
    }
    return false;
}
static void itSh_Fall_Phys(HSD_GObj* gobj)
{
    Item* ip = GET_ITEM(gobj);
    it_80272860(gobj, ip->xCC_item_attr->x10_fall_speed,
                ip->xCC_item_attr->x14_fall_speed_max);
    if (ip->x40_vel.y > 0.0f) {
        itSh_Rebound_Enter(gobj);
    }
}
static bool itSh_Fall_Coll(HSD_GObj* gobj)
{
    it_8026E15C(gobj, itSh_Rebound_Enter);
    return false;
}
static bool itSh_Rebound_Anim(HSD_GObj* gobj)
{
    itSh_SpringAttrs* da = itSh_Attrs(GET_ITEM(gobj));
    HSD_JObj* joint = it_80272CC0(gobj, 0);
    joint->scale.x = joint->scale.y = joint->scale.z = da->rebound_scale;
    return it_80273130(gobj);
}
static void itSh_Rebound_Phys(HSD_GObj* gobj)
{
    Item* ip = GET_ITEM(gobj);
    it_80272860(gobj, ip->xCC_item_attr->x10_fall_speed,
                ip->xCC_item_attr->x14_fall_speed_max);
}
static bool itSh_JumpedOn(HSD_GObj* gobj)
{
    Item* ip = GET_ITEM(gobj);
    HSD_GObj* jumper = ip->xCFC;
    Fighter* fp = GET_FIGHTER(jumper);
    itSh_SpringAttrs* da = itSh_Attrs(ip);
    itSh_SpringVars* mv = itSh_Vars(ip);
    float next;
    /* Unlike the other spring implementation, there is no grab-release work here. */
    ftCo_JumpAerial_Enter_Basic(jumper);
    fp->self_vel.y = mv->bounce_vel;
    next = mv->bounce_vel - da->bounce_loss;
    mv->bounce_vel = next;
    mv->bounce_vel = next < da->min_bounce_vel ? da->min_bounce_vel : next;
    itSh_Idle_Enter(gobj);
    ft_80088478(GET_FIGHTER(ip->owner), ftSh_Sfx_Spring, 0xFF, 0x40);
    return false;
}

static ItemStateTable itSh_States[] = {
    { 1, itSh_Anim, itSh_Nothing, itSh_Idle_Coll },
    { 2, itSh_Anim, itSh_Fall_Phys, itSh_Fall_Coll },
    { 3, itSh_Rebound_Anim, itSh_Rebound_Phys, itSh_False },
};
const ItemLogicTable itSh_Spring_LogicTable = {
    .states = itSh_States, .spawned = itSh_OnSpawn, .destroyed = itSh_OnDestroy,
    .dmg_dealt = itSh_OnDamage, .dmg_received = itSh_True, .reflected = itSh_OnReflect,
    .shield_bounced = itSh_OnShield, .hit_shield = itSh_True,
};
