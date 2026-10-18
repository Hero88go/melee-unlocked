/* ACE's Chun-Li: her two articles with code (m-ex "itFunction" of PlCn.dat, articles 2 and 3;
 * the spawn is in the fighter's block).
 *
 * m-ex builds an article's logic table from the defaults MxDt.dat holds for the item kind and
 * the fighter file's itFunction exports. For item kinds 346 to 350 MxDt has no default at all
 * (run-source/rel09-ace-native/chunli/mxdt.txt, custom item entries 109 to 113), so each table
 * is exactly the exports of its block. Articles 0 and 1 have no block and nothing of hers
 * creates them; the fifth entry of the lookup is never registered.
 *
 * Both blocks are one shot: a lifetime and a speed from the article's attributes (article 2:
 * 50 frames at 2.65; article 3: 60 frames at 2.8), straight ahead with no gravity, gone at the
 * end of its lifetime or one frame after it touches a wall, a ceiling or a floor. Gone when it
 * hits; on a reflect it turns over its z axis (3.14) and lives on. Article 2 bounces off a
 * shield, article 3 does not (it lives on and keeps going). The two blocks differ only there. */
#include "chunli.h"

#include <dolphin/os.h>

#include <melee/db/db.h>
#include <melee/ft/inlines.h>
#include <melee/ft/ftlib.h>
#include <melee/ft/types.h>
#include <melee/it/inlines.h>
#include <melee/it/it_26B1.h>
#include <melee/it/it_2725.h>
#include <melee/it/item.h>
#include <melee/it/itgroundcoll.h>
#include <melee/it/types.h>
#include <sysdolphin/baselib/gobj.h>
#include <sysdolphin/baselib/jobj.h>

extern ItemLogicTable ftKbCn_ShotLogic;

/* The article's special attributes (ip->xC4_article_data->x4_specialAttributes, disc data):
 * the two words the blocks read. */
typedef struct itCn_ShotAttrs {
    /* +0 */ float lifetime;
    /* +4 */ float vel_x;
} DISC_STRUCT itCn_ShotAttrs;

/* The surface contacts that end a shot (item 2 +0184, +0188: rlwinm 0,20,18 then clrlwi 15:
 * the low 17 bits of the item's collision flags but bit 12). */
#define ITCN_SHOT_ENV_MASK 0x0001EFFFu

/* ==== the spawn (fighter block) ================================================================== */

/* Fighter block +325C, from the two accessory callbacks (+29BC, +2D24). The request of a retail
 * fighter projectile (Mario's fireball, it_8029B6F8): the position the owner's middle
 * (ftLib_80086990), the previous position the bone's, on the plane, no speed, the facing, no
 * damage, the first collision flag, the airborne creator; then it_8026B3A8 on the new item, the
 * owner for the debug display and it_802750F8.
 *
 * Not written by the console and zeroed here: spawn+0x3E and the seven low bits of spawn+0x44
 * (left as the stack held them), hold_kind, x10 and the ground or air word (the creator writes
 * those three). The console does not test the creator's result: see NOTES.md, "Native
 * additions". */
void itCn_Shot_Spawn(HSD_GObj* owner_gobj, Vec3* pos, ItemKind kind, float facing_dir)
{
    SpawnItem spawn = { 0 };
    Item_GObj* item_gobj;
    Vec3 owner_pos;

    ftLib_80086990(owner_gobj, &owner_pos);

    spawn.x0_parent_gobj = owner_gobj;
    spawn.x4_parent_gobj2 = owner_gobj;
    spawn.kind = kind;
    spawn.pos = owner_pos;
    spawn.prev_pos.x = pos->x;
    spawn.prev_pos.y = pos->y;
    spawn.facing_dir = facing_dir;
    spawn.x3C_damage = 0;
    spawn.x40 = 0;
    spawn.x44_flag.b0 = true;
    spawn.prev_pos.z = 0.0f;
    spawn.vel.x = 0.0f;
    spawn.vel.y = 0.0f;
    spawn.vel.z = 0.0f;

    item_gobj = Item_80268B18(&spawn);
    if (item_gobj == NULL) {
        OSReport("[ak] Chun-Li shot: item kind %d was not created\n", (int) kind);
        return;
    }
    it_8026B3A8(item_gobj);
    db_80225DD8(item_gobj, owner_gobj);
    it_802750F8(item_gobj);

    {
        /* Bring-up evidence (the first shots only). */
        static int logged;
        if (logged < 4) {
            Item* ip = GET_ITEM(item_gobj);
            logged++;
            OSReport("[ak] Chun-Li shot: item kind %d at (%.2f, %.2f) vel x %.2f timer %.0f\n",
                     (int) kind, ip->pos.x, ip->pos.y, ip->x40_vel.x, ip->xD44_lifeTimer);
        }
    }
}

