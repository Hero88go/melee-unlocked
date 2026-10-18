/* ACE's Metal Sonic: his article 0, the neutral special's shot (the m-ex "itFunction" block of
 * PlNm.dat, built without symbols: "it0 +" is an offset into it,
 * run-source/rel09-ace-native/metalsonic/listing.txt), and its two spawns (in the fighter block).
 *
 * m-ex builds an article's logic table from the defaults MxDt.dat holds for the item kind and the
 * fighter file's itFunction exports. For item kind 333 MxDt has no default at all, so the table
 * is the seven exports: the state table, destroyed, dmg_dealt, reflected, clanked (onunk1),
 * shield_bounced and hit_shield. Item kinds 334 to 336 (articles 1 to 3) have no code and no
 * default; the file never hands them to m-ex, so they have no table here.
 *
 * The block is the shape of the Zero and Blastoise shot blocks (ace/zero/zero_items.c) without
 * their destroy effect and shield bounce turn; it is written again here, not shared (other
 * offsets, other code at two exports). Extent: the listing's inferred 228 bytes end at the last
 * `blr` (+E0); the constant 3.14 that +1C reads is at it0 +E4, past the listing (read from the
 * disc: 0x4048F5C3). */
#include "metalsonic.h"

#include <dolphin/os.h>

#include <melee/db/db.h>
#include <melee/ft/ftlib.h>
#include <melee/ft/inlines.h>
#include <melee/ft/types.h>
#include <melee/it/inlines.h>
#include <melee/it/it_2725.h>
#include <melee/it/item.h>
#include <melee/it/itmaplib.h>
#include <melee/it/types.h>
#include <melee/mp/mpcoll.h>
#include <sysdolphin/baselib/gobj.h>
#include <sysdolphin/baselib/jobj.h>

#define ITNM_REFLECT_ROT_Z 3.14f /* it0 +E4 */
#define ITNM_SHORT_LIFETIME 8.0f /* +4FBC, the short shot */

static inline itNm_Attrs* itNm_SpecialAttrs(Item* ip)
{
    return (itNm_Attrs*) DP(ip->xC4_article_data->x4_specialAttributes);
}

static inline itNm_ItemVars* itNm_IV(Item* ip)
{
    return (itNm_ItemVars*) &ip->xDD4_itemVar;
}

/* +54B0 and +567C, the same spawn but the lifetime: the item kind of article 0 (MEX_GetFtItemID,
 * 0x803D7088, index 0); the position from the owner (ftLib_80086990); the previous position from
 * the caller's point with z = 0; the caller's velocity and facing; no damage, x40 0, the first
 * flag of +44 set. The console leaves the hold kind, x10, x3E, the other flags and the
 * ground/air word (+0C, +10, +3E, +45 to +47, +48) as stack contents; here they are zero. Then
 * db_80225DD8, state 0, the lifetime (it_80275158), the owner at ip+518 and ip+DD8, and the joint
 * marked dirty. */
static void itNm_Spawn(HSD_GObj* owner_gobj, Vec3* pos, Vec3* vel, float facing_dir,
                       bool short_shot)
{
    SpawnItem spawn = { 0 };
    Item_GObj* item_gobj;
    Item* ip;
    int kind = (int) mu_ak_article_kind(owner_gobj, ftNm_Article_Shot);

    if (kind < 0) {
        /* Not on the console: there the id always exists. Without it nothing can be created. */
        static int logged;
        if (!logged) {
            logged = 1;
            OSReport("[ak] Metal Sonic: no item kind for the shot article\n");
        }
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
        static int logged;
        if (logged < 4) {
            logged++;
            OSReport("[ak] Metal Sonic shot: item kind %d was not created\n", kind);
        }
        return;
    }
    db_80225DD8(item_gobj, owner_gobj);
    ip = GET_ITEM(item_gobj);
    Item_80268E5C(item_gobj, 0, ITEM_ANIM_UPDATE);
    if (short_shot) {
        it_80275158(item_gobj, ITNM_SHORT_LIFETIME);
    } else {
        it_80275158(item_gobj, itNm_SpecialAttrs(ip)->lifetime);
    }
    ip->owner = owner_gobj;
    itNm_IV(ip)->owner = owner_gobj;
    HSD_JObjSetMtxDirtySub((HSD_JObj*) item_gobj->hsd_obj);

    {
        /* Bring-up evidence (the first shots only). */
        static int logged;
        if (logged < 8) {
            logged++;
            OSReport("[ak] Metal Sonic shot: item kind %d at (%.2f, %.2f) vel (%.2f, %.2f) "
                     "timer %.0f\n",
                     kind, ip->pos.x, ip->pos.y, ip->x40_vel.x, ip->x40_vel.y,
                     ip->xD44_lifeTimer);
        }
    }
}

