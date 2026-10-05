/* ACE's Metal Mario: his article 0, the neutral special's projectile (m-ex "itFunction" of
 * PlMM.dat, one block; the spawn and the init are in the fighter's block).
 *
 * m-ex builds an article's logic table from the defaults MxDt.dat holds for the item kind (MxDt
 * item section, entry kind - 237) and the fighter file's itFunction exports, which replace the
 * slots they name. For item kinds 325, 326 and 327 MxDt has no default at all
 * (run-source/rel09-ace-native/metalmario/mxdt.txt), so the table of 325 is the six exports:
 * the state table, dmg_dealt, reflected, clanked, shield_bounced and hit_shield. Absorbed and
 * evt_unk stay empty, as on the console (Mario's fireball has both).
 *
 * It is Mario's fireball (it/kinds/itmariofireball.c) with these differences: no effect when it
 * bounces off a surface, the bounce sound for every item kind, "reflected" and "shield_bounced"
 * return a constant in place of what the common routine returns. Its numbers are its own
 * (PlMM.dat: speed 3.5, angle 1.3 radians, 45 frames, least speed 0.9). */
#include "metalmario.h"

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

/* The sound of a bounce off a surface (code+0x188 of the item block: 0x2BF39). A retail id of
 * Mario's bank (18), the bank MxDt gives Metal Mario; the fireball's own id in the retail game. */
#define ITMM_FIRE_BOUNCE_SFX 180025

/* The article's special attributes are read through the type Mario's fireball uses: speed,
 * angle, lifetime, an unread word and the least speed. */
static inline itUnkAttributes* itMM_Fire_Attrs(Item* ip)
{
    return (itUnkAttributes*) DP(ip->xC4_article_data->x4_specialAttributes);
}

/* Fighter block code+0xFB4, called once from the spawn. Same as retail it_8029B7C0:
 * (cos(angle) * speed) * facing and sin(angle) * speed, each product rounded to single. */
static void itMM_Fire_Init(Item_GObj* gobj)
{
    Item* ip = GET_ITEM(gobj);
    itUnkAttributes* attrs = itMM_Fire_Attrs(ip);
    float angle = attrs->x4_float;
    float speed = attrs->x0_float;
    float forward = cosf(angle) * speed;

    ip->x40_vel.x = forward * ip->facing_dir;
    ip->x40_vel.y = sinf(angle) * speed;
    ip->x40_vel.z = 0.0f;

    it_80275158(gobj, attrs->x8);
    Item_80268E5C(gobj, 0, ITEM_ANIM_UPDATE);
}

/* Fighter block code+0xE24. Same as retail it_8029B6F8 (Mario's fireball): previous position
 * from the hand bone with z cleared, position from the owner (it_8026BB68 is ftLib_80086990,
 * which the disc calls directly), the common spawn fields with the first flag set, the airborne
 * creator. */
void itMM_Fire_Spawn(HSD_GObj* owner_gobj, Vec3* pos, ItemKind kind, float facing_dir)
{
    SpawnItem spawn = { 0 };
    Item_GObj* item_gobj;

    spawn.kind = kind;
    Item_InitSpawnPositionFromParent(&spawn, owner_gobj, pos);
    Item_InitSpawnCommonFields(&spawn, owner_gobj, facing_dir, true);

    item_gobj = Item_80268B18(&spawn);
    if (item_gobj == NULL) {
        /* The disc does not test this (neither does Mario's): an item that cannot be created
         * would be a null read there. */
        OSReport("[ak] Metal Mario fire: item kind %d was not created\n", (int) kind);
        return;
    }
    itMM_Fire_Init(item_gobj);
    db_80225DD8(item_gobj, owner_gobj);
    it_802750F8(item_gobj);

    {
        /* Bring-up evidence (the first items only). */
        static int logged;
        if (logged < 8) {
            Item* ip = GET_ITEM(item_gobj);
            logged++;
            OSReport("[ak] Metal Mario fire: item kind %d at (%.2f, %.2f) vel (%.2f, %.2f) "
                     "facing %.0f timer %.0f\n",
                     (int) kind, ip->pos.x, ip->pos.y, ip->x40_vel.x, ip->x40_vel.y,
                     ip->facing_dir, ip->xD44_lifeTimer);
        }
    }
}