/* ==== the callbacks (the same in both blocks unless noted) ======================================= */

static inline itCn_ShotAttrs* itCn_Shot_Attrs(Item* ip)
{
    return (itCn_ShotAttrs*) DP(ip->xC4_article_data->x4_specialAttributes);
}

/* item 2 +0010 (item 3 +0010), [onspawn]: the lifetime, state 0, the speed times the facing,
 * then it_8026B3A8. */
static void itCn_Shot_Spawned(Item_GObj* gobj)
{
    Item* ip = GET_ITEM(gobj);
    itCn_ShotAttrs* attrs = itCn_Shot_Attrs(ip);

    it_80275158(gobj, attrs->lifetime);
    Item_80268E5C(gobj, 0, ITEM_ANIM_UPDATE);
    ip->x40_vel.x = attrs->vel_x * ip->facing_dir;
    it_8026B3A8(gobj);
}

/* item 2 +0088, +0090, +0098, +00A0, +00B8 (the same in item 3), [ondestroy], [onpickup],
 * [ondrop], [onthrow], [onenterair]: a return value only (1, 0, 0, 0, 0), which these
 * slots do not use. */
static void itCn_Shot_Nothing(Item_GObj* gobj)
{
    (void) gobj;
}

/* item 2 +00A8 (item 3 +00A8), [ongivedamage]: used up. */
static bool itCn_Shot_DmgDealt(Item_GObj* gobj)
{
    (void) gobj;
    return true;
}

/* item 2 +00B0 (item 3 +00B0), [ontakedamage]: used up. */
static bool itCn_Shot_DmgReceived(Item_GObj* gobj)
{
    (void) gobj;
    return true;
}

/* item 2 +00C0 (item 3 +00C0), [onreflect]: the model's z rotation set to 3.14 (stored into the
 * joint, the matrix not marked), the common reflect, and it lives on. */
static bool itCn_Shot_Reflected(Item_GObj* gobj)
{
    HSD_JObj* jobj = GET_JOBJ(gobj);

    jobj->rotate.z = 3.14f;
    it_80273030(gobj);
    return false;
}

/* item 2 +00F4 (item 3 +00F4), [onunk1] (the clank slot): used up. */
static bool itCn_Shot_Clanked(Item_GObj* gobj)
{
    (void) gobj;
    return true;
}

/* item 2 +00FC, [onhitshieldbounce]: the common shield bounce, and it lives on. */
static bool itCn_Shot_ShieldBounced(Item_GObj* gobj)
{
    itColl_BounceOffShield(gobj);
    return false;
}

/* item 3 +00FC, [onhitshieldbounce]: lives on, with no bounce. */
static bool itCn_ShotAlt_ShieldBounced(Item_GObj* gobj)
{
    (void) gobj;
    return false;
}

/* item 2 +0120 (item 3 +0104), [onhitshielddeterminedestroy]: used up. */
static bool itCn_Shot_HitShield(Item_GObj* gobj)
{
    (void) gobj;
    return true;
}

/* item 2 +0128 (item 3 +010C), the animation callback of state 0: one frame of lifetime less,
 * gone at zero. */
static bool itCn_Shot_Anim(Item_GObj* gobj)
{
    Item* ip = GET_ITEM(gobj);
    float lifetime = ip->xD44_lifeTimer - 1.0f;

    ip->xD44_lifeTimer = lifetime;
    return lifetime <= 0.0f;
}

