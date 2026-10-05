/* ACE's Blastoise: his two articles (the spawn routines of the "ftFunction" block and the two
 * "itFunction" blocks of PlBl.dat, which are built without symbols: "it0+" and "it1+" are offsets
 * into the block of article 0 and of article 1, run-source/rel09-ace-native/blastoise/listing.txt
 * and listing_items_tail.txt).
 *
 * m-ex builds an article's logic table from the defaults MxDt.dat holds for the item kind and the
 * fighter file's itFunction exports. For item kinds 344 and 345 MxDt has no default at all, so
 * each table is the seven exports: the state table, destroyed, dmg_dealt, reflected, clanked,
 * shield_bounced and hit_shield.
 *
 *   0 Hydro Pump (item kind 344): flies straight for 28 frames, no gravity, ends on any surface.
 *   1 Bubble (item kind 345): lives 120 frames, sinks slowly (fall speed 0.005 up to 0.5) and
 *     turns its velocity back off every surface without loss, as Dr. Luigi's pill does.
 * Both are used up when they hit, and both play common effect 1057 when they end in state 0. */
#include "blastoise.h"

#include <dolphin/os.h>

#include <melee/db/db.h>
#include <melee/ef/efsync.h>
#include <melee/ft/ftlib.h>
#include <melee/ft/inlines.h>
#include <melee/ft/types.h>
#include <melee/it/inlines.h>
#include <melee/it/it_2725.h>
#include <melee/it/item.h>
#include <melee/it/itgroundcoll.h>
#include <melee/it/itmaplib.h>
#include <melee/it/types.h>
#include <melee/mp/forward.h>
#include <melee/mp/mpcoll.h>
#include <sysdolphin/baselib/gobj.h>
#include <sysdolphin/baselib/jobj.h>

#define ITBL_DESTROY_GFX 0x421      /* it0+0028, it1+0028 */
#define ITBL_REFLECT_ROT_Z 3.14f    /* it0+0140 */
#define ITBL_BUBBLE_FALL 0.005f     /* it1+0110 */
#define ITBL_BUBBLE_FALL_MAX 0.5f   /* it1+010C */

/* Item variables (ip->xDD4_itemVar): the spawn stores the owner at ip+DD8. Nothing of his reads
 * it back. */
typedef struct itBl_ItemVars {
    /* ip+DD4 */ s32 x0;
    /* ip+DD8 */ HSD_GObj* owner;
} itBl_ItemVars;

static inline itBl_Attrs* itBl_SpecialAttrs(Item* ip)
{
    return (itBl_Attrs*) DP(ip->xC4_article_data->x4_specialAttributes);
}

/* +1BEC "Spawn_HydroPump" and +1AC4 "Spawn_Bubble": the same routine with article 0 and
 * article 1 (MEX_GetFtItemID at 0x803D7088). Position from the owner (ftLib_80086990), previous
 * position from the bone with z cleared, the caller's velocity, the first flag set, the airborne
 * creator; then state 0, the lifetime of the article and the owner written twice. */
static void itBl_Spawn(HSD_GObj* owner_gobj, Vec3* pos, Vec3* vel, float facing_dir,
                       int article, const char* name)
{
    SpawnItem spawn = { 0 };
    Item_GObj* item_gobj;
    Item* ip;
    int kind = (int) mu_ak_article_kind(owner_gobj, article);

    if (kind < 0) {
        /* Not on the console: there the id always exists. Without it nothing can be created. */
        OSReport("[ak] Blastoise: no item kind for the %s article\n", name);
        return;
    }

    spawn.x0_parent_gobj = owner_gobj;
    spawn.x4_parent_gobj2 = owner_gobj;
    spawn.kind = (ItemKind) kind;
    ftLib_80086990(owner_gobj, &spawn.pos);
    spawn.prev_pos.x = pos->x;
    spawn.prev_pos.y = pos->y;
    spawn.prev_pos.z = 0.0f;
    spawn.vel.x = vel->x;
    spawn.vel.y = vel->y;
    spawn.vel.z = vel->z;
    spawn.facing_dir = facing_dir;
    spawn.x3C_damage = 0;
    spawn.x44_flag.b0 = 1;
    spawn.x40 = 0;

    item_gobj = Item_80268B18(&spawn);
    if (item_gobj == NULL) {
        OSReport("[ak] Blastoise %s: item kind %d was not created\n", name, kind);
        return;
    }
    db_80225DD8(item_gobj, owner_gobj);
    ip = GET_ITEM(item_gobj);
    Item_80268E5C(item_gobj, 0, ITEM_ANIM_UPDATE);
    it_80275158(item_gobj, itBl_SpecialAttrs(ip)->lifetime);
    ip->owner = owner_gobj;
    ((itBl_ItemVars*) &ip->xDD4_itemVar)->owner = owner_gobj;
    HSD_JObjSetMtxDirtySub((HSD_JObj*) item_gobj->hsd_obj);

    {
        /* Bring-up evidence (the first shots only). */
        static int logged;
        if (logged < 12) {
            logged++;
            OSReport("[ak] Blastoise %s: item kind %d at (%.2f, %.2f) vel (%.2f, %.2f) "
                     "facing %.0f timer %.0f\n",
                     name, kind, ip->pos.x, ip->pos.y, ip->x40_vel.x, ip->x40_vel.y,
                     ip->facing_dir, ip->xD44_lifeTimer);
        }
    }
}

