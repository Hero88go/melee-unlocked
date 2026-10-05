/* ACE's Daisy: her three articles with code (m-ex "itFunction" of PlDa.dat, blocks 1, 3 and 4;
 * the spawns are in the fighter's block).
 *
 * m-ex builds an article's logic table from the defaults MxDt.dat holds for the item kind and
 * the fighter file's itFunction exports, which replace the slots they name. For item kinds 287
 * to 293 MxDt has no default at all (run-source/rel09-ace-native/daisy/mxdt.txt), so each
 * table is exactly the exports of its block. Articles 0 and 2 have no block: their kinds 287
 * and 289 are never created (the items Peach's retail code makes for her are Peach's retail
 * kinds, with the retail tables).
 *
 *  1 (288) the pulled item: Peach's turnip (it/kinds/itpeachturnip.c) with no face, no weight
 *          table and a damage of 7; most slots call Peach's functions.
 *  3 (290) the Toad of the counter: Peach's Toad (itpeachtoad.c), which tells a Kirby owner
 *          from another owner by the owner's fighter kind where Peach's reads the item kind.
 *  4 (291) the spores: Peach's functions in every slot the block exports. */
#include "daisy.h"

#include <dolphin/os.h>

#include <melee/db/db.h>
#include <melee/ft/inlines.h>
#include <melee/ft/kinds/ftKirby/ftkirbyspecialpeach.h>
#include <melee/ft/types.h>
#include <melee/it/inlines.h>
#include <melee/it/it_26B1.h>
#include <melee/it/it_2725.h>
#include <melee/it/itcoll.h>
#include <melee/it/item.h>
#include <melee/it/kinds/itpeachparasol.h>
#include <melee/it/kinds/itpeachtoad.h>
#include <melee/it/kinds/itpeachtoadspore.h>
#include <melee/it/kinds/itpeachturnip.h>
#include <melee/it/types.h>
#include <melee/lb/lb_00B0.h>
#include <sysdolphin/baselib/gobj.h>

#include <string.h>

/* ==== article 1: the pulled item ================================================================ */

/* The damage of the thrown article (block 1 +0x98: li r9, 7), kept in the word Peach's turnip
 * uses for its face (ip+DD8) and handed to the hitbox when it is thrown. */
#define ITDA_VEG_DAMAGE 7

/* The hold kind the spawn asks for (code+0x1CFC: li r4, 3). */
#define ITDA_VEG_HOLD_KIND 3

/* Fighter block code+0x1C90. Peach's it_802BD4AC without the turnip setup (no lifetime, no
 * face, no damage from a table, no owner word), through the creator that keeps the caller's
 * hold kind (Item_8026862C on the console), and with these stores of its own:
 *  - the word at spawn+0x40 is 1 (Peach's spawns clear it);
 *  - the owner GObj is written over the first item variable word (ip+DD4). Peach's pickup
 *    routine reads the top bit of that byte as "picked up before"; a console address has it
 *    set, so the bit is set here.
 * The file does not test the result of the creator and does not write spawn+0x10 or the
 * ground or air word, which Item_8026862C reads: see NOTES.md, "Native additions". */
Item_GObj* itDa_Veg_Spawn(HSD_GObj* owner_gobj, ItemKind kind, float facing_dir)
{
    Fighter* fp = GET_FIGHTER(owner_gobj);
    SpawnItem spawn = { 0 };
    Item_GObj* item_gobj;
    Item* ip;
    Vec3 pos;

    lb_8000B1CC(fp->parts[FtPart_109].joint, NULL, &pos);

    spawn.kind = kind;
    spawn.hold_kind = ITDA_VEG_HOLD_KIND;
    spawn.x10 = 0;
    spawn.prev_pos = pos;
    spawn.pos = pos;
    spawn.vel.x = spawn.vel.y = spawn.vel.z = 0.0f;
    spawn.facing_dir = facing_dir;
    spawn.x3C_damage = 0;
    spawn.x3E = 0;
    spawn.x40 = 1;
    spawn.x0_parent_gobj = owner_gobj;
    spawn.x4_parent_gobj2 = owner_gobj;
    spawn.x44_flag.b0 = true;
    spawn.x48_ground_or_air = GA_Air;

    item_gobj = mu_ak_item_create(&spawn);
    if (item_gobj == NULL) {
        OSReport("[ak] Daisy pull: item kind %d was not created\n", (int) kind);
        return NULL;
    }
    ip = GET_ITEM(item_gobj);
    memset(&ip->xDD4_itemVar.peachturnip, 0, sizeof(ip->xDD4_itemVar.peachturnip));
    ip->xDD4_itemVar.peachturnip.xDD4.b0 = 1;

    Item_8026AB54(item_gobj, owner_gobj, FtPart_109);
    fp->item_gobj = item_gobj;
    db_80225DD8(item_gobj, owner_gobj);
    it_802750F8(item_gobj);
    return item_gobj;
}

