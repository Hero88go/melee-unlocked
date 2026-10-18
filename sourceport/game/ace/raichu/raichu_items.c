/* PlRc.dat's two itFunction blocks and ftFunction +21FC, +2BC8.
 * Article 2 has no block and no registration in onload. */
#include "raichu.h"
#include <dolphin/os.h>
#include <math.h>
#include <melee/db/db.h>
#include <melee/ft/ftlib.h>
#include <melee/ft/inlines.h>
#include <melee/it/inlines.h>
#include <melee/it/item.h>
#include <melee/it/it_2725.h>
#include <melee/it/it_3F14.h>
#include <melee/it/itgroundcoll.h>
#include <melee/it/kinds/types.h>
#include <melee/lb/lbvector.h>
#include <melee/mp/mpcoll.h>

typedef struct itRc_JoltAttrs {
    float speed, angle, lifetime, xC, x10;
} DISC_STRUCT itRc_JoltAttrs;

/* ft +2BC8. The item is initially spawned airborne on both paths. */
static void itRc_Jolt_Init(Item_GObj* gobj, GroundOrAir air)
{
    Item* ip = GET_ITEM(gobj);
    itRc_JoltAttrs* at = DP(ip->xC4_article_data->x4_specialAttributes);
    if (air == GA_Air) {
        ip->x40_vel.x = (cosf(at->angle) * at->speed) * ip->facing_dir;
        ip->x40_vel.y = sinf(at->angle) * at->speed;
        ip->x40_vel.z = 0;
    } else {
        ip->x40_vel.x = ip->facing_dir * at->speed;
        ip->x40_vel.y = ip->x40_vel.z = 0;
    }
    it_80275158(gobj, at->lifetime);
    Item_80268E5C(gobj, 0, 2);
}

/* ft +21FC. The supplied point becomes prev_pos, only x and y are copied. */
Item_GObj* itRc_Jolt_Spawn(HSD_GObj* owner, Vec3* pos, ItemKind kind,
                          GroundOrAir air, float facing)
{
    SpawnItem spawn = { 0 };
    Item_GObj* result;
    ftLib_80086990(owner, &spawn.pos);
    spawn.x0_parent_gobj = spawn.x4_parent_gobj2 = owner;
    spawn.kind = kind;
    spawn.prev_pos.x = pos->x;
    spawn.prev_pos.y = pos->y;
    spawn.facing_dir = facing;
    spawn.x44_flag.b0 = true;
    result = Item_80268B18(&spawn);
    /* The mod assumes creation succeeds. */
    itRc_Jolt_Init(result, air);
    {
        Item* ip = GET_ITEM(result);
        OSReport("[rc-jolt] pos %.9g %.9g prev %.9g %.9g vel %.9g %.9g air %d\n",
                 ip->pos.x, ip->pos.y, spawn.prev_pos.x, spawn.prev_pos.y,
                 ip->x40_vel.x, ip->x40_vel.y, (int) air);
    }
    db_80225DD8(result, owner);
    it_802750F8(result);
    return result;
}

static bool itRc_False(Item_GObj* gobj) { (void) gobj; return false; }
static bool itRc_True(Item_GObj* gobj) { (void) gobj; return true; }

/* it0 +10. */
static void itRc_Article0_Destroyed(Item_GObj* gobj)
{
    Fighter* owner = GET_FIGHTER(GET_ITEM(gobj)->owner);
    owner->mv.pk.speciallw.x4 = 0;
}

/* it0 +50 and it1 +98. */
static void itRc_Item_Phys(Item_GObj* gobj)
{
    it_80274658(gobj, it_804D6D28->x68_float);
}

/* it0 +BC. The local reflected velocity and both accumulators are discarded
 * by the console code. Preserve this mod bug: do not assign ip->x40_vel. */