void itBl_HydroPump_Spawn(HSD_GObj* owner_gobj, Vec3* pos, Vec3* vel, float facing_dir)
{
    itBl_Spawn(owner_gobj, pos, vel, facing_dir, ftBl_Article_HydroPump, "Hydro Pump");
}

void itBl_Bubble_Spawn(HSD_GObj* owner_gobj, Vec3* pos, Vec3* vel, float facing_dir)
{
    itBl_Spawn(owner_gobj, pos, vel, facing_dir, ftBl_Article_Bubble, "Bubble");
}

/* ---- callbacks both articles have, same code in both blocks ---- */

/* it0+00B0, it1+00A0, the state's animation callback: count the lifetime down, done at zero. */
static bool itBl_Anim(Item_GObj* gobj)
{
    return it_80273130(gobj);
}

/* it0+0010, it1+0010, [ondestroy]: common effect 1057 at the item, in state 0 only (the only
 * state there is). */
static void itBl_Destroyed(Item_GObj* gobj)
{
    Item* ip = GET_ITEM(gobj);

    if (ip->msid == 0) {
        efSync_Spawn(ITBL_DESTROY_GFX, gobj, &ip->pos);
    }
}

/* it0+0034, it1+0034, [ongivedamage]: used up. */
static bool itBl_DmgDealt(Item_GObj* gobj)
{
    (void) gobj;
    return true;
}

/* it0+0064, it1+0054, [onunk1], the clank slot: a bare return, so the caller sees its own
 * argument (the item GObj, never zero) as the answer: used up. */
static bool itBl_Clanked(Item_GObj* gobj)
{
    (void) gobj;
    return true;
}

/* it0+0068, it1+0058, [onhitshieldbounce]: the model's z rotation is negated, then the common
 * shield bounce and its answer. */
static bool itBl_ShieldBounced(Item_GObj* gobj)
{
    HSD_JObj* jobj = (HSD_JObj*) gobj->hsd_obj;

    jobj->rotate.z = -jobj->rotate.z;
    HSD_JObjSetMtxDirtySub(jobj);
    return itColl_BounceOffShield(gobj);
}

/* it0+00A8, it1+0098, [onhitshielddeterminedestroy]: used up. */
static bool itBl_HitShield(Item_GObj* gobj)
{
    (void) gobj;
    return true;
}

/* ---- article 0: Hydro Pump ---- */

/* it0+00B4, the collision callback. Retail it_8026DB40 with two differences: the position is
 * taken from the collision data before the stage test, not after it, and the answer is the two
 * results masked with 0xD. No physics callback: the shot keeps the velocity it was made with. */
static bool itBl_HydroPump_Coll(Item_GObj* gobj)
{
    Item* ip = GET_ITEM(gobj);
    CollData* coll = &ip->x378_itemColl;
    s32 touched;
    s32 edge;

    it_80276214(gobj);
    ip->pos = coll->cur_pos;
    touched = mpColl_800471F8(coll);
    edge = it_80276308(gobj);
    if (touched) {
        ip->xC30 = coll->floor.index;
    }
    return ((touched | edge) & 0xD) != 0;
}

