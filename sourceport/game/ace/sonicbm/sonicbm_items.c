/* ACE's Sonic BM: his three articles (m-ex "itFunction" of PlSc.dat, one block each) and their
 * spawns (in the fighter's block).
 *
 * m-ex builds an article's logic table from the defaults MxDt.dat holds for the item kind and
 * the fighter file's itFunction exports. For item kinds 320, 321 and 322 MxDt has no default
 * (run-source/rel09-ace-native/sonicbm/mxdt.txt), so each table is the block's exports:
 *
 *   article 0 (320), the spring: state table, dmg_dealt, dmg_received, reflected, hit_shield.
 *     State 0 sits on the floor (put there at the spawn) and lives while its owner's fp+22D4 is
 *     set, up to 175 frames; state 1 falls (the air up special), and on its first rise it pops
 *     (spin -100, 1.0 up, 60 frames, scale 0.7). A fighter that jumps on it is thrown up (4.5,
 *     JumpF or JumpB by the stick) with one jump used; a fighter of the owner's character is
 *     sent into the up special again (or, mid down special, into the air roll).
 *   article 1 (321), an afterimage: state table only. A chain of up to seven links, one more
 *     every 5 frames, each at the middle of the previous one (the first follows the fighter),
 *     scaled from 0.7 up, painted with the costume's "Spin" TEV record and faded in; the chain
 *     lives while its owner is in the homing wind up or dash, the spin charges or the rolls.
 *   article 2 (322), the homing mark: state table only. It sits on the owner's homing target
 *     (fp+2354) while he is in the wind up or the dash.
 *
 * None of them is a retail item's code. Spawns: +31FC (and its copy it1 +2F4), +3434, +3680. */
#include "sonicbm.h"

#include <dolphin/os.h>

#include <string.h>

#include <melee/db/db.h>
#include <melee/ft/fighter.h>
#include <melee/ft/ft_0881.h>
#include <melee/ft/ftanim.h>
#include <melee/ft/ftlib.h>
#include <melee/ft/inlines.h>
#include <melee/ft/types.h>
#include <melee/it/inlines.h>
#include <melee/it/it_2725.h>
#include <melee/it/item.h>
#include <melee/it/itgroundcoll.h>
#include <melee/it/itmaplib.h>
#include <melee/it/types.h>
#include <melee/pl/player.h>
#include <sysdolphin/baselib/dobj.h>
#include <sysdolphin/baselib/gobj.h>
#include <sysdolphin/baselib/jobj.h>
#include <sysdolphin/baselib/mobj.h>
#include <sysdolphin/baselib/tobj.h>

#define itSc_TV(ip) ((itSc_TrailVars*) &(ip)->xDD4_itemVar)
#define itSc_OV(ip) ((itSc_OwnerVars*) &(ip)->xDD4_itemVar)

/* The spawn record the three spawns build on the stack: owner as both parents, hold kind 8,
 * position and previous position equal, no speed, facing 1.0 (whatever the owner's facing),
 * no damage, x40 0, the first flag of +44 clear. The console leaves x10 and x3E as stack
 * contents; here they are zero (as Toad's port). */
static void itSc_InitSpawn(SpawnItem* spawn, HSD_GObj* owner_gobj, ItemKind kind, Vec3* pos,
                           GroundOrAir ground_or_air)
{
    memset(spawn, 0, sizeof(*spawn));
    spawn->x0_parent_gobj = owner_gobj;
    spawn->x4_parent_gobj2 = owner_gobj;
    spawn->kind = kind;
    spawn->hold_kind = 8;
    spawn->pos = *pos;
    spawn->prev_pos = *pos;
    spawn->vel.x = 0.0f;
    spawn->vel.y = 0.0f;
    spawn->vel.z = 0.0f;
    spawn->facing_dir = 1.0f;
    spawn->x3C_damage = 0;
    spawn->x40 = 0;
    spawn->x44_flag.b0 = false;
    spawn->x48_ground_or_air = ground_or_air;
}

