/* King Dedede (Akaneia), native: the articles' item code.
 *
 * PlDe.dat "itFunction" holds one m-ex function block per article (index = ftDataDedede->x48_items
 * index). Article 0 (the star model a spat fighter rides in) has none; 1 is the item spit star,
 * 2 the Gordo, 3 the Up B landing star. Each becomes an ItemLogicTable here, with its motion state
 * table. The itFunction slots map to the table in order: item_state_table = states, onspawn =
 * spawned, ondestroy = destroyed, onpickup, ondrop, onthrow, ongivedamage = dmg_dealt,
 * ontakedamage = dmg_received, onenterair, onreflect = reflected, onunk1 = clanked, onunk2 =
 * absorbed, onhitshieldbounce = shield_bounced, onhitshielddeterminedestroy = hit_shield,
 * onunk3 = evt_unk.
 *
 * Source: PlDe.dat itFunction[1..3], rewritten by hand. */
#include "ftDe.h"

#include <melee/ft/inlines.h>
#include <melee/it/inlines.h>
#include <melee/it/it_26B1.h>
#include <melee/it/it_2725.h>
#include <melee/it/it_3F14.h>
#include <melee/it/itcoll.h>
#include <melee/it/itgroundcoll.h>
#include <melee/it/itmaplib.h>
#include <melee/it/item.h>
#include <MSL/math.h>
#include <sysdolphin/baselib/gobj.h>
#include <sysdolphin/baselib/jobj.h>

/* OnUnk3 of every article */
static void itDe_OnUnk3(HSD_GObj* gobj, HSD_GObj* ref_gobj)
{
    it_8026B894(gobj, ref_gobj);
}

static bool itDe_Anim_Common(HSD_GObj* gobj)
{
    return it_80273130(gobj);
}

static bool itDe_ReturnFalse(HSD_GObj* gobj)
{
    return false;
}

static void itDe_NoPhys(HSD_GObj* gobj) {}

/* ---------------------------------------------------------------- article 1: spit star */

/* State0_PhysCB: slows down by `decel` per frame; below that it stops sideways. */
static void itDe_SpitStar_Phys(HSD_GObj* gobj)
{
    Item* ip = GET_ITEM(gobj);
    float decel = ((itDe_SpitStarVars*) &ip->xDD4_itemVar)->decel;
    float vx = ip->x40_vel.x;
    float vy = ip->x40_vel.y;
    float speed = sqrtf(vx * vx + vy * vy);
    if (decel < speed) {
        float left = speed - decel;
        ip->x40_vel.x = left * vx / speed;
        ip->x40_vel.y = left * vy / speed;
    } else {
        ip->x40_vel.x = 0.0f;
    }
}

/* State0_CollCB */
static bool itDe_SpitStar_Coll(HSD_GObj* gobj)
{
    return it_8026DFB0(gobj) != false;
}

static ItemStateTable itDe_SpitStar_States[] = {
    { 0, itDe_Anim_Common, itDe_SpitStar_Phys, itDe_SpitStar_Coll },
    { -1, itDe_ReturnFalse, itDe_NoPhys, itDe_ReturnFalse },
    { -1, itDe_ReturnFalse, itDe_NoPhys, itDe_ReturnFalse },
};

static ItemLogicTable itDe_SpitStar_Logic = {
    .states = itDe_SpitStar_States,
    .evt_unk = itDe_OnUnk3,
};

/* ---------------------------------------------------------------- article 2: Gordo */

#define ITDE_GORDO_ROT 0x1.921ff2p+0f /* 0x3FC90FF9, just over pi/2 */

static inline itDe_GordoAttrs* Gordo_Attrs(Item* ip)
{
    return DP(ip->xC4_article_data->x4_specialAttributes);
}

static inline itDe_GordoVars* Gordo_Vars(Item* ip)
{
    return (itDe_GordoVars*) &ip->xDD4_itemVar;
}

/* StepValue: move `value` toward `target` by `step` without passing it. */
static float Gordo_Step(float value, float target, float step)
{
    if (value < target) {
        value += step;
        if (value > target) {
            value = target;
        }
    } else if (value > target) {
        value -= step;
        if (value < target) {
            value = target;
        }
    }
    return value;
}

/* Gordo_OnDestroy: tell the Dedede that spawned it a new one may be pulled. */
static void Gordo_ReleaseOwner(HSD_GObj* gobj)
{
    Item* ip = GET_ITEM(gobj);
    HSD_GObj* dedede_gobj;
    if (ip == NULL) {
        return;
    }
    dedede_gobj = (HSD_GObj*) MU_Z(Gordo_Vars(ip)->dedede);
    if (dedede_gobj != NULL) {
        ftDe_SpecialS_ClearHeldGordo(dedede_gobj);
    }
}

