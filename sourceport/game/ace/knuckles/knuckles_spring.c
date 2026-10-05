/* Knuckles native port working copy; PlKx.dat comparison review in progress. */
/* Akaneia's Sonic: the spring article left by the up special (itFunction in PlSn.dat).
 *
 * States: Idle (standing on ground, bounces anyone who jumps on it), Fall, Rebound (knocked
 * off: spins, shrinks and falls through the stage until its short lifetime runs out). Each
 * bounce is weaker than the last down to a floor. Special attributes (article x4, big-endian):
 *   0x00 lifetime 100, 0x04 first bounce speed 2, 0x08 min bounce speed 0.5,
 *   0x0C bounce speed lost per use 1, 0x10 rebound lifetime 30, 0x14 rebound spin -100,
 *   0x18 rebound launch y 1, 0x1C rebound scale 0.8.
 * Hand-written from PlSn.dat's itFunction code; see NOTES.md. */
#include "knuckles.h"

#include <melee/ft/fighter.h>
#include <melee/ft/ft_0881.h>
#include <melee/ft/ftcommon.h>
#include <melee/ft/inlines.h>
#include <melee/ft/kinds/ftCommon/ftCo_JumpAerial.h>
#include <melee/ft/kinds/ftCommon/ftCo_Throw.h>
#include <melee/ft/types.h>
#include <melee/it/inlines.h>
#include <melee/it/it_2725.h>
#include <melee/it/item.h>
#include <melee/it/itgroundcoll.h>
#include <melee/it/itmaplib.h>
#include <melee/it/types.h>
#include <sysdolphin/baselib/jobj.h>

typedef struct ftKnuckles_SpringAttrs {
    /* 0x00 */ float lifetime;
    /* 0x04 */ float bounce_vel;
    /* 0x08 */ float min_bounce_vel;
    /* 0x0C */ float bounce_vel_loss;
    /* 0x10 */ float rebound_lifetime;
    /* 0x14 */ float rebound_spin;
    /* 0x18 */ float rebound_vel_y;
    /* 0x1C */ float rebound_scale;
} DISC_STRUCT ftKnuckles_SpringAttrs;

enum {
    ftKx_Spring_MS_Idle,
    ftKx_Spring_MS_Fall,
    ftKx_Spring_MS_Rebound,
};

static inline ftKnuckles_SpringAttrs* ftKx_SpringAttrs(Item* ip)
{
    return (ftKnuckles_SpringAttrs*) DP(ip->xC4_article_data->x4_specialAttributes);
}

/* The Sonic that placed it. m-ex keeps this in its item extension (console item+FCC); the
 * native item's owner is the same gobj (Spawn_Spring passes Sonic as both parents). */
static inline Fighter* ftKx_SpringOwner(Item* ip)
{
    return GET_FIGHTER(ip->owner);
}

static bool ftKx_Spring_JumpedOn(HSD_GObj* gobj);
static void ftKx_Spring_Rebound_Enter(HSD_GObj* gobj);

/* ---- enter ---------------------------------------------------------------------------------- */

static void ftKx_Spring_Idle_Enter(HSD_GObj* gobj)
{
    Item* ip = GET_ITEM(gobj);

    Item_80268E5C(gobj, ftKx_Spring_MS_Idle, ITEM_ANIM_UPDATE);
    it_802762B0(ip);
    ip->xDD0_flag.b0 = true;
    ip->jumped_on = ftKx_Spring_JumpedOn;
    ip->x40_vel.x = 0.0F;
    ip->x40_vel.y = 0.0F;
}

static void ftKx_Spring_Fall_Enter(HSD_GObj* gobj)
{
    Item* ip = GET_ITEM(gobj);

    ip->ground_or_air = GA_Air;
    Item_80268E5C(gobj, ftKx_Spring_MS_Fall, ITEM_ANIM_UPDATE);
}