/* Not on the console (no test there): one line when the item could not be made. */
static void itSc_LogNoItem(const char* what, ItemKind kind)
{
    static int logged;
    if (logged < 4) {
        logged++;
        OSReport("[ak] Sonic BM %s: item kind %d was not created\n", what, (int) kind);
    }
}

/* ---- article 1, the afterimage --------------------------------------------------------------- */

/* +31FC of the fighter block, and it1 +2F4 (the same 75 instructions, read word by word; each
 * block reads its own constant pool, 0.0 and 1.0 in both). At the owner's position (x, y from
 * ftLib_80086990) and depth `z`: parent, index, a delay of 5 and not painted; state `msid`. */
void itSc_Trail_Spawn(HSD_GObj* owner_gobj, HSD_GObj* parent_gobj, ItemKind kind, s32 index,
                      s32 msid, float z)
{
    SpawnItem spawn;
    Vec3 owner_pos;
    Vec3 pos;
    Item_GObj* item_gobj;
    itSc_TrailVars* tv;

    ftLib_80086990(owner_gobj, &owner_pos);
    pos.x = owner_pos.x;
    pos.y = owner_pos.y;
    pos.z = z;
    itSc_InitSpawn(&spawn, owner_gobj, kind, &pos, GA_Ground);

    item_gobj = Item_80268B18(&spawn);
    if (item_gobj == NULL) {
        itSc_LogNoItem("afterimage", kind);
        return;
    }
    it_80274658(item_gobj, 0.0f);
    tv = itSc_TV(GET_ITEM(item_gobj));
    tv->owner = owner_gobj;
    tv->parent = parent_gobj;
    tv->index = index;
    tv->painted = 0;
    tv->delay = 5;
    Item_80268E5C(item_gobj, msid, ITEM_ANIM_UPDATE);
}

/* it1 +20: both states' animation callback. The delay counts down; at 0, while the index is 5
 * or less, the next link (index + 1, state 1, parented to this one, 0.1 nearer the camera in
 * double) and the delay -1. The scale (x and y) is index / 18 + 0.7. The first frame paints the
 * first texture with the owner's "Spin" record (fp+22E4) and sets the material alpha to 0;
 * later frames raise the alpha by 0.01 (in double) up to 0.8 - index / 10. Used up when the
 * owner is out of 343, 344, 354, 356, 358, 359 and 364 (the mask 0x0021A803 from 343).
 *
 * The console reads the joint, display object, material and texture chain without a check;
 * here a missing link skips the painting (logged once). The TEV record is copied as the game
 * loads one (tobj.c HSD_TObjTevLoadDesc): the bytes, then the big-endian word "active". */
static bool itSc_Trail_Anim(Item_GObj* gobj)
{
    Item* ip = GET_ITEM(gobj);
    itSc_TrailVars* tv = itSc_TV(ip);
    s32 delay = tv->delay;
    HSD_GObj* owner_gobj = tv->owner;
    Fighter* owner = GET_FIGHTER(owner_gobj);
    s32 index = tv->index;
    float scale;
    HSD_JObj* jobj;
    HSD_DObj* dobj;
    u32 n;

    if (delay > 0) {
        tv->delay = delay - 1;
    } else if (index <= 5 && delay != -1) {
        /* +01B0 */
        tv->delay = -1;
        itSc_Trail_Spawn(owner_gobj, gobj, mu_ak_article_kind(owner_gobj, ftSc_Article_Trail),
                         tv->index + 1, 1, (float) ((double) ip->pos.z - 0.1));
        index = tv->index;
    }

    /* +006C */
    scale = (float) index;
    scale = scale / 18.0f;
    scale = scale + 0x1.666666p-1f; /* 0.7 */
    jobj = it_80272CC0(gobj, 0);
    dobj = jobj != NULL ? HSD_JObjGetDObj(jobj) : NULL;
    if (dobj == NULL || dobj->mobj == NULL || dobj->mobj->mat == NULL) {
        static int logged;
        if (!logged) {
            logged = 1;
            OSReport("[ak] Sonic BM afterimage: no material to paint\n");
        }
    } else if (tv->painted == 1) {
        HSD_Material* mat = dobj->mobj->mat;
        float limit = (float) (0.8 - (double) (float) tv->index / 10.0);
        if (mat->alpha < limit) {
            mat->alpha = (float) (mat->alpha + 0.01);
        }
    } else {
        /* +0184 */
        HSD_Material* mat = dobj->mobj->mat;
        HSD_TObjTev* tev = dobj->mobj->tobj != NULL ? dobj->mobj->tobj->tev : NULL;
        HSD_TObjTevDesc* desc = ftSc_FV(owner)->spin_tev;
        tv->painted = 1;
        mat->alpha = 0.0f;
        if (tev != NULL && desc != NULL) {
            memcpy(tev, desc, offsetof(HSD_TObjTev, active));
            tev->active = desc->active;
        }
    }
    if (jobj != NULL) {
        jobj->scale.x = scale;
        jobj->scale.y = scale;
    }

    /* +0114 */
    n = (u32) (owner->motion_id - ftSc_MS_SpecialN);
    if (n > 0x15) {
        return true;
    }
    return (((0x0021A803u >> n) & 1) ^ 1) != 0;
}