static bool itRc_Article0_CheckFloor(Item_GObj* gobj)
{
    Item* ip = GET_ITEM(gobj);
    Vec3 zero = { 0 }, sum = { 0 }, velocity = ip->x40_vel;
    Vec3* normal = &ip->x378_itemColl.floor.normal;
    if ((ip->x378_itemColl.env_flags & 0x18000) &&
        __builtin_fmaf(velocity.x, normal->x, velocity.y * normal->y) < 0.0f) {
        lbVector_Mirror(&velocity, normal);
        lbVector_Add_xy(&sum, &velocity);
        if (mpCollGetSpeedFloor(&ip->x378_itemColl, &velocity)) {
            lbVector_Add_xy(&zero, &velocity);
        }
        return it_80273130(gobj) != 0;
    }
    return false;
}

/* it0 +80. */
static bool itRc_Article0_Coll(Item_GObj* gobj)
{
    it_8026D9A0(gobj);
    return itRc_Article0_CheckFloor(gobj) != 0;
}

/* it1 +70. */
static bool itRc_Jolt_Anim(Item_GObj* gobj)
{
    return it_80273130(gobj) != 0;
}

/* it1 +18 and +44. */
static bool itRc_Jolt_Reflected(Item_GObj* gobj)
{
    it_80273030(gobj);
    return false;
}
static bool itRc_Jolt_ShieldBounced(Item_GObj* gobj)
{
    itColl_BounceOffShield(gobj);
    return false;
}

/* it1 +F8. r3 initially holds env_flags & 0x3F. Any attempted reflection
 * leaves it nonzero, even though no velocity is written back. */
bool itRc_Jolt_CheckSurfacesFrom(Item_GObj* gobj, bool result)
{
    Item* ip = GET_ITEM(gobj);
    CollData* cd = &ip->x378_itemColl;
    Vec3 sum = { 0 }, speed_sum = { 0 };
    const u32 masks[] = { 0x3F, 0xFC0, 0x6000, 0x18000 };
    SurfaceData* surfaces[] = { &cd->left_facing_wall, &cd->right_facing_wall,
                                &cd->ceiling, &cd->floor };
    bool (*speeds[])(CollData*, Vec3*) = { mpCollGetSpeedLeftWall, mpCollGetSpeedRightWall,
                                         mpCollGetSpeedCeiling, mpCollGetSpeedFloor };
    for (int i = 0; i < 4; i++) {
        Vec3 velocity = ip->x40_vel;
        Vec3* normal = &surfaces[i]->normal;
        if ((cd->env_flags & masks[i]) &&
            __builtin_fmaf(velocity.x, normal->x, velocity.y * normal->y) < 0.0f) {
            lbVector_Mirror(&velocity, normal);
            lbVector_Add_xy(&sum, &velocity);
            if (speeds[i](cd, &velocity)) {
                lbVector_Add_xy(&speed_sum, &velocity);
            }
            result = true;
        }
    }
    return result;
}

/* it1 +C8. */
static bool itRc_Jolt_Coll(Item_GObj* gobj)
{
    it_8026D9A0(gobj);
    return itRc_Jolt_CheckSurfacesFrom(gobj,
                                      (GET_ITEM(gobj)->x378_itemColl.env_flags & 0x3F) != 0);
}

static ItemStateTable itRc_Article0_States[] = {
    { 0, itRc_False, itRc_Item_Phys, itRc_Article0_Coll },
};
static ItemStateTable itRc_Jolt_States[] = {
    { 0, itRc_Jolt_Anim, itRc_Item_Phys, itRc_Jolt_Coll },
};

const ItemLogicTable itRc_Articles[2] = {
    { .states = itRc_Article0_States, .destroyed = itRc_Article0_Destroyed,
      .dmg_dealt = itRc_False, .reflected = itRc_False,
      .shield_bounced = itRc_False, .hit_shield = itRc_True },
    { .states = itRc_Jolt_States, .dmg_dealt = itRc_True,
      .reflected = itRc_Jolt_Reflected, .clanked = itRc_True,
      .shield_bounced = itRc_Jolt_ShieldBounced, .hit_shield = itRc_True },
};