/* Block 1 +0x70, [onpickup]: Peach's routine (with the bit above set it always takes state 4),
 * then the damage word. */
static void itDa_Veg_PickedUp(Item_GObj* gobj)
{
    Item* ip = GET_ITEM(gobj);

    itPeachTurnip_Logic56_PickedUp(gobj);
    ip->xDD4_itemVar.peachturnip.xDD8 = ITDA_VEG_DAMAGE;
}

/* Block 1 +0xC0, [onthrow]: state 2 and the damage into the first hitbox. Peach's also sets
 * the face frame and the scale; hers does neither. */
static void itDa_Veg_Thrown(Item_GObj* gobj)
{
    Item* ip = GET_ITEM(gobj);

    Item_80268E5C(gobj, 2, ITEM_ANIM_UPDATE | ITEM_DROP_UPDATE);
    it_80272460(&ip->x5D4_hitboxes[0].hit, ip->xDD4_itemVar.peachturnip.xDD8, gobj);
}

/* Block 1 +0x0, [item_state_table]: six states. 0 to 4 are Peach's turnip table (the
 * animation callback of 2 and 3, block 1 +0x1E4, counts the lifetime down like Peach's);
 * 5 names the parasol's callback and nothing in the file enters it. */
static ItemStateTable itDa_Veg_States[] = {
    { 1, itPeachturnip_UnkMotion4_Anim, itPeachturnip_UnkMotion4_Phys, NULL },
    { -1, itPeachturnip_UnkMotion1_Anim, itPeachturnip_UnkMotion1_Phys, NULL },
    { 2, itPeachturnip_UnkMotion3_Anim, itPeachturnip_UnkMotion3_Phys,
      itPeachturnip_UnkMotion3_Coll },
    { 2, itPeachturnip_UnkMotion3_Anim, itPeachturnip_UnkMotion3_Phys,
      itPeachturnip_UnkMotion3_Coll },
    { -1, itPeachturnip_UnkMotion4_Anim, itPeachturnip_UnkMotion4_Phys, NULL },
    { -1, itPeachparasol_UnkMotion2_Anim, NULL, NULL },
};

static ItemLogicTable itDa_Veg_Logic = {
    .states = itDa_Veg_States,
    .spawned = NULL,
    .destroyed = itPeachTurnip_Logic56_Destroyed,       /* +0x60: a jump to 0x802BD47C */
    .picked_up = itDa_Veg_PickedUp,                     /* +0x70 */
    .dropped = itPeachTurnip_Logic56_Dropped,           /* +0xB0: a jump to 0x802BD8CC */
    .thrown = itDa_Veg_Thrown,                          /* +0xC0 */
    .dmg_dealt = itPeachTurnip_Logic56_DmgDealt,        /* +0x10C: the same body */
    .dmg_received = NULL,
    .entered_air = NULL,
    .reflected = itPeachTurnip_Logic56_Reflected,       /* +0x14C: the same body */
    .clanked = NULL,
    .absorbed = NULL,
    .shield_bounced = itPeachTurnip_Logic56_ShieldBounced, /* +0x170: the same body */
    .hit_shield = itPeachTurnip_Logic56_HitShield,      /* +0x194: the same body */
    .evt_unk = itPeachTurnip_Logic56_EvtUnk,            /* +0x1B8: a call to 0x802BDA08 */
};

/* ==== article 3: the Toad ======================================================================= */

/* The states of a Kirby who holds her ability during which the Toad stays (block 3 +0x220:
 * the five ids from 0x190). Kirby's copy of her is not native; the test is kept as written. */
#define ITDA_TOAD_KIRBY_MS_FIRST 0x190
#define ITDA_TOAD_KIRBY_MS_SPAN 4

/* The owner lets go of the Toad (block 3 +0x38 to +0xB4; also code+0x1FEC to +0x2094 of the
 * fighter block, which prints "Kirby ran" or "Daisy ran" first: not printed here). A Kirby
 * owner: the two steps of ftKb_SpecialNPe_8010C3C0. */
static void itDa_Toad_OwnerForget(HSD_GObj* owner_gobj)
{
    Fighter* owner_fp = GET_FIGHTER(owner_gobj);

    if (owner_fp->kind == Ft_Kind_Kirby) {
        ftKb_SpecialNPe_8010C47C(owner_gobj);
        owner_fp->u.kb.xD0 = NULL;
    } else {
        ftDa_SpecialN_ToadGone(owner_gobj);
    }
}

/* Whether the owner has left the counter (the tail of both animation callbacks): true with
 * no owner. Her four states are 365 to 368. */
static bool itDa_Toad_OwnerDone(HSD_GObj* owner_gobj)
{
    Fighter* owner_fp;

    if (owner_gobj == NULL) {
        return true;
    }
    owner_fp = GET_FIGHTER(owner_gobj);
    if (owner_fp->kind == Ft_Kind_Kirby) {
        return (u32) (owner_fp->motion_id - ITDA_TOAD_KIRBY_MS_FIRST) > ITDA_TOAD_KIRBY_MS_SPAN;
    }
    return (u32) (owner_fp->motion_id - ftPe_MS_SpecialN) > 3;
}

