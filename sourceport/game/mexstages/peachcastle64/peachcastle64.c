/* Peach's Castle 64 (Akaneia 1.0.1 and ACE 2.0.0, the same file: internal 71, external 288), the
 * m-ex grFunction block of GrOPc.dat (80 instructions with symbols) and its one itFunction block
 * (119 instructions, stage item 0 = m-ex item kind 237). Disassembly: `py -3.12 grdis.py ak
 * GrOPc.dat` and `... GrOPc.dat it 0` in run-source/rel09-stages.
 *
 * Exports 1, 3 and 5 only; the other slots keep the MxDt row's words (Dream Land 64's retail
 * on_demo_init, on_start, callback4, on_touch_line, on_check_shadow_render). Three map gobjs:
 * the castle (Sheik's row 0 callbacks), the background (Background_Create), the bridge with its
 * collision and animation; the bridge creates the bumper (stage item 0) on its joint 7, and the
 * bumper follows that joint every frame. The bumper plays its hit animation and sound 0 of the
 * stage's bank when it deals damage.
 *
 * Map table flags are 0 in all three rows (no lights or fog map gobj: the stage takes the default
 * lights), as in the block. */
#include "wp1_common.h"

#include <melee/gr/granime.h>
#include <melee/gr/ground.h>
#include <melee/gr/types.h>
#include <melee/it/forward.h>
#include <melee/it/inlines.h>
#include <melee/it/it_2725.h>
#include <melee/it/item.h>
#include <melee/it/itmaplib.h>
#include <melee/it/itzako.h>
#include <melee/it/types.h>
#include <melee/lb/lb_00B0.h>
#include <melee/lb/lb_00F9.h>
#include <sysdolphin/baselib/gobj.h>
#include <sysdolphin/baselib/jobj.h>

/* The bridge's state in its Ground union: console gp+0xE0 (union +0x1C). */
typedef struct MexgrPcBridge {
    u8 x0_pad[0x1C];
    /* +1C gp+E0 */ Item_GObj* bumper;   /* written once, never read by the block */
} MexgrPcBridge;
_Static_assert(sizeof(MexgrPcBridge) <= sizeof(((Ground*) 0)->u), "bridge state fits the Ground union");

/* Stage_OnCreate (+D8): collision of the map joint, animation set 0, then the bumper at joint 7
 * (it_8027B5B0 with no position: the joint's world position; use_init 1, an air spawn). */
static void mexgr_pc_bridge_create(Ground_GObj* gobj)
{
    Ground* gp = HSD_GObjGetUserData(gobj);
    HSD_JObj* joint;
    ItemKind kind;
    Ground_InitMapColl(GET_JOBJ(gobj), gp->map_id);
    grAnime_801C8138(gobj, gp->map_id, 0);
    joint = Ground_801C3FA4(gobj, 7);
    kind = mu_gr_item_kind(0);
    ((MexgrPcBridge*) &gp->u)->bumper = it_8027B5B0(kind, NULL, joint, NULL, true);
}

/* Stage_OnFrame (+158) */
static void mexgr_pc_bridge_frame(Ground_GObj* gobj)
{
    Ground_UpdateMapColl(gobj);
    lb_800115F4();
}

/* map_gobjs (+0): OnCreation, OnUnk, OnFrame, OnDeletion, flags. Rows 0 and 1 are Sheik's
 * retail words except row 1's Background_Create. */
static StageCallbacks mexgr_pc_map[3] = {
    { mexgr_wp1_tseak_anim, mexgr_wp1_tseak_false, mexgr_wp1_tseak_nop, mexgr_wp1_tseak_nop, 0 },
    { mexgr_wp1_background_create, mexgr_wp1_tseak_false, mexgr_wp1_tseak_coll, mexgr_wp1_tseak_nop, 0 },
    { mexgr_pc_bridge_create, mexgr_wp1_tseak_false, mexgr_pc_bridge_frame, mexgr_wp1_tseak_nop, 0 },
};

/* onInit (+3C) */
static void mexgr_pc_init(void)
{
    mexgr_wp1_init(3);
}

/* ---- stage item 0: the bumper (itFunction block 0) ---- */

/* stateWait (+194) */
static void mexit_pc_state_wait(HSD_GObj* gobj)
{
    it_802762BC(GET_ITEM(gobj));
    Item_80268E5C(gobj, 0, ITEM_ANIM_UPDATE);
}

/* stateHit (+1D4) */
static void mexit_pc_state_hit(HSD_GObj* gobj)
{
    Item_80268E5C(gobj, 1, ITEM_ANIM_UPDATE);
}

/* onSpawn (+20), export 1 (spawned): "li r3, 0" after the call, into a void slot. */
static void mexit_pc_spawned(HSD_GObj* gobj)
{
    mexit_pc_state_wait(gobj);
}

/* onGiveDamage (+44), export 6 (dmg_dealt): sound 0 of the stage's bank, then the hit state. */
static bool mexit_pc_dmg_dealt(HSD_GObj* gobj)
{
    mu_gr_mex_sfx(0);
    mexit_pc_state_hit(gobj);
    return false;
}

/* waitAnim (+80), waitColl (+E4), hitColl (+18C) */
static bool mexit_pc_false(HSD_GObj* gobj)
{
    (void) gobj;
    return false;
}

/* waitPhys (+88) and hitPhys (+130), the same code: the item stands on the joint it_8027B5B0
 * stored in its zako variables (ip+DD4), when it has one. */
static void mexit_pc_follow(HSD_GObj* gobj)
{
    Item* ip = GET_ITEM(gobj);
    HSD_JObj* joint = ip->xDD4_itemVar.zako.jobj;
    if (joint != NULL) {
        Vec3 pos;
        lb_8000B1CC(joint, NULL, &pos);
        ip->pos = pos;
    }
}

/* hitAnim (+EC): back to waiting once the hit animation has no frames left. */
static bool mexit_pc_hit_anim(HSD_GObj* gobj)
{
    if (!it_80272C6C(gobj)) {
        mexit_pc_state_wait(gobj);
    }
    return false;
}

/* item_state_table (+0) */
static ItemStateTable mexit_pc_states[2] = {
    { 0, mexit_pc_false, mexit_pc_follow, mexit_pc_false },
    { 1, mexit_pc_hit_anim, mexit_pc_follow, mexit_pc_false },
};

/* Exports 0, 1, 6. */
static const ItemLogicTable mexit_pc_items[1] = {
    { .states = mexit_pc_states, .spawned = mexit_pc_spawned, .dmg_dealt = mexit_pc_dmg_dealt },
};

const MuGrStage mu_gr_peachs_castle_64 = {
    .name = "Peach's Castle 64",
    .file = "/GrOPc.dat",
    .map_gobjs = mexgr_pc_map,
    .on_init = mexgr_pc_init,
    .on_load = mu_gr_event_nop,
    .items = mexit_pc_items,
    .item_count = 1,
    .flags = MU_GR_READY,
};