/* item 2 +015C (item 3 +0140), the physics callback of state 0: a return value only. */
static void itCn_Shot_Phys(Item_GObj* gobj)
{
    (void) gobj;
}

/* item 2 +0164 (item 3 +0148), the collision callback of state 0. The collision flags are read
 * BEFORE the common collision step runs (the load is ahead of the call on the console), so the
 * test sees last frame's contacts: the lifetime goes to zero one frame after a contact, and the
 * animation callback then ends the shot. Mod quirk, kept. */
static bool itCn_Shot_Coll(Item_GObj* gobj)
{
    Item* ip = GET_ITEM(gobj);
    u32 env_flags = (u32) ip->x378_itemColl.env_flags;

    it_8026D9A0(gobj);
    if (env_flags & ITCN_SHOT_ENV_MASK) {
        ip->xD44_lifeTimer = 0.0f;
    }
    return false;
}

/* item 2 +0000 (item 3 +0000), [item_state_table]: one state, animation 0. */
static ItemStateTable itCn_Shot_States[] = {
    { 0, itCn_Shot_Anim, itCn_Shot_Phys, itCn_Shot_Coll },
};

static ItemStateTable itCn_ShotAlt_States[] = {
    { 0, itCn_Shot_Anim, itCn_Shot_Phys, itCn_Shot_Coll },
};

/* Article 2 (348): slots 0 to 10, 12 and 13 exported; 11 (absorbed) and 14 not. */
static ItemLogicTable itCn_Shot_Logic = {
    .states = itCn_Shot_States,
    .spawned = itCn_Shot_Spawned,          /* +0010 */
    .destroyed = itCn_Shot_Nothing,        /* +0088 */
    .picked_up = itCn_Shot_Nothing,        /* +0090 */
    .dropped = itCn_Shot_Nothing,          /* +0098 */
    .thrown = itCn_Shot_Nothing,           /* +00A0 */
    .dmg_dealt = itCn_Shot_DmgDealt,       /* +00A8 */
    .dmg_received = itCn_Shot_DmgReceived, /* +00B0 */
    .entered_air = itCn_Shot_Nothing,      /* +00B8 */
    .reflected = itCn_Shot_Reflected,      /* +00C0 */
    .clanked = itCn_Shot_Clanked,          /* +00F4 */
    .absorbed = NULL,
    .shield_bounced = itCn_Shot_ShieldBounced, /* +00FC */
    .hit_shield = itCn_Shot_HitShield,         /* +0120 */
    .evt_unk = NULL,
};

/* Article 3 (349): the same slots. */
static ItemLogicTable itCn_ShotAlt_Logic = {
    .states = itCn_ShotAlt_States,
    .spawned = itCn_Shot_Spawned,          /* +0010 */
    .destroyed = itCn_Shot_Nothing,        /* +0088 */
    .picked_up = itCn_Shot_Nothing,        /* +0090 */
    .dropped = itCn_Shot_Nothing,          /* +0098 */
    .thrown = itCn_Shot_Nothing,           /* +00A0 */
    .dmg_dealt = itCn_Shot_DmgDealt,       /* +00A8 */
    .dmg_received = itCn_Shot_DmgReceived, /* +00B0 */
    .entered_air = itCn_Shot_Nothing,      /* +00B8 */
    .reflected = itCn_Shot_Reflected,      /* +00C0 */
    .clanked = itCn_Shot_Clanked,          /* +00F4 */
    .absorbed = NULL,
    .shield_bounced = itCn_ShotAlt_ShieldBounced, /* +00FC */
    .hit_shield = itCn_Shot_HitShield,            /* +0104 */
    .evt_unk = NULL,
};

/* ==== the list, in the order of her MxDt item lookup ============================================ */
ItemLogicTable* const itCn_ArticleTables[ftCn_Article_Count] = {
    NULL,                /* 346: article 0, no code */
    &ftKbCn_ShotLogic,    /* 347: article 1, code from the Kirby hat */
    &itCn_Shot_Logic,    /* 348: article 2 */
    &itCn_ShotAlt_Logic, /* 349: article 3 */
    NULL,                /* 350: not registered by the load */
};