static void ftKx_Spring_Rebound_Enter(HSD_GObj* gobj)
{
    Item* ip = GET_ITEM(gobj);
    ftKnuckles_SpringAttrs* sa = ftKx_SpringAttrs(ip);

    Item_80268E5C(gobj, ftKx_Spring_MS_Rebound, ITEM_ANIM_UPDATE);
    ip->xD3C_spinSpeed = sa->rebound_spin;
    ip->x40_vel.y = sa->rebound_vel_y;
    it_80275158(gobj, sa->rebound_lifetime);
}

/* ---- item logic ----------------------------------------------------------------------------- */

/* OnSpawn */
static void ftKx_Spring_OnSpawn(HSD_GObj* gobj)
{
    Item* ip = GET_ITEM(gobj);
    ftKnuckles_SpringAttrs* sa = ftKx_SpringAttrs(ip);

    ftKx_SpringVars(ip)->bounce_vel = sa->bounce_vel;
    it_80275158(gobj, sa->lifetime);
    ft_80088478(ftKx_SpringOwner(ip), ftKx_Sfx_Spring, 0xFF, 0x40);
    if (ip->ground_or_air == GA_Ground) {
        ftKx_Spring_Idle_Enter(gobj);
    } else {
        ftKx_Spring_Fall_Enter(gobj);
    }
}

/* OnDestroy: forget ourselves in the owner. */
static void ftKx_Spring_OnDestroy(HSD_GObj* gobj)
{
    Item* ip = GET_ITEM(gobj);
    HSD_GObj* owner = ftKx_SpringVars(ip)->owner;

    if (owner != NULL) {
        ftKx_FV(GET_FIGHTER(owner))->spring_gobj = NULL;
    }
}

/* OnGiveDamage */
static bool ftKx_Spring_OnGiveDamage(HSD_GObj* gobj)
{
    itColl_BounceOffVictim(gobj);
    return false;
}

/* OnTakeDamage */
static bool ftKx_Spring_OnTakeDamage(HSD_GObj* gobj)
{
    return true;
}

/* OnReflect */
static bool ftKx_Spring_OnReflect(HSD_GObj* gobj)
{
    return it_80273030(gobj);
}

/* OnHitShieldBounce */
static bool ftKx_Spring_OnHitShieldBounce(HSD_GObj* gobj)
{
    itColl_BounceOffShield(gobj);
    return false;
}

/* OnHitShieldDetermineDestroy */
static bool ftKx_Spring_OnHitShieldDetermineDestroy(HSD_GObj* gobj)
{
    return true;
}

/* Spring_JumpedOn: launch the jumper (breaking any grab he is in) with the stored speed, then
 * weaken the spring. */
static bool ftKx_Spring_JumpedOn(HSD_GObj* gobj)
{
    Item* ip = GET_ITEM(gobj);
    HSD_GObj* jumper = ip->xCFC;
    Fighter* jfp = GET_FIGHTER(jumper);
    ftKnuckles_SpringAttrs* sa = ftKx_SpringAttrs(ip);
    ftKnuckles_SpringVars* sv = ftKx_SpringVars(ip);
    HSD_GObj* partner = jfp->x1A5C;
    float next;

    if (partner != NULL) {
        ftCommon_8007DB58(partner);
        ftCo_800DCFD4(partner);
        ftCo_800DC920(jumper, partner);
    }
    ftCommon_8007DB58(jumper);
    ftCo_JumpAerial_Enter_Basic(jumper);
    jfp->self_vel.y = sv->bounce_vel;

    next = sv->bounce_vel - sa->bounce_vel_loss;
    sv->bounce_vel = next;
    sv->bounce_vel = next < sa->min_bounce_vel ? sa->min_bounce_vel : next;

    ftKx_Spring_Idle_Enter(gobj);
    ft_80088478(ftKx_SpringOwner(ip), ftKx_Sfx_Spring, 0xFF, 0x40);
    return false;
}

/* ---- states --------------------------------------------------------------------------------- */