/* OnDestroy */
static void itDe_Gordo_OnDestroy(HSD_GObj* gobj)
{
    Item* ip = GET_ITEM(gobj);
    it_802725D4(gobj);
    Gordo_ReleaseOwner(gobj);
    ip->xDC8_word.flags.x13 = false;
}

/* OnGiveDamage: bounce back off whoever it hit, weaker and with less time left. */
static bool itDe_Gordo_OnGiveDamage(HSD_GObj* gobj)
{
    Item* ip = GET_ITEM(gobj);
    itDe_GordoAttrs* attrs = Gordo_Attrs(ip);
    HitCapsule* hit = &ip->x5D4_hitboxes[0].hit;
    float vy;

    ip->x40_vel.x *= -attrs->rebound_x;
    vy = ip->x40_vel.y;
    if (vy > 0.0f || vy != vy) {
        ip->x40_vel.y = vy * attrs->rebound_y;
    } else {
        ip->x40_vel.y = -attrs->rebound_y * vy;
    }
    it_80272460(hit, (s32) ((float) (s32) hit->unk_count * attrs->rebound_damage_scale), gobj);
    it_80274658(gobj, 0.0f);
    ip->xD44_lifeTimer -= 50.0f;
    it_80273130(gobj);
    ip->facing_dir = -ip->facing_dir;
    return false;
}

/* OnTakeDamage: a hit of at least hit_min_damage knocks it back the other way, harder for
 * stronger hits, and gives it more time. */
static bool itDe_Gordo_OnTakeDamage(HSD_GObj* gobj)
{
    Item* ip = GET_ITEM(gobj);
    itDe_GordoAttrs* attrs = Gordo_Attrs(ip);
    HitCapsule* hit = &ip->x5D4_hitboxes[0].hit;
    float life, bonus, taken;
    double k;
    s32 damage;

    if (ip->xCA0 < attrs->hit_min_damage) {
        return false;
    }
    life = ip->xD44_lifeTimer;
    bonus = attrs->hit_lifetime_bonus;
    it_8027236C(gobj);

    taken = (float) ip->xCA0;
    k = (double) taken * 0.2;
    ip->x40_vel.x = -(float) ((double) ip->xCCC_incDamageDirection * k);
    ip->x40_vel.y = (float) k;
    damage = (s32) (taken * attrs->hit_damage_scale * ip->x40_vel.x);
    if (damage < 0) {
        damage = -damage;
    }
    it_80272460(hit, damage + (s32) hit->unk_count, gobj);
    it_80275158(gobj, life + bonus);
    ip->facing_dir = -ip->facing_dir;
    it_80274658(gobj, 0.0f);
    return false;
}

static bool itDe_Gordo_OnReflect(HSD_GObj* gobj)
{
    it_80273030(gobj);
    return false;
}

static bool itDe_Gordo_OnHitShieldBounce(HSD_GObj* gobj)
{
    itColl_BounceOffShield(gobj);
    return true;
}

static bool itDe_Gordo_OnHitShieldDetermineDestroy(HSD_GObj* gobj)
{
    return true;
}

/* GordoSpawn_AnimCB (state 0, held): face the model the way Dedede faces. */
static bool itDe_Gordo_Held_Anim(HSD_GObj* gobj)
{
    Item* ip = GET_ITEM(gobj);
    HSD_JObj* model = HSD_JObjGetNext(HSD_JObjGetChild(GET_JOBJ(gobj)));
    HSD_JObjSetRotationY(model, ip->facing_dir == 1.0f ? ITDE_GORDO_ROT : -ITDE_GORDO_ROT);
    return false;
}