/* it1 +218: state 0's physics (the first link): at the parent fighter's position, 2.0 ahead of
 * it (fused in single) while it is on the ground, and 4.0 up. */
static void itSc_Trail_Phys0(Item_GObj* gobj)
{
    Item* ip = GET_ITEM(gobj);
    Fighter* parent = GET_FIGHTER(itSc_TV(ip)->parent);
    float x = parent->cur_pos.x;

    if (parent->ground_or_air != GA_Air) {
        x = MU_FMADDS(parent->facing_dir, 2.0f, x);
    }
    ip->pos.x = x;
    ip->pos.y = parent->cur_pos.y + 4.0f;
}

/* it1 +264: state 1's physics (a later link): halfway to the previous link.
 *
 * The console reads the previous link without a check; when it was used up earlier in the same
 * frame its memory is read after the free (as on the console). */
static void itSc_Trail_Phys1(Item_GObj* gobj)
{
    Item* ip = GET_ITEM(gobj);
    Item* prev = GET_ITEM(itSc_TV(ip)->parent);
    float y = ip->pos.y + prev->pos.y;
    float x = ip->pos.x + prev->pos.x;

    y = y * 0.5f;
    x = x * 0.5f;
    ip->pos.y = y;
    ip->pos.x = x;
}

/* ---- article 2, the homing mark -------------------------------------------------------------- */

/* +3434 of the fighter block: at `pos`, state 0. */
void itSc_Mark_Spawn(HSD_GObj* owner_gobj, ItemKind kind, Vec3* pos)
{
    SpawnItem spawn;
    Item_GObj* item_gobj;

    itSc_InitSpawn(&spawn, owner_gobj, kind, pos, GA_Ground);
    item_gobj = Item_80268B18(&spawn);
    if (item_gobj == NULL) {
        itSc_LogNoItem("mark", kind);
        return;
    }
    it_80274658(item_gobj, 0.0f);
    itSc_OV(GET_ITEM(item_gobj))->owner = owner_gobj;
    Item_80268E5C(item_gobj, 0, ITEM_ANIM_UPDATE);
}

/* it2 +10: animation: used up unless the owner is in 343, 344 or 364. */
static bool itSc_Mark_Anim(Item_GObj* gobj)
{
    Fighter* owner = GET_FIGHTER(itSc_OV(GET_ITEM(gobj))->owner);
    FtMotionId msid = owner->motion_id;

    if (msid > ftSc_MS_SpecialAirN) {
        return msid != ftSc_MS_SpecialNDash;
    }
    return msid < ftSc_MS_SpecialN;
}