static bool ftKx_Spring_Idle_Anim(HSD_GObj* gobj)
{
    return it_80273130(gobj);
}

static void ftKx_Spring_Idle_Phys(HSD_GObj* gobj) {}

/* it_8026D8A4 calls its callback when the ground under the item moves it; m-ex passes NULL
 * there (which would crash on console if that ever fired). A no-op keeps the same behavior
 * everywhere else. */
static void ftKx_Spring_Nothing(HSD_GObj* gobj) {}

static bool ftKx_Spring_Idle_Coll(HSD_GObj* gobj)
{
    if (!it_8026D8A4(gobj, ftKx_Spring_Nothing)) {
        ftKx_Spring_Fall_Enter(gobj);
    }
    return false;
}

static bool ftKx_Spring_Fall_Anim(HSD_GObj* gobj)
{
    return it_80273130(gobj);
}

/* Fall_Phys: gravity; if something sent it upward, it rebounds. */
static void ftKx_Spring_Fall_Phys(HSD_GObj* gobj)
{
    Item* ip = GET_ITEM(gobj);
    ItemAttr* attr = ip->xCC_item_attr;

    it_80272860(gobj, attr->x10_fall_speed, attr->x14_fall_speed_max);
    if (ip->x40_vel.y > 0.0F) {
        ftKx_Spring_Rebound_Enter(gobj);
    }
}

static bool ftKx_Spring_Fall_Coll(HSD_GObj* gobj)
{
    it_8026E15C(gobj, ftKx_Spring_Rebound_Enter);
    return false;
}

static bool ftKx_Spring_Rebound_Anim(HSD_GObj* gobj)
{
    Item* ip = GET_ITEM(gobj);
    ftKnuckles_SpringAttrs* sa = ftKx_SpringAttrs(ip);
    HSD_JObj* jobj = it_80272CC0(gobj, 0);

    jobj->scale.x = sa->rebound_scale;
    jobj->scale.y = sa->rebound_scale;
    jobj->scale.z = sa->rebound_scale;
    return it_80273130(gobj);
}

static void ftKx_Spring_Rebound_Phys(HSD_GObj* gobj)
{
    Item* ip = GET_ITEM(gobj);
    ItemAttr* attr = ip->xCC_item_attr;

    it_80272860(gobj, attr->x10_fall_speed, attr->x14_fall_speed_max);
}

static bool ftKx_Spring_Rebound_Coll(HSD_GObj* gobj)
{
    return false;
}

/* ---- tables --------------------------------------------------------------------------------- */

ItemStateTable ftKx_Spring_StateTable[3] = {
    { 1, ftKx_Spring_Idle_Anim, ftKx_Spring_Idle_Phys, ftKx_Spring_Idle_Coll },
    { 2, ftKx_Spring_Fall_Anim, ftKx_Spring_Fall_Phys, ftKx_Spring_Fall_Coll },
    { 3, ftKx_Spring_Rebound_Anim, ftKx_Spring_Rebound_Phys, ftKx_Spring_Rebound_Coll },
};

/* m-ex itFunction exports, in ItemLogicTable order (slots m-ex leaves empty are NULL). */
const ItemLogicTable ftKx_Spring_LogicTable = {
    ftKx_Spring_StateTable,
    ftKx_Spring_OnSpawn,
    ftKx_Spring_OnDestroy,
    NULL,                                   /* onpickup */
    NULL,                                   /* ondrop */
    NULL,                                   /* onthrow */
    ftKx_Spring_OnGiveDamage,
    ftKx_Spring_OnTakeDamage,
    NULL,                                   /* onenterair */
    ftKx_Spring_OnReflect,
    NULL,                                   /* onunk1 (clank) */
    NULL,                                   /* onunk2 (absorb) */
    ftKx_Spring_OnHitShieldBounce,
    ftKx_Spring_OnHitShieldDetermineDestroy,
    NULL,                                   /* onunk3 */
};