/* State0_PhysCB (state 1, thrown): fall, slow toward cruise_speed, spin with the speed. */
static void itDe_Gordo_Thrown_Phys(HSD_GObj* gobj)
{
    Item* ip = GET_ITEM(gobj);
    itDe_GordoAttrs* attrs = Gordo_Attrs(ip);
    ItemAttr* item_attrs = ip->xCC_item_attr;
    float vx, spin, sign;

    it_80272860(gobj, item_attrs->x10_fall_speed, item_attrs->x14_fall_speed_max);

    vx = ip->x40_vel.x;
    vx = Gordo_Step(vx, attrs->cruise_speed * (vx >= 0.0f ? 1.0f : -1.0f), Gordo_Vars(ip)->slow);
    ip->x40_vel.x = vx;

    spin = (float) ((double) -item_attrs->xC_spin_speed * 6.283185307179586 / 60.0 * vx * 57.29578f);
    sign = spin >= 0.0f ? 1.0f : -1.0f;
    if (spin < 0.0f) {
        spin = -spin;
    }
    if (!(attrs->spin_min <= spin)) {
        spin = attrs->spin_min;
    }
    if (!(attrs->spin_max >= spin)) {
        spin = attrs->spin_max;
    }
    it_80274658(gobj, spin * sign);
}

/* State0_CollCB (state 1): bounce on landing; down and up tosses keep part of their bounce. */
static bool itDe_Gordo_Thrown_Coll(HSD_GObj* gobj)
{
    Item* ip = GET_ITEM(gobj);
    itDe_GordoAttrs* attrs = Gordo_Attrs(ip);
    float vy;

    it_8026D9A0(gobj);
    if (!it_8027781C(gobj)) {
        return false;
    }

    vy = ip->x40_vel.y;
    if (Gordo_Vars(ip)->toss == 1) {
        if (vy >= 0.0f) {
            ip->x40_vel.y = attrs->down_bounce * vy;
        }
    } else if (Gordo_Vars(ip)->toss == 2) {
        if (vy >= 0.0f) {
            ip->x40_vel.y = attrs->up_bounce * vy;
        }
    }

    ip->facing_dir = ip->x40_vel.x <= 0.0f ? -1.0f : 1.0f;
    vy = ip->x40_vel.y;
    if (vy >= 0.0f && vy < 2.0f) {
        ip->x40_vel.y = 2.0f;
    }
    it_8027321C(gobj);
    return false;
}

/* StateDeath_PhysCB (state 2) */
static void itDe_Gordo_Death_Phys(HSD_GObj* gobj)
{
    ItemAttr* item_attrs = GET_ITEM(gobj)->xCC_item_attr;
    it_80272860(gobj, item_attrs->x10_fall_speed, item_attrs->x14_fall_speed_max);
}

static ItemStateTable itDe_Gordo_States[] = {
    { 0, itDe_Gordo_Held_Anim, NULL, NULL },
    { 1, itDe_Anim_Common, itDe_Gordo_Thrown_Phys, itDe_Gordo_Thrown_Coll },
    { 0, itDe_Anim_Common, itDe_Gordo_Death_Phys, itDe_ReturnFalse },
};

static ItemLogicTable itDe_Gordo_Logic = {
    .states = itDe_Gordo_States,
    .destroyed = itDe_Gordo_OnDestroy,
    .dmg_dealt = itDe_Gordo_OnGiveDamage,
    .dmg_received = itDe_Gordo_OnTakeDamage,
    .reflected = itDe_Gordo_OnReflect,
    .shield_bounced = itDe_Gordo_OnHitShieldBounce,
    .hit_shield = itDe_Gordo_OnHitShieldDetermineDestroy,
    .evt_unk = itDe_OnUnk3,
};

/* ---------------------------------------------------------------- article 3: Up B landing star */

/* State0_PhysCB: ordinary falling item physics. */
static void itDe_HiStar_Phys(HSD_GObj* gobj)
{
    Item_ApplyFallingPhysics(gobj);
}

static bool itDe_HiStar_OnReflect(HSD_GObj* gobj)
{
    it_80273030(gobj);
    return false;
}

static bool itDe_HiStar_OnShield(HSD_GObj* gobj)
{
    itColl_BounceOffShield(gobj);
    return false;
}

static ItemStateTable itDe_HiStar_States[] = {
    { 0, itDe_Anim_Common, itDe_HiStar_Phys, itDe_ReturnFalse },
};

static ItemLogicTable itDe_HiStar_Logic = {
    .states = itDe_HiStar_States,
    .dmg_dealt = itDe_ReturnFalse,
    .reflected = itDe_HiStar_OnReflect,
    .shield_bounced = itDe_HiStar_OnShield,
    .hit_shield = itDe_HiStar_OnShield,
    .evt_unk = itDe_OnUnk3,
};

/* ---------------------------------------------------------------- the table */

ItemLogicTable* const ftDe_ArticleLogic[ftDe_Article_Count] = {
    NULL,
    &itDe_SpitStar_Logic,
    &itDe_Gordo_Logic,
    &itDe_HiStar_Logic,
};