/* +54B0: the long shot, the article's lifetime (attribute +0, 35). */
void itNm_Shot_Spawn(HSD_GObj* owner_gobj, Vec3* pos, Vec3* vel, float facing_dir)
{
    itNm_Spawn(owner_gobj, pos, vel, facing_dir, false);
}

/* +567C: the short shot, 8 frames (a constant of the fighter block). */
void itNm_ShortShot_Spawn(HSD_GObj* owner_gobj, Vec3* pos, Vec3* vel, float facing_dir)
{
    itNm_Spawn(owner_gobj, pos, vel, facing_dir, true);
}

/* it0 +54, the state's animation callback: count the lifetime down, done at zero. */
static bool itNm_Anim(Item_GObj* gobj)
{
    return it_80273130(gobj);
}

/* it0 +58, the collision callback: it_80276214, the position from the collision data, the stage
 * test (mpColl_800471F8), it_80276308, the floor index kept when touched, and the two results
 * masked with 0xD as the answer. No physics callback: the shot keeps its velocity. */
static bool itNm_Coll(Item_GObj* gobj)
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

/* it0 +10, [ondestroy]: a bare return. */
static void itNm_Destroyed(Item_GObj* gobj)
{
    (void) gobj;
}

/* it0 +14, [ongivedamage]: used up. */
static bool itNm_DmgDealt(Item_GObj* gobj)
{
    (void) gobj;
    return true;
}

/* it0 +1C, [onreflect]: the lifetime starts again (attribute +0), the model's z rotation is set
 * to 3.14 (written straight into the joint, no dirty call), then the common reflect and its
 * answer. */
static bool itNm_Reflected(Item_GObj* gobj)
{
    Item* ip = GET_ITEM(gobj);
    HSD_JObj* jobj = (HSD_JObj*) gobj->hsd_obj;

    ip->xD44_lifeTimer = itNm_SpecialAttrs(ip)->lifetime;
    jobj->rotate.z = ITNM_REFLECT_ROT_Z;
    return it_80273030(gobj);
}

/* it0 +44, [onunk1], the clank slot: a bare return, so the caller sees its own argument (the
 * item GObj, never zero) as the answer: used up. */
static bool itNm_Clanked(Item_GObj* gobj)
{
    (void) gobj;
    return true;
}

/* it0 +48, [onhitshieldbounce]: the common shield bounce (a tail call) and its answer. */
static bool itNm_ShieldBounced(Item_GObj* gobj)
{
    return itColl_BounceOffShield(gobj);
}

/* it0 +4C, [onhitshielddeterminedestroy]: used up. */
static bool itNm_HitShield(Item_GObj* gobj)
{
    (void) gobj;
    return true;
}

/* it0 +00, [item_state_table]: one state, animation 0, no physics callback. */
static ItemStateTable itNm_Shot_States[] = {
    { 0, itNm_Anim, NULL, itNm_Coll },
};

const ItemLogicTable itNm_Articles[ftNm_Article_Count] = {
    {
        /* article 0 (item kind 333 on ACE 2.0.0) */
        .states = itNm_Shot_States,
        .spawned = NULL,
        .destroyed = itNm_Destroyed,
        .picked_up = NULL,
        .dropped = NULL,
        .thrown = NULL,
        .dmg_dealt = itNm_DmgDealt,
        .dmg_received = NULL,
        .entered_air = NULL,
        .reflected = itNm_Reflected,
        .clanked = itNm_Clanked,
        .absorbed = NULL,
        .shield_bounced = itNm_ShieldBounced,
        .hit_shield = itNm_HitShield,
        .evt_unk = NULL,
    },
};
