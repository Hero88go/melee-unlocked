/* ACE's Ninten: his three articles (m-ex "itFunction" of PlNt.dat, blocks 0, 1 and 2; the
 * spawns are in the fighter's block). MxDt holds no default for item kinds 329 to 332
 * (run-source/rel09-ace-native/ninten/mxdt.txt), so each table is exactly the exports of its
 * block.
 *
 *  0 (329) PK Hypnosis: Lucas's PK Freeze article. The block exports the same three slots;
 *          OnDestroy, OnReflect and the callbacks of states 1 and 2 are Lucas's byte for byte,
 *          the three callbacks of state 0 are the same calls and constants with other
 *          registers (lucas_diff.txt). Lucas's table is used as it is.
 *  1 (330) the slingshot's pellet: Lucas's PK Fire bolt that is used up on a hit (no
 *          lingering spark, one state) and has a clank slot.
 *  2 (331) the bat: Lucas's stick, which clears fp+2244 where Lucas's clears fp+2248. */
#include "ninten.h"

#include <string.h>

#include <dolphin/os.h>

#include <melee/ft/inlines.h>
#include <melee/ft/types.h>
#include <melee/it/inlines.h>
#include <melee/it/item.h>
#include <melee/it/types.h>
#include <sysdolphin/baselib/gobj.h>
#include <sysdolphin/baselib/jobj.h>

/* Both held articles keep the fighter that made them at ip+DD4 (as Lucas's). */
static inline HSD_GObj** itNt_Article_Owner(Item* ip)
{
    return (HSD_GObj**) &ip->xDD4_itemVar;
}

/* ==== article 1: the pellet ===================================================================== */

/* Block 1 +0x34, [ongivedamage]: used up (Lucas's bolt turns into a spark here). */
static bool itNt_Pellet_DmgDealt(Item_GObj* gobj)
{
    (void) gobj;
    return true;
}

/* Block 1 +0x54, [onunk1], the clank slot: a lone blr, so the caller reads the GObj still in
 * r3 as the answer: not zero, used up. */
static bool itNt_Pellet_Clanked(Item_GObj* gobj)
{
    (void) gobj;
    return true;
}

/* Block 1 +0x0, [item_state_table]: one state {0, +0xA0, none, +0xA4}, which is row 0 of
 * Lucas's PK Fire table (the animation callback byte for byte, the collision callback the same
 * calls with other registers), so Lucas's table is named; its second row is never entered. */
static ItemLogicTable itNt_Pellet_Logic = {
    .states = ftLc_PKFire_States,
    .destroyed = ftLc_PKFire_OnDestroy,                /* +0x10: Lucas's byte for byte */
    .dmg_dealt = itNt_Pellet_DmgDealt,                 /* +0x34 */
    .reflected = ftLc_PKFire_OnReflect,                /* +0x3C: Lucas's byte for byte */
    .clanked = itNt_Pellet_Clanked,                    /* +0x54 */
    .shield_bounced = ftLc_PKFire_OnHitShieldBounce,   /* +0x58: Lucas's byte for byte */
    .hit_shield = ftLc_PKFire_OnHitShieldDestroy,      /* +0x98: Lucas's byte for byte */
};

/* ==== article 2: the bat ======================================================================== */

/* Fighter block code+0x33C4 "SpawnItem_Bat". Lucas's SpawnItem_Stick with article 2: in his
 * hand, no initial collision, the script variables cleared, the owner at ip+DD4, scaled with
 * the fighter. */
Item_GObj* ftNt_SpawnBat(HSD_GObj* gobj, Vec3* pos, int part, float facing)
{
    Fighter* fp = GET_FIGHTER(gobj);
    SpawnItem spawn;
    Item_GObj* item_gobj;

    memset(&spawn, 0, sizeof(spawn));
    spawn.x0_parent_gobj = gobj;
    spawn.x4_parent_gobj2 = gobj;
    spawn.kind = mu_ak_article_kind(gobj, ftNt_Article_Bat);
    spawn.pos.x = pos->x;
    spawn.pos.y = pos->y;
    spawn.pos.z = 0.0f;
    spawn.prev_pos = spawn.pos;
    spawn.vel.x = spawn.vel.y = spawn.vel.z = 0.0f;
    spawn.facing_dir = facing;
    spawn.x3C_damage = 0;
    spawn.x44_flag.b0 = false;
    spawn.x40 = 0;

    item_gobj = Item_80268B18(&spawn);
    if (item_gobj != NULL) {
        Item* ip = GET_ITEM(item_gobj);
        HSD_JObj* jobj;
        float scale;

        ip->xDAC_itcmd_var0 = 0;
        ip->xDB0_itcmd_var1 = 0;
        ip->xDB4_itcmd_var2 = 0;
        ip->xDB8_itcmd_var3 = 0;
        ip->xDCC_flag.b0 = false;
        *itNt_Article_Owner(ip) = gobj;
        Item_8026AB54(item_gobj, gobj, part);

        jobj = GET_JOBJ(item_gobj);
        scale = ip->xCC_item_attr->x60_scale * fp->x34_scale.y;
        jobj->scale.x = scale;
        jobj->scale.y = scale;
        jobj->scale.z = scale;
        HSD_JObjSetMtxDirtySub(jobj);
        ip->scl = fp->x34_scale.y;
    }

    {
        /* Bring-up evidence (the first ones only). */
        static int logged;
        if (logged < 4) {
            logged++;
            OSReport("[ak] Ninten bat: item kind %d, %s\n", (int) spawn.kind,
                     item_gobj != NULL ? "created" : "not created");
        }
    }
    return item_gobj;
}

/* Block 2 +0x48 "AnimCB": lives while he is in the forward smash (or on the results screen). */
static bool itNt_Bat_Anim(Item_GObj* gobj)
{
    Item* ip = GET_ITEM(gobj);
    HSD_GObj** owner = itNt_Article_Owner(ip);

    if (*owner != NULL) {
        Fighter* owner_fp = GET_FIGHTER(*owner);

        if (owner_fp->x18 == 14) {
            return false;
        }
        if (owner_fp->motion_id == ftLc_MS_AttackS4) {
            if (ftNt_Vars(owner_fp)->bat_gobj != NULL) {
                return false;
            }
        }
        if (*owner == ip->owner) {
            ftNt_Vars(owner_fp)->bat_gobj = NULL;
        }
    }
    *owner = NULL;
    ip->owner = NULL;
    return true;
}

/* Block 2 +0xB4 "CollCB" */
static bool itNt_Bat_Coll(Item_GObj* gobj)
{
    (void) gobj;
    return false;
}

/* Block 2 +0x0, [item_state_table] */
static ItemStateTable itNt_Bat_States[] = {
    { 0, itNt_Bat_Anim, NULL, itNt_Bat_Coll },
};

static ItemLogicTable itNt_Bat_Logic = {
    .states = itNt_Bat_States,
    .picked_up = ftLc_Stick_OnPickup, /* +0x10: Lucas's byte for byte */
};

/* ==== the list, in the order of his MxDt item lookup ============================================ */
ItemLogicTable* const itNt_ArticleTables[ftNt_Article_Count] = {
    &ftLc_ArticleLogic[ftLc_Art_PKFreeze], /* 329: Lucas's table as it is */
    &itNt_Pellet_Logic,                    /* 330 */
    &itNt_Bat_Logic,                       /* 331 */
};