/* it2 +50: physics: x and y on the owner's target (fp+2354, fp+2358). */
static void itSc_Mark_Phys(Item_GObj* gobj)
{
    Item* ip = GET_ITEM(gobj);
    Fighter* owner = GET_FIGHTER(itSc_OV(ip)->owner);

    ip->pos.x = ftSc_MV(owner)->target.x;
    ip->pos.y = ftSc_MV(owner)->target.y;
}

/* ---- article 0, the spring ------------------------------------------------------------------- */

static bool itSc_Spring_JumpedOn(Item_GObj* gobj);

/* +3680 of the fighter block. At `pos` (his position), ground or air by `air` (his fp+2340),
 * state `air`, 175 frames, the jumped on callback +3BF4. On the ground it is also marked (the
 * first flag of +DD0), its spin reset and put on the floor (it_80276174).
 *
 * it_80276174 takes a position for the collision's previous position, and the file passes none:
 * r4 there is what it_80274658 leaves in it, the item's own Item pointer, so the console reads
 * the first 12 bytes of the Item (a pointer and two small words) as that position, values of
 * about zero. Here it is (0, 0, 0) (see NOTES.md). */
void itSc_Spring_Spawn(HSD_GObj* owner_gobj, Vec3* pos, ItemKind kind, s32 air)
{
    SpawnItem spawn;
    Vec3 owner_pos;
    Item_GObj* item_gobj;
    Item* ip;

    ftLib_80086990(owner_gobj, &owner_pos);
    itSc_InitSpawn(&spawn, owner_gobj, kind, &owner_pos, air != 0 ? GA_Air : GA_Ground);
    spawn.prev_pos = *pos;

    item_gobj = Item_80268B18(&spawn);
    if (item_gobj == NULL) {
        itSc_LogNoItem("spring", kind);
        return;
    }
    ip = GET_ITEM(item_gobj);
    itSc_OV(ip)->owner = owner_gobj;
    it_80275158(item_gobj, 175.0f);
    Item_80268E5C(item_gobj, air, ITEM_ANIM_UPDATE);
    ip->jumped_on = itSc_Spring_JumpedOn;
    if (air == 0) {
        Vec3 last_pos = { 0.0f, 0.0f, 0.0f };
        ip->xDD0_flag.b0 = true;
        it_80274658(item_gobj, 0.0f);
        it_80276174(item_gobj, &last_pos);
    }
    db_80225DD8(item_gobj, owner_gobj);
    it_802750F8(item_gobj);
}

/* +3D74 of the fighter block: a fighter of the owner's character on the spring. In the air
 * charge or the air roll: the spring sound, the air roll again, 4.0 more up, 60 frames. Else the
 * up special again with no new spring (hi_bounced): the up and side specials used, the script's
 * words cleared, no jumps left. */
static void itSc_Spring_BounceOwn(HSD_GObj* gobj)
{
    Fighter* fp = GET_FIGHTER(gobj);
    ftSc_FighterVars* fv = ftSc_FV(fp);
    int max_jumps;

    if (fp->motion_id == ftSc_MS_SpecialAirLwCharge || fp->motion_id == ftSc_MS_SpecialAirLwRoll)
    {
        ft_800881D8(fp, FTSC_SFX_SPRING, 0x7F, 0x40);
        Fighter_ChangeMotionState(gobj, ftSc_MS_SpecialAirLwRoll, Ft_MF_None, 0.0f, 1.0f, 0.0f,
                                  NULL);
        ftAnim_8006EBA4(gobj);
        ftSc_MV(fp)->count = 60;
        fp->self_vel.y = fp->self_vel.y + 4.0f;
        return;
    }
    max_jumps = fp->co_attrs.max_jumps;
    fv->up_used = 1;
    fv->side_used = 1;
    fp->cmd_vars[1] = 0;
    fp->cmd_vars[2] = 0;
    fp->x1968_jumpsUsed = (u8) max_jumps;
    Fighter_ChangeMotionState(gobj, ftSc_MS_SpecialHi, Ft_MF_None, 0.0f, 1.0f, 0.0f, NULL);
    ftAnim_8006EBA4(gobj);
    ftSc_MV(fp)->hi_air = 0;
    ftSc_MV(fp)->hi_bounced = 1;
}

