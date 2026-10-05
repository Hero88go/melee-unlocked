/* ACE's Zero: his two articles, the buster shot and the charged shot (the spawn routines of the
 * "ftFunction" block and the two "itFunction" blocks of PlZx.dat, which are built without
 * symbols: "it+" is an offset into either block, run-source/rel09-ace-native/zero/listing.txt
 * and listing_items_tail.txt).
 *
 * The two item blocks are the same code word for word, and the same as the block of Blastoise's
 * Hydro Pump (PlBl.dat article 0); the two spawn routines are Blastoise's too. What differs
 * between the shots is data: the article (model, hitbox, script). Both live 25 frames.
 *
 * m-ex builds an article's logic table from the defaults MxDt.dat holds for the item kind and the
 * fighter file's itFunction exports. For item kinds 338 and 339 MxDt has no default at all, so
 * each table is the seven exports: the state table, destroyed, dmg_dealt, reflected, clanked,
 * shield_bounced and hit_shield.
 *
 * None of Link's articles (bow, arrow, boomerang, bomb, hookshot) has code here: they are
 * retail item kinds with retail logic, and Zero's code never creates one. */
#include "zero.h"

#include <dolphin/os.h>

#include <melee/db/db.h>
#include <melee/ef/efsync.h>
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

#define ITZX_DESTROY_GFX 0x421   /* it+0028 */
#define ITZX_REFLECT_ROT_Z 3.14f /* it+0140 */

/* Item variables (ip->xDD4_itemVar): the spawn stores the owner at ip+DD8. Nothing of his reads
 * it back. */
typedef struct itZx_ItemVars {
    /* ip+DD4 */ s32 x0;
    /* ip+DD8 */ HSD_GObj* owner;
} itZx_ItemVars;

static inline itZx_Attrs* itZx_SpecialAttrs(Item* ip)
{
    return (itZx_Attrs*) DP(ip->xC4_article_data->x4_specialAttributes);
}

/* +24C8 "Spawn_Blaster" and +23A0 "Spawn_Blaster2": the same routine with article 0 and
 * article 1 (MEX_GetFtItemID at 0x803D7088). Position from the owner (ftLib_80086990), previous
 * position from the bone with z cleared, the caller's velocity, the first flag set, the airborne
 * creator; then state 0, the lifetime of the article and the owner written twice. */
static void itZx_Spawn(HSD_GObj* owner_gobj, Vec3* pos, Vec3* vel, float facing_dir,
                       int article, const char* name)
{
    SpawnItem spawn = { 0 };
    Item_GObj* item_gobj;
    Item* ip;
    int kind = (int) mu_ak_article_kind(owner_gobj, article);

    if (kind < 0) {
        /* Not on the console: there the id always exists. Without it nothing can be created. */
        OSReport("[ak] Zero: no item kind for the %s article\n", name);
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
        OSReport("[ak] Zero %s: item kind %d was not created\n", name, kind);
        return;
    }
    db_80225DD8(item_gobj, owner_gobj);
    ip = GET_ITEM(item_gobj);
    Item_80268E5C(item_gobj, 0, ITEM_ANIM_UPDATE);
    it_80275158(item_gobj, itZx_SpecialAttrs(ip)->lifetime);
    ip->owner = owner_gobj;
    ((itZx_ItemVars*) &ip->xDD4_itemVar)->owner = owner_gobj;
    HSD_JObjSetMtxDirtySub((HSD_JObj*) item_gobj->hsd_obj);

    {
        /* Bring-up evidence (the first shots only). */
        static int logged;
        if (logged < 12) {
            logged++;
            OSReport("[ak] Zero %s: item kind %d at (%.2f, %.2f) vel (%.2f, %.2f) "
                     "facing %.0f timer %.0f\n",
                     name, kind, ip->pos.x, ip->pos.y, ip->x40_vel.x, ip->x40_vel.y,
                     ip->facing_dir, ip->xD44_lifeTimer);
        }
    }
}

void itZx_Shot_Spawn(HSD_GObj* owner_gobj, Vec3* pos, Vec3* vel, float facing_dir)
{
    itZx_Spawn(owner_gobj, pos, vel, facing_dir, ftZx_Article_Shot, "shot");
}