/* Fighter block code+0x1FC4: remove the Toad from the owner's side (Peach's it_802BDF40). */
void itDa_Toad_Remove(Item_GObj* item_gobj)
{
    Item* ip = GET_ITEM(item_gobj);

    if (ip == NULL) {
        return;
    }
    if (ip->owner != NULL) {
        itDa_Toad_OwnerForget(ip->owner);
    }
    Item_8026A8EC(item_gobj);
}

/* Block 3 +0x20, [ondestroy]. */
static void itDa_Toad_Destroyed(Item_GObj* gobj)
{
    Item* ip = GET_ITEM(gobj);

    if (ip->owner != NULL) {
        itDa_Toad_OwnerForget(ip->owner);
    }
}

/* Block 3 +0x134, the animation callback of state 0: shown on frame 7, hidden from frame 53,
 * gone when the owner has left the counter. */
static bool itDa_Toad_Hold_Anim(Item_GObj* gobj)
{
    Item* ip = GET_ITEM(gobj);

    if (ip->x5CC_currentAnimFrame == 7.0f) {
        it_8026BB20(gobj);
    }
    if (ip->x5CC_currentAnimFrame >= 53.0f) {
        it_8026BB44(gobj);
    }
    if (itDa_Toad_OwnerDone(ip->owner)) {
        if (ip->owner != NULL) {
            itDa_Toad_OwnerForget(ip->owner);
        }
        return true;
    }
    return false;
}

/* Block 3 +0x24C, the animation callback of state 1 (after the counter was hit): hidden from
 * frame 60, gone when the owner has left; the owner is not told here, as in Peach's. */
static bool itDa_Toad_Hit_Anim(Item_GObj* gobj)
{
    Item* ip = GET_ITEM(gobj);

    if (ip->x5CC_currentAnimFrame >= 60.0f) {
        it_8026BB44(gobj);
    }
    return itDa_Toad_OwnerDone(ip->owner);
}

/* Block 3 +0x0, [item_state_table]. */
static ItemStateTable itDa_Toad_States[] = {
    { 0, itDa_Toad_Hold_Anim, NULL, NULL },
    { 1, itDa_Toad_Hit_Anim, NULL, NULL },
};

static ItemLogicTable itDa_Toad_Logic = {
    .states = itDa_Toad_States,
    .spawned = NULL,
    .destroyed = itDa_Toad_Destroyed,             /* +0x20 */
    .picked_up = itPeachToad_Logic91_PickedUp,    /* +0xC4: the same calls */
    .dropped = NULL,
    .thrown = NULL,
    .dmg_dealt = NULL,
    .dmg_received = NULL,
    .entered_air = NULL,
    .reflected = NULL,
    .clanked = NULL,
    .absorbed = NULL,
    .shield_bounced = NULL,
    .hit_shield = NULL,
    .evt_unk = itPeachToad_Logic91_EvtUnk,        /* +0x124: a jump to 0x802BE1F4 */
};

/* ==== article 4: the spores ===================================================================== */

/* Block 4 +0x0, [item_state_table]: one state; its callbacks (+0xD0, +0x118) are Peach's
 * instruction for instruction. */
static ItemStateTable itDa_Spore_States[] = {
    { 0, itPeachtoadspore_UnkMotion0_Anim, itPeachtoadspore_UnkMotion0_Phys, NULL },
};

/* Peach's table has a clank callback as well; her block exports none. */
static ItemLogicTable itDa_Spore_Logic = {
    .states = itDa_Spore_States,
    .spawned = NULL,
    .destroyed = NULL,
    .picked_up = NULL,
    .dropped = NULL,
    .thrown = NULL,
    .dmg_dealt = itPeachToadSpore_Logic92_DmgDealt,           /* +0x10: the same body */
    .dmg_received = NULL,
    .entered_air = NULL,
    .reflected = itPeachToadSpore_Logic68_Reflected,          /* +0x34: the same body */
    .clanked = NULL,
    .absorbed = itPeachToadSpore_Logic68_Absorbed,            /* +0x74: the same body */
    .shield_bounced = itPeachToadSpore_Logic68_ShieldBounced, /* +0x98: the same jump */
    .hit_shield = itPeachToadSpore_Logic68_HitShield,         /* +0x9C: the same body */
    .evt_unk = itPeachToadSpore_Logic92_EvtUnk,               /* +0xC0: a jump to 0x802BE578 */
};

/* ==== the list, in the order of her MxDt item lookup ============================================ */
ItemLogicTable* const itDa_ArticleTables[ftDa_Article_Count] = {
    NULL,              /* 287: the side special's explosion, registered for the retail kind */
    &itDa_Veg_Logic,   /* 288 */
    NULL,              /* 289: the parasol, registered for the retail kind */
    &itDa_Toad_Logic,  /* 290 */
    &itDa_Spore_Logic, /* 291 */
};