/* +3BF4 of the fighter block: the spring's jumped on callback. Back to state 0 with 175 frames
 * and the callback set again; the fighter that jumped (ip->xCFC) bounces. "The owner's
 * character" is compared by player character (Player_GetPlayerCharacter of both slots), so a
 * second Sonic BM counts as the owner (kept from the file). */
static bool itSc_Spring_JumpedOn(Item_GObj* gobj)
{
    Item* ip = GET_ITEM(gobj);
    Fighter* owner = GET_FIGHTER(itSc_OV(ip)->owner);
    HSD_GObj* jumper_gobj = ip->xCFC;
    Fighter* jumper = GET_FIGHTER(jumper_gobj);
    float x;

    Item_80268E5C(gobj, 0, ITEM_ANIM_UPDATE);
    ip->jumped_on = itSc_Spring_JumpedOn;
    it_80275158(gobj, 175.0f);
    if (Player_GetPlayerCharacter(owner->player_idx) ==
        Player_GetPlayerCharacter(jumper->player_idx))
    {
        itSc_Spring_BounceOwn(jumper_gobj);
        return false;
    }
    ft_800881D8(owner, FTSC_SFX_SPRING, 0x7F, 0x40);
    x = jumper->input.lstick[0].x;
    if (x < 0.0f) {
        if (jumper->facing_dir <= 0.0f) {
            goto jump_f;
        }
        goto jump_b;
    }
    if (x <= 0.0f) {
        goto jump_f;
    }
    if (jumper->facing_dir < 0.0f) {
        goto jump_b;
    }
jump_f:
    Fighter_ChangeMotionState(jumper_gobj, ftCo_MS_JumpF, Ft_MF_None, 0.0f, 1.0f, 0.0f, NULL);
    goto lift;
jump_b:
    Fighter_ChangeMotionState(jumper_gobj, ftCo_MS_JumpB, Ft_MF_None, 0.0f, 1.0f, 0.0f, NULL);
lift:
    jumper->x1968_jumpsUsed = 1;
    jumper->self_vel.y = jumper->self_vel.y + 4.5f;
    return false;
}

/* it0 +5C: state 0's animation: one frame of the lifetime; used up at once when the owner's
 * fp+22D4 is clear (a newer ground up special started), else when the lifetime is out. */
static bool itSc_Spring_Anim0(Item_GObj* gobj)
{
    Item* ip = GET_ITEM(gobj);
    Fighter* owner = GET_FIGHTER(itSc_OV(ip)->owner);

    ip->xD44_lifeTimer = ip->xD44_lifeTimer - 1.0f;
    if (ftSc_FV(owner)->spring_live == 0) {
        return true;
    }
    return ip->xD44_lifeTimer <= 0.0f;
}

/* it0 +AC: state 0's physics: the item's fall speed (it_80272860 with the two fall
 * attributes). */
static void itSc_Spring_Phys0(Item_GObj* gobj)
{
    Item* ip = GET_ITEM(gobj);

    it_80272860(gobj, ip->xCC_item_attr->x10_fall_speed, ip->xCC_item_attr->x14_fall_speed_max);
}

/* it0 +278: a lone return, the callback the floor collision is given. */
static void itSc_Spring_Nop(HSD_GObj* gobj)
{
    (void) gobj;
}

/* it0 +E0: state 0's collision: the common floor collision, staying on the floor. */
static bool itSc_Spring_Coll0(Item_GObj* gobj)
{
    it_8026E414(gobj, itSc_Spring_Nop);
    return false;
}

/* it0 +10C: state 1's animation: one frame of the lifetime, used up when it is out. */
static bool itSc_Spring_Anim1(Item_GObj* gobj)
{
    Item* ip = GET_ITEM(gobj);

    ip->xD44_lifeTimer = ip->xD44_lifeTimer - 1.0f;
    return ip->xD44_lifeTimer <= 0.0f;
}