/* code+0x70, the state's animation callback: count the lifetime down, done at zero. */
static bool itMM_Fire_Anim(Item_GObj* gobj)
{
    return it_80273130(gobj) ? true : false;
}

/* code+0x98, the physics callback: the item's fall speed and the spin, as Mario's fireball
 * (Item_ApplyFallingPhysics). */
static void itMM_Fire_Phys(Item_GObj* gobj)
{
    Item_ApplyFallingPhysics(gobj);
}

/* code+0xF4 (tail at +0x164), the collision callback: the common ground collision, then the
 * bounce (it_8027781C, which the file calls through a pointer kept at code+0x1A8). After a
 * bounce the item is used up when it is slower than the least speed of its attributes; otherwise
 * it plays the bounce sound and lives on. The speed is sqrt(x * x + y * y) with the first
 * product fused into the sum (fmadds). */
static bool itMM_Fire_Coll(Item_GObj* gobj)
{
    Item* ip = GET_ITEM(gobj);
    itUnkAttributes* attrs = itMM_Fire_Attrs(ip);
    float speed_sq;

    it_8026D9A0(gobj);
    if (!it_8027781C(gobj)) {
        return false;
    }
    speed_sq = ip->x40_vel.y * ip->x40_vel.y;
    speed_sq = MU_FMADDS(ip->x40_vel.x, ip->x40_vel.x, speed_sq);
    if (attrs->x10 > sqrtf(speed_sq)) {
        return true;
    }
    Item_8026AE84(ip, ITMM_FIRE_BOUNCE_SFX, 0x7F, 0x40);
    return false;
}

/* code+0x10, [ongivedamage]: used up. */
static bool itMM_Fire_DmgDealt(Item_GObj* gobj)
{
    return true;
}

/* code+0x18, [onreflect]: the common reflect (it_80273030), and the item lives on whatever
 * that returns. */
static bool itMM_Fire_Reflected(Item_GObj* gobj)
{
    it_80273030(gobj);
    return false;
}

/* code+0x3C, [onunk1], the clank slot: used up. */
static bool itMM_Fire_Clanked(Item_GObj* gobj)
{
    return true;
}

/* code+0x44, [onhitshieldbounce]: the common shield bounce, then used up whatever it returns. */
static bool itMM_Fire_ShieldBounced(Item_GObj* gobj)
{
    itColl_BounceOffShield(gobj);
    return true;
}

/* code+0x68, [onhitshielddeterminedestroy]: used up. */
static bool itMM_Fire_HitShield(Item_GObj* gobj)
{
    return true;
}

/* code+0x0, [item_state_table]: one state, animation 0. */
static ItemStateTable itMM_Fire_States[] = {
    { 0, itMM_Fire_Anim, itMM_Fire_Phys, itMM_Fire_Coll },
};

const ItemLogicTable itMM_Articles[ftMM_Article_Count] = {
    {
        /* article 0 (item kind 325 on ACE 2.0.0) */
        .states = itMM_Fire_States,
        .spawned = NULL,
        .destroyed = NULL,
        .picked_up = NULL,
        .dropped = NULL,
        .thrown = NULL,
        .dmg_dealt = itMM_Fire_DmgDealt,
        .dmg_received = NULL,
        .entered_air = NULL,
        .reflected = itMM_Fire_Reflected,
        .clanked = itMM_Fire_Clanked,
        .absorbed = NULL,
        .shield_bounced = itMM_Fire_ShieldBounced,
        .hit_shield = itMM_Fire_HitShield,
        .evt_unk = NULL,
    },
};