/* it0+003C, [onreflect]: the lifetime starts again, the model is turned half a turn about z
 * (written straight into the joint, no dirty call), then the common reflect and its answer. */
static bool itBl_HydroPump_Reflected(Item_GObj* gobj)
{
    Item* ip = GET_ITEM(gobj);
    HSD_JObj* jobj = (HSD_JObj*) gobj->hsd_obj;

    ip->xD44_lifeTimer = itBl_SpecialAttrs(ip)->lifetime;
    jobj->rotate.z = ITBL_REFLECT_ROT_Z;
    return it_80273030(gobj);
}

/* it0+0000, [item_state_table]: one state, animation 0, no physics callback. */
static ItemStateTable itBl_HydroPump_States[] = {
    { 0, itBl_Anim, NULL, itBl_HydroPump_Coll },
};

/* ---- article 1: Bubble ---- */

/* it1+00A4, the physics callback: the fall speed only. */
static void itBl_Bubble_Phys(Item_GObj* gobj)
{
    it_80272860(gobj, ITBL_BUBBLE_FALL, ITBL_BUBBLE_FALL_MAX);
}

/* it1+0114: turn the velocity back off the surface the bubble touches, v = v - 2 (v . n) n, with
 * no loss. The floor is tried first, then the ceiling, the left wall flags and the right wall
 * flags, and the item is put back in the air. The same routine as Dr. Luigi's pill (PlDl.dat
 * item block +0134): the file fuses each multiply with the add that follows it (fmadds), except
 * the first product. */
static void itBl_Bubble_Bounce(Item_GObj* gobj)
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
    vel_y = MU_FMADDS(dot, -normal->y, ip->x40_vel.y);
    vel_x = MU_FMADDS(-dot, normal->x, ip->x40_vel.x);
    ip->x40_vel.y = vel_y;
    ip->x40_vel.x = vel_x;
}

/* it1+00D4, the collision callback: the common item ground collision, then the bounce. */
static bool itBl_Bubble_Coll(Item_GObj* gobj)
{
    it_8026D9A0(gobj);
    itBl_Bubble_Bounce(gobj);
    return false;
}

/* it1+003C, [onreflect]: the lifetime starts again, then the common reflect and its answer. */
static bool itBl_Bubble_Reflected(Item_GObj* gobj)
{
    Item* ip = GET_ITEM(gobj);

    ip->xD44_lifeTimer = itBl_SpecialAttrs(ip)->lifetime;
    return it_80273030(gobj);
}

/* it1+0000, [item_state_table]: one state, animation 0. */
static ItemStateTable itBl_Bubble_States[] = {
    { 0, itBl_Anim, itBl_Bubble_Phys, itBl_Bubble_Coll },
};

const ItemLogicTable itBl_Articles[ftBl_Article_Count] = {
    {
        /* article 0 (item kind 344 on ACE 2.0.0): Hydro Pump */
        .states = itBl_HydroPump_States,
        .spawned = NULL,
        .destroyed = itBl_Destroyed,
        .picked_up = NULL,
        .dropped = NULL,
        .thrown = NULL,
        .dmg_dealt = itBl_DmgDealt,
        .dmg_received = NULL,
        .entered_air = NULL,
        .reflected = itBl_HydroPump_Reflected,
        .clanked = itBl_Clanked,
        .absorbed = NULL,
        .shield_bounced = itBl_ShieldBounced,
        .hit_shield = itBl_HitShield,
        .evt_unk = NULL,
    },
    {
        /* article 1 (item kind 345): Bubble */
        .states = itBl_Bubble_States,
        .spawned = NULL,
        .destroyed = itBl_Destroyed,
        .picked_up = NULL,
        .dropped = NULL,
        .thrown = NULL,
        .dmg_dealt = itBl_DmgDealt,
        .dmg_received = NULL,
        .entered_air = NULL,
        .reflected = itBl_Bubble_Reflected,
        .clanked = itBl_Clanked,
        .absorbed = NULL,
        .shield_bounced = itBl_ShieldBounced,
        .hit_shield = itBl_HitShield,
        .evt_unk = NULL,
    },
};