/* it0 +140: state 1's physics: the fall speed; on its first rise (y speed over 0 and the
 * command word +DB0 still 0) it pops: spin -100, 1.0 up, +DB0 = 1, 60 frames. From then on its
 * model is scaled to 0.7. */
static void itSc_Spring_Phys1(Item_GObj* gobj)
{
    Item* ip = GET_ITEM(gobj);
    HSD_JObj* jobj;

    it_80272860(gobj, ip->xCC_item_attr->x10_fall_speed, ip->xCC_item_attr->x14_fall_speed_max);
    if (!(ip->x40_vel.y <= 0.0f) && ip->xDB0_itcmd_var1 == 0) {
        /* +01EC */
        ip->xD3C_spinSpeed = -100.0f;
        ip->x40_vel.y = 1.0f;
        ip->xDB0_itcmd_var1 = 1;
        it_80275158(gobj, 60.0f);
    }
    if (ip->xDB0_itcmd_var1 == 1) {
        jobj = it_80272CC0(gobj, 0);
        jobj->scale.x = 0x1.666666p-1f; /* 0.7 */
        jobj->scale.y = 0x1.666666p-1f;
        jobj->scale.z = 0x1.666666p-1f;
    }
}

/* it0 +224: state 1's collision: the common air collision with no landing callback until it
 * has popped. */
static bool itSc_Spring_Coll1(Item_GObj* gobj)
{
    if (GET_ITEM(gobj)->xDB0_itcmd_var1 == 1) {
        return false;
    }
    it_8026E15C(gobj, NULL);
    return false;
}

/* it0 +20 [ongivedamage], +28 [ontakedamage], +54 [onhitshielddeterminedestroy]: used up. */
static bool itSc_Spring_UsedUp(Item_GObj* gobj)
{
    (void) gobj;
    return true;
}

/* it0 +30 [onreflect]: the common reflect, and the item lives on. */
static bool itSc_Spring_Reflected(Item_GObj* gobj)
{
    it_80273030(gobj);
    return false;
}

/* ---- the tables ----------------------------------------------------------------------------- */

/* it0 +00: two states, animations 0 and 1. */
static ItemStateTable itSc_Spring_States[] = {
    { 0, itSc_Spring_Anim0, itSc_Spring_Phys0, itSc_Spring_Coll0 },
    { 1, itSc_Spring_Anim1, itSc_Spring_Phys1, itSc_Spring_Coll1 },
};

/* it1 +00: two states, animations 0 and 1, no collision callback. */
static ItemStateTable itSc_Trail_States[] = {
    { 0, itSc_Trail_Anim, itSc_Trail_Phys0, NULL },
    { 1, itSc_Trail_Anim, itSc_Trail_Phys1, NULL },
};

/* it2 +00: one state, animation 0, no collision callback. */
static ItemStateTable itSc_Mark_States[] = {
    { 0, itSc_Mark_Anim, itSc_Mark_Phys, NULL },
};

const ItemLogicTable itSc_Articles[ftSc_Article_Count] = {
    {
        /* article 0 (item kind 320 on ACE 2.0.0) */
        .states = itSc_Spring_States,
        .spawned = NULL,
        .destroyed = NULL,
        .picked_up = NULL,
        .dropped = NULL,
        .thrown = NULL,
        .dmg_dealt = itSc_Spring_UsedUp,
        .dmg_received = itSc_Spring_UsedUp,
        .entered_air = NULL,
        .reflected = itSc_Spring_Reflected,
        .clanked = NULL,
        .absorbed = NULL,
        .shield_bounced = NULL,
        .hit_shield = itSc_Spring_UsedUp,
        .evt_unk = NULL,
    },
    {
        /* article 1 (item kind 321) */
        .states = itSc_Trail_States,
    },
    {
        /* article 2 (item kind 322) */
        .states = itSc_Mark_States,
    },
};
