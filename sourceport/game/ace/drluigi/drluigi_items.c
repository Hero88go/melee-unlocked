/* ACE's Dr. Luigi: his article, the pill (m-ex "itFunction" of PlDl.dat, one block).
 *
 * m-ex builds an article's logic table from the defaults MxDt.dat holds for the item kind (MxDt
 * item section, entry kind - 237) and the fighter file's itFunction exports, which replace the
 * slots they name. For item kind 328 MxDt has no default at all, so the table is the six exports:
 * the state table, dmg_dealt, reflected, clanked, shield_bounced and hit_shield. Absorbed and
 * evt_unk stay empty, as on the console.
 *
 * The pill is not Dr. Mario's (it/kinds/itdrmariopill.c). It has one state, does not fall, keeps
 * its speed, and turns its velocity back off any surface it touches until its lifetime ends. The
 * spawn is Mario's fireball spawn (it/kinds/itmariofireball.c, it_8029B6F8); the rest is its
 * own. */
#include "drluigi.h"

#include <dolphin/os.h>

#include <math.h>

#include <melee/db/db.h>
#include <melee/ft/inlines.h>
#include <melee/ft/types.h>
#include <melee/it/inlines.h>
#include <melee/it/it_2725.h>
#include <melee/it/it_3F14.h>
#include <melee/it/item.h>
#include <melee/it/itgroundcoll.h>
#include <melee/it/kinds/inlines.h>
#include <melee/it/types.h>
#include <melee/mp/forward.h>
#include <sysdolphin/baselib/gobj.h>

/* The vertical speed a new pill gets (code+0x600): a constant, not sin(angle) * speed. */
#define ITDL_PILL_VEL_Y -0.3f

static inline itDrLuigiPill_Attrs* itDl_Pill_Attrs(Item* ip)
{
    return (itDrLuigiPill_Attrs*) DP(ip->xC4_article_data->x4_specialAttributes);
}

/* code+0xA68, called once from the spawn. Mario's it_8029B7C0 with a fixed vertical speed:
 * (cos(angle) * speed) * facing, in that order, each product rounded to single. */
static void itDl_Pill_Init(Item_GObj* gobj)
{
    Item* ip = GET_ITEM(gobj);
    itDrLuigiPill_Attrs* attrs = itDl_Pill_Attrs(ip);
    float speed = attrs->speed;
    float forward = cosf(attrs->angle) * speed;

    ip->x40_vel.x = forward * ip->facing_dir;
    ip->x40_vel.y = ITDL_PILL_VEL_Y;
    ip->x40_vel.z = 0.0f;

    it_80275158(gobj, attrs->lifetime);
    Item_80268E5C(gobj, 0, ITEM_ANIM_UPDATE);
}

/* code+0x8D8. Same as retail it_8029B6F8 (Mario's fireball): previous position from the hand
 * bone with z cleared, position from the owner (it_8026BB68 is ftLib_80086990, which the disc
 * calls directly), the common spawn fields with the first flag set, the airborne creator. */
void itDl_Pill_Spawn(HSD_GObj* owner_gobj, Vec3* pos, ItemKind kind, float facing_dir)
{
    SpawnItem spawn = { 0 };
    Item_GObj* item_gobj;

    spawn.kind = kind;
    Item_InitSpawnPositionFromParent(&spawn, owner_gobj, pos);
    Item_InitSpawnCommonFields(&spawn, owner_gobj, facing_dir, true);

    item_gobj = Item_80268B18(&spawn);
    if (item_gobj == NULL) {
        /* The disc does not test this (neither does Mario's): a pill that cannot be created
         * would be a null read there. */
        OSReport("[ak] Dr. Luigi pill: item kind %d was not created\n", (int) kind);
        return;
    }
    itDl_Pill_Init(item_gobj);
    db_80225DD8(item_gobj, owner_gobj);
    it_802750F8(item_gobj);

    {
        /* Bring-up evidence (the first pills only). */
        static int logged;
        if (logged < 8) {
            Item* ip = GET_ITEM(item_gobj);
            logged++;
            OSReport("[ak] Dr. Luigi pill: item kind %d at (%.2f, %.2f) vel (%.2f, %.2f) "
                     "facing %.0f timer %.0f\n",
                     (int) kind, ip->pos.x, ip->pos.y, ip->x40_vel.x, ip->x40_vel.y,
                     ip->facing_dir, ip->xD44_lifeTimer);
        }
    }
}