void itZx_ChargedShot_Spawn(HSD_GObj* owner_gobj, Vec3* pos, Vec3* vel, float facing_dir)
{
    itZx_Spawn(owner_gobj, pos, vel, facing_dir, ftZx_Article_ChargedShot, "charged shot");
}

/* it+00B0, the state's animation callback: count the lifetime down, done at zero. */
static bool itZx_Anim(Item_GObj* gobj)
{
    return it_80273130(gobj);
}

/* it+00B4, the collision callback. Retail it_8026DB40 with two differences: the position is
 * taken from the collision data before the stage test, not after it, and the answer is the two
 * results masked with 0xD. No physics callback: the shot keeps the velocity it was made with. */
static bool itZx_Coll(Item_GObj* gobj)
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

/* it+0010, [ondestroy]: common effect 1057 at the item, in state 0 only (the only state there
 * is). */
static void itZx_Destroyed(Item_GObj* gobj)
{
    Item* ip = GET_ITEM(gobj);

    if (ip->msid == 0) {
        efSync_Spawn(ITZX_DESTROY_GFX, gobj, &ip->pos);
    }
}

/* it+0034, [ongivedamage]: used up. */
static bool itZx_DmgDealt(Item_GObj* gobj)
{
    (void) gobj;
    return true;
}

/* it+003C, [onreflect]: the lifetime starts again, the model is turned half a turn about z
 * (written straight into the joint, no dirty call), then the common reflect and its answer. */
static bool itZx_Reflected(Item_GObj* gobj)
{
    Item* ip = GET_ITEM(gobj);
    HSD_JObj* jobj = (HSD_JObj*) gobj->hsd_obj;

    ip->xD44_lifeTimer = itZx_SpecialAttrs(ip)->lifetime;
    jobj->rotate.z = ITZX_REFLECT_ROT_Z;
    return it_80273030(gobj);
}

/* it+0064, [onunk1], the clank slot: a bare return, so the caller sees its own argument (the
 * item GObj, never zero) as the answer: used up. */
static bool itZx_Clanked(Item_GObj* gobj)
{
    (void) gobj;
    return true;
}

/* it+0068, [onhitshieldbounce]: the model's z rotation is negated, then the common shield
 * bounce and its answer. */
static bool itZx_ShieldBounced(Item_GObj* gobj)
{
    HSD_JObj* jobj = (HSD_JObj*) gobj->hsd_obj;

    jobj->rotate.z = -jobj->rotate.z;
    HSD_JObjSetMtxDirtySub(jobj);
    return itColl_BounceOffShield(gobj);
}

/* it+00A8, [onhitshielddeterminedestroy]: used up. */
static bool itZx_HitShield(Item_GObj* gobj)
{
    (void) gobj;
    return true;
}

/* it+0000, [item_state_table]: one state, animation 0, no physics callback. One table per
 * article, as on the disc. */
static ItemStateTable itZx_Shot_States[] = {
    { 0, itZx_Anim, NULL, itZx_Coll },
};

static ItemStateTable itZx_ChargedShot_States[] = {
    { 0, itZx_Anim, NULL, itZx_Coll },
};

#define ITZX_LOGIC(state_table)                                                               \
    {                                                                                         \
        .states = (state_table),                                                              \
        .spawned = NULL,                                                                      \
        .destroyed = itZx_Destroyed,                                                          \
        .picked_up = NULL,                                                                    \
        .dropped = NULL,                                                                      \
        .thrown = NULL,                                                                       \
        .dmg_dealt = itZx_DmgDealt,                                                           \
        .dmg_received = NULL,                                                                 \
        .entered_air = NULL,                                                                  \
        .reflected = itZx_Reflected,                                                          \
        .clanked = itZx_Clanked,                                                              \
        .absorbed = NULL,                                                                     \
        .shield_bounced = itZx_ShieldBounced,                                                 \
        .hit_shield = itZx_HitShield,                                                         \
        .evt_unk = NULL,                                                                      \
    }

const ItemLogicTable itZx_Articles[ftZx_Article_Count] = {
    /* article 0 (item kind 338 on ACE 2.0.0): the shot */
    ITZX_LOGIC(itZx_Shot_States),
    /* article 1 (item kind 339): the charged shot */
    ITZX_LOGIC(itZx_ChargedShot_States),
};
