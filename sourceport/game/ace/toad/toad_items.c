/* ACE's Toad: his article 0, the neutral special's projectile (m-ex "itFunction" of PlTd.dat,
 * one block of 0x198 bytes; the spawn and the init are in the fighter's block).
 *
 * m-ex builds an article's logic table from the defaults MxDt.dat holds for the item kind and
 * the fighter file's itFunction exports. For item kinds 354, 355 and 356 MxDt has no default
 * (run-source/rel09-ace-native/toad/mxdt.txt), so the table of 354 is the five exports: the
 * state table, dmg_dealt, reflected, shield_bounced and hit_shield.
 *
 * It is Mario's fireball (it/kinds/itmariofireball.c) with these differences: no effect and no
 * sound when it bounces off a surface, no clank and no absorb callback, "reflected" and
 * "shield_bounced" return 0 whatever the common routine returns. Its numbers are its own
 * (PlTd.dat: speed 1.75, angle -0.698 radians, 37 frames, least speed 0.85). Close to ACE's
 * Metal Mario's article, whose block differs in the clank slot, the shield result and a sound. */
#include "toad.h"

#include <dolphin/os.h>

#include <math.h>

#include <melee/db/db.h>
#include <melee/ft/inlines.h>
#include <melee/ft/types.h>
#include <melee/it/inlines.h>
#include <melee/it/it_2725.h>
#include <melee/it/it_3F14.h>
#include <melee/it/itCommonItems.h>
#include <melee/it/item.h>
#include <melee/it/itgroundcoll.h>
#include <melee/it/itmaplib.h>
#include <melee/it/kinds/inlines.h>
#include <melee/it/types.h>
#include <sysdolphin/baselib/gobj.h>

/* The article's special attributes, read through the type Mario's fireball uses: speed, angle,
 * lifetime, an unread word and the least speed. */
static inline itUnkAttributes* itTd_Shot_Attrs(Item* ip)
{
    return (itUnkAttributes*) DP(ip->xC4_article_data->x4_specialAttributes);
}

/* +1D0C of the fighter block, called once from the spawn. Same as retail it_8029B7C0:
 * (cos(angle) * speed) * facing and sin(angle) * speed, then the lifetime and the state. */
static void itTd_Shot_Init(Item_GObj* gobj)
{
    Item* ip = GET_ITEM(gobj);
    itUnkAttributes* attrs = itTd_Shot_Attrs(ip);
    float angle = attrs->x4_float;
    float speed = attrs->x0_float;
    float forward = cosf(angle) * speed;

    ip->x40_vel.x = forward * ip->facing_dir;
    ip->x40_vel.y = sinf(angle) * speed;
    ip->x40_vel.z = 0.0f;

    it_80275158(gobj, attrs->x8);
    Item_80268E5C(gobj, 0, ITEM_ANIM_UPDATE);
}

/* +1AE8 of the fighter block. Same as retail it_8029B6F8 (Mario's fireball): previous position
 * from the owner (ftLib_80086990, called directly), position from the hand bone, the common
 * spawn fields with the first flag set, the airborne creator. */
void itTd_Shot_Spawn(HSD_GObj* owner_gobj, Vec3* pos, ItemKind kind, float facing_dir)
{
    SpawnItem spawn = { 0 };
    Item_GObj* item_gobj;

    spawn.kind = kind;
    Item_InitSpawnPositionFromParent(&spawn, owner_gobj, pos);
    Item_InitSpawnCommonFields(&spawn, owner_gobj, facing_dir, true);

    item_gobj = Item_80268B18(&spawn);
    if (item_gobj == NULL) {
        /* The disc does not test this (neither does Mario's). */
        OSReport("[ak] Toad shot: item kind %d was not created\n", (int) kind);
        return;
    }
    itTd_Shot_Init(item_gobj);
    db_80225DD8(item_gobj, owner_gobj);
    it_802750F8(item_gobj);

    {
        /* Bring-up evidence (the first items only). */
        static int logged;
        if (logged < 8) {
            Item* ip = GET_ITEM(item_gobj);
            logged++;
            OSReport("[ak] Toad shot: item kind %d at (%.2f, %.2f) vel (%.2f, %.2f) facing "
                     "%.0f timer %.0f\n",
                     (int) kind, ip->pos.x, ip->pos.y, ip->x40_vel.x, ip->x40_vel.y,
                     ip->facing_dir, ip->xD44_lifeTimer);
        }
    }
}

/* item +68, the state's animation callback: count the lifetime down, done at zero. */
static bool itTd_Shot_Anim(Item_GObj* gobj)
{
    return it_80273130(gobj) ? true : false;
}

/* item +90, the physics callback: the item's fall speed and the spin, as Mario's fireball
 * (it_80272860 with the two fall attributes, it_80274658 with the common value). */
static void itTd_Shot_Phys(Item_GObj* gobj)
{
    Item_ApplyFallingPhysics(gobj);
}

/* item +EC, the collision callback: the common ground collision, then the bounce (it_8027781C,
 * called through the pointer the block keeps at +198). After a bounce the item is used up when
 * it is slower than the least speed of its attributes. The speed is sqrt(x * x + y * y) with
 * the first product fused into the sum (fmadds). */
static bool itTd_Shot_Coll(Item_GObj* gobj)
{
    Item* ip = GET_ITEM(gobj);
    itUnkAttributes* attrs = itTd_Shot_Attrs(ip);
    float speed_sq;

    it_8026D9A0(gobj);
    if (!it_8027781C(gobj)) {
        return false;
    }
    speed_sq = ip->x40_vel.y * ip->x40_vel.y;
    speed_sq = MU_FMADDS(ip->x40_vel.x, ip->x40_vel.x, speed_sq);
    return attrs->x10 > sqrtf(speed_sq);
}

/* item +10, [ongivedamage]: used up. */
static bool itTd_Shot_DmgDealt(Item_GObj* gobj)
{
    (void) gobj;
    return true;
}

/* item +18, [onreflect]: the common reflect, and the item lives on. */
static bool itTd_Shot_Reflected(Item_GObj* gobj)
{
    it_80273030(gobj);
    return false;
}

/* item +3C, [onhitshieldbounce]: the common shield bounce, and the item lives on. */
static bool itTd_Shot_ShieldBounced(Item_GObj* gobj)
{
    itColl_BounceOffShield(gobj);
    return false;
}

/* item +60, [onhitshielddeterminedestroy]: used up. */
static bool itTd_Shot_HitShield(Item_GObj* gobj)
{
    (void) gobj;
    return true;
}

/* item +0, [item_state_table]: one state, animation 0. */
static ItemStateTable itTd_Shot_States[] = {
    { 0, itTd_Shot_Anim, itTd_Shot_Phys, itTd_Shot_Coll },
};

const ItemLogicTable itTd_Articles[ftTd_Article_Count] = {
    {
        /* article 0 (item kind 354 on ACE 2.0.0) */
        .states = itTd_Shot_States,
        .spawned = NULL,
        .destroyed = NULL,
        .picked_up = NULL,
        .dropped = NULL,
        .thrown = NULL,
        .dmg_dealt = itTd_Shot_DmgDealt,
        .dmg_received = NULL,
        .entered_air = NULL,
        .reflected = itTd_Shot_Reflected,
        .clanked = NULL,
        .absorbed = NULL,
        .shield_bounced = itTd_Shot_ShieldBounced,
        .hit_shield = itTd_Shot_HitShield,
        .evt_unk = NULL,
    },
};