/* code+0xA4, the state's animation callback: count the lifetime down, done at zero. */
static bool itDl_Pill_Anim(Item_GObj* gobj)
{
    return it_80273130(gobj) ? true : false;
}

/* code+0xCC, the physics callback: the spin only. Mario's fireball and Dr. Mario's pill also
 * apply their fall speed here (it_80272860); this one does not, so the pill never accelerates. */
static void itDl_Pill_Phys(Item_GObj* gobj)
{
    it_80274658(gobj, it_804D6D28->x68_float);
}

/* code+0x134: turn the velocity back off the surface the pill touches, v = v - 2 (v . n) n,
 * with no loss. The floor is tried first, then the ceiling, the left wall flags and the right
 * wall flags, and the item is put back in the air. The disc fuses each multiply with the add
 * that follows it (fmadds), except the first product. */
static void itDl_Pill_Bounce(Item_GObj* gobj)
{
    Item* ip = GET_ITEM(gobj);
    CollData* coll = &ip->x378_itemColl;
    s32 flags = coll->env_flags;
    Vec3* normal;
    float dot;
    float vel_x;
    float vel_y;

    if (flags & Collide_FloorMask) {
        normal = &coll->floor.normal;
    } else if (flags & Collide_CeilingMask) {
        normal = &coll->ceiling.normal;
    } else if (flags & Collide_LeftWallMask) {
        normal = &coll->left_facing_wall.normal;
    } else if (flags & Collide_RightWallMask) {
        normal = &coll->right_facing_wall.normal;
    } else {
        return;
    }

    ip->ground_or_air = GA_Air;
    dot = ip->x40_vel.y * normal->y;
    dot = MU_FMADDS(ip->x40_vel.x, normal->x, dot);
    dot = dot + dot;
    dot = -dot;
    vel_y = MU_FMADDS(dot, normal->y, ip->x40_vel.y);
    vel_x = MU_FMADDS(dot, normal->x, ip->x40_vel.x);
    ip->x40_vel.y = vel_y;
    ip->x40_vel.x = vel_x;
}

/* code+0xFC, the collision callback: the common item ground collision, then the bounce. */
static bool itDl_Pill_Coll(Item_GObj* gobj)
{
    it_8026D9A0(gobj);
    itDl_Pill_Bounce(gobj);
    return false;
}

/* code+0x10, [ongivedamage]: play the hit sound the attributes name, and the pill is used up.
 * Dr. Mario's itDrMarioPill_DmgDealt with the sound id read from the article. */
static bool itDl_Pill_DmgDealt(Item_GObj* gobj)
{
    Item* ip = GET_ITEM(gobj);

    Item_8026AE84(ip, itDl_Pill_Attrs(ip)->hit_sfx, 0x7F, 0x40);
    return true;
}

/* code+0x4C, [onreflect]: the common reflect (it_80273030), and the pill lives on whatever
 * that returns. */
static bool itDl_Pill_Reflected(Item_GObj* gobj)
{
    it_80273030(gobj);
    return false;
}

/* code+0x70, [onunk1], the clank slot: used up. */
static bool itDl_Pill_Clanked(Item_GObj* gobj)
{
    return true;
}

/* code+0x78, [onhitshieldbounce]: the common shield bounce, then used up whatever it returns. */
static bool itDl_Pill_ShieldBounced(Item_GObj* gobj)
{
    itColl_BounceOffShield(gobj);
    return true;
}

/* code+0x9C, [onhitshielddeterminedestroy]: used up. */
static bool itDl_Pill_HitShield(Item_GObj* gobj)
{
    return true;
}

/* code+0x0, [item_state_table]: one state, animation 0. */
static ItemStateTable itDl_Pill_States[] = {
    { 0, itDl_Pill_Anim, itDl_Pill_Phys, itDl_Pill_Coll },
};

const ItemLogicTable itDl_Articles[ftDl_Article_Count] = {
    {
        /* article 0 (item kind 328 on ACE 2.0.0): the pill */
        .states = itDl_Pill_States,
        .spawned = NULL,
        .destroyed = NULL,
        .picked_up = NULL,
        .dropped = NULL,
        .thrown = NULL,
        .dmg_dealt = itDl_Pill_DmgDealt,
        .dmg_received = NULL,
        .entered_air = NULL,
        .reflected = itDl_Pill_Reflected,
        .clanked = itDl_Pill_Clanked,
        .absorbed = NULL,
        .shield_bounced = itDl_Pill_ShieldBounced,
        .hit_shield = itDl_Pill_HitShield,
        .evt_unk = NULL,
    },
};
