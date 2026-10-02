/* King Dedede (Akaneia), native: Side B, Gordo Throw.
 *
 * Dedede pulls out a Gordo (article 2) on the frame the animation script sets cmd_vars[0], holds
 * it on a bone, and throws it on the frame the script sets cmd_vars[1]. The stick picks a forward,
 * up or down toss; a smash input throws faster. Only one Gordo exists per Dedede: held_gordo stays
 * set until the Gordo is destroyed (itDe_Articles.c clears it). Getting hit while holding it
 * destroys it.
 *
 * Source: PlDe.dat ftFunction: SpecialS*, SpecialAirS*, SpecialS_Enter, SpecialS_SetupStateCallbacks,
 * SpecialS_GordoThink, Dedede_HeldGordoOnHit, SpecialS_SpawnGordo, Gordo_EnterThrown,
 * SpecialS_PassLedge, SpecialAirS_TouchGround; itFunction[2] Dedede_RemoveHeldGordo. */
#include "ftDe.h"

#include <string.h>

#include <melee/db/db.h>
#include <melee/ft/fighter.h>
#include <melee/ft/ft_081B.h>
#include <melee/ft/ft_084E.h>
#include <melee/ft/ft_0892.h>
#include <melee/ft/ftanim.h>
#include <melee/ft/ftcommon.h>
#include <melee/ft/inlines.h>
#include <melee/ft/kinds/ftCommon/ftCo_Fall.h>
#include <melee/ft/kinds/ftCommon/forward.h>
#include <melee/it/inlines.h>
#include <melee/it/it_26B1.h>
#include <melee/it/it_2725.h>
#include <melee/it/item.h>
#include <melee/lb/lb_00B0.h>
#include <MSL/math.h>
#include <sysdolphin/baselib/gobj.h>
#include <sysdolphin/baselib/jobj.h>

static void SpecialS_GordoThink(HSD_GObj* gobj);

/* Dedede_HeldGordoOnHit: getting hit (or dying) while holding the Gordo destroys it. */
static void SpecialS_HeldGordoOnHit(HSD_GObj* gobj)
{
    Fighter* fp = GET_FIGHTER(gobj);
    ftDe_FighterVars* fv = ftDe_Vars(fp);
    if (fv->held_gordo != NULL) {
        Item_8026A8EC(fv->held_gordo);
        fv->held_gordo = NULL;
    }
}

/* SpecialS_SetupStateCallbacks */
static void SpecialS_SetCallbacks(HSD_GObj* gobj)
{
    Fighter* fp = GET_FIGHTER(gobj);
    ftDe_MotionVars* mv = ftDe_MV(fp);
    if (mv->specials.spawned == 1 && mv->specials.thrown == 0) {
        fp->death2_cb = SpecialS_HeldGordoOnHit;
        fp->take_dmg_cb = SpecialS_HeldGordoOnHit;
    }
    fp->accessory4_cb = SpecialS_GordoThink;
}

/* SpecialS_Enter (both SpecialS and SpecialAirS) */
void ftDe_SpecialS_Enter(HSD_GObj* gobj)
{
    Fighter* fp = GET_FIGHTER(gobj);
    ftDe_MotionVars* mv = ftDe_MV(fp);
    ftDe_DatAttrs* da = ftDe_Attrs(fp);

    fp->cmd_vars[0] = 0;
    fp->cmd_vars[1] = 0;
    mv->specials.spawned = 0;
    mv->specials.thrown = 0;
    mv->specials.smash = (s32) fp->active_sticky.lstick.x <= da->specials_smash_frames;

    Fighter_ChangeMotionState(gobj, fp->ground_or_air == GA_Air ? ftDe_MS_SpecialAirS : ftDe_MS_SpecialS, 0,
                              0.0f, 1.0f, 0.0f, NULL);
    SpecialS_SetCallbacks(gobj);
}

/* SpecialS_SpawnGordo: spawn the Gordo article at `pos`, not yet thrown. */
Item_GObj* ftDe_SpecialS_SpawnGordo(HSD_GObj* gobj, Vec3* pos, ItemKind kind, float facing_dir)
{
    SpawnItem spawn;
    Item_GObj* item_gobj;
    itDe_GordoVars* gv;

    memset(&spawn, 0, sizeof(spawn));
    spawn.x0_parent_gobj = gobj;
    spawn.x4_parent_gobj2 = gobj;
    spawn.kind = kind;
    spawn.pos = *pos;
    spawn.prev_pos = *pos;
    spawn.vel.x = spawn.vel.y = spawn.vel.z = 0.0f;
    spawn.facing_dir = facing_dir;
    spawn.x3C_damage = 0;
    spawn.x40 = 0;
    spawn.x44_flag.b0 = true;

    item_gobj = Item_80268B18(&spawn);
    if (item_gobj == NULL) {
        return NULL; /* the m-ex code does not check */
    }
    gv = (itDe_GordoVars*) &GET_ITEM(item_gobj)->xDD4_itemVar;
    gv->toss = 0;
    gv->slow = 0.0f;
    gv->dedede = mu_addr32(gobj);
    Item_80268E5C(item_gobj, 0, 2);
    db_80225DD8(item_gobj, gobj);
    return item_gobj;
}

/* Gordo_EnterThrown: let go of the Gordo. The stick (lstick[1], as read by the m-ex code) picks
 * the toss; `vel` goes through it_8027429C and `pos_offset` is added to the position. */
void ftDe_Gordo_EnterThrown(Item_GObj* item_gobj, Vec3* vel, Vec3* pos_offset)
{
    Item* ip = GET_ITEM(item_gobj);
    HSD_JObj* jobj = GET_JOBJ(item_gobj);
    itDe_GordoAttrs* attrs = DP(ip->xC4_article_data->x4_specialAttributes);
    itDe_GordoVars* gv = (itDe_GordoVars*) &ip->xDD4_itemVar;
    Fighter* owner_fp;
    float stick_y, speed, angle;

    Item_80268E5C(item_gobj, 1, 2);
    it_80275158(item_gobj, attrs->lifetime);
    it_8027429C(item_gobj, vel);
    ip->xDC8_word.flags.x14 = false;
    it_8026B3A8(item_gobj);

    owner_fp = GET_FIGHTER(ip->owner);
    stick_y = owner_fp->input.lstick[1].y;
    if (stick_y >= 0.33f) {
        gv->toss = 2;
        speed = attrs->up_speed;
        angle = attrs->up_angle;
        gv->slow = attrs->up_slow;
    } else if (!(stick_y <= -0.33f)) {
        gv->toss = 0;
        speed = attrs->fwd_speed;
        angle = attrs->fwd_angle;
        gv->slow = attrs->fwd_slow;
    } else {
        gv->toss = 1;
        speed = attrs->down_speed;
        angle = attrs->down_angle;
        gv->slow = attrs->down_slow;
    }
    if (ftDe_MV(owner_fp)->specials.smash == 1) {
        speed *= attrs->smash_speed_mul;
    }

    ip->x40_vel.x = speed * ip->facing_dir * cosf(angle);
    ip->x40_vel.y = sinf(angle) * speed;
    ip->x40_vel.z = 0.0f;
    ip->pos.x += pos_offset->x;
    ip->pos.y += pos_offset->y;
    ip->pos.z += pos_offset->z;
    HSD_JObjSetTranslate(jobj, &ip->pos);
    HSD_JObjSetRotationY(jobj, 0.0f);
    it_802731E0(item_gobj);
}

/* SpecialS_GordoThink (accessory4): spawn on cmd_vars[0], throw on cmd_vars[1]. */
static void SpecialS_GordoThink(HSD_GObj* gobj)
{
    Fighter* fp = GET_FIGHTER(gobj);
    ftDe_FighterVars* fv = ftDe_Vars(fp);
    ftDe_MotionVars* mv = ftDe_MV(fp);
    ftDe_DatAttrs* da = ftDe_Attrs(fp);

    if (fp->cmd_vars[0] == 1) {
        fp->cmd_vars[0] = 0;
        if (fv->held_gordo == NULL) {
            s32 bone = da->specials_gordo_bone;
            Item_GObj* gordo;
            Vec3 pos;
            lb_8000B1CC(fp->parts[bone].joint, NULL, &pos);
            gordo = ftDe_SpecialS_SpawnGordo(gobj, &pos, mu_ak_article_kind(gobj, ftDe_Article_Gordo),
                                             fp->facing_dir);
            /* A failed create (item limit, no article data): the throw plays with empty
             * hands, as a retail fighter's move does when its item is not created. The
             * console code does not check. */
            if (gordo != NULL) {
                Item_8026AB54(gordo, gobj, bone);
                fv->held_gordo = gordo;
                mv->specials.spawned = 1;
                fp->x1984_heldItemSpec = gordo;
                SpecialS_SetCallbacks(gobj);
            }
        }
    }

    if (fp->cmd_vars[1] == 1 && fv->held_gordo != NULL && mv->specials.spawned != 0) {
        Vec3 vel = { 0.0f, 0.0f, 0.0f };
        Vec3 offset = { 0.0f, 0.0f, 0.0f };
        fp->cmd_vars[1] = 0;
        mv->specials.thrown = 1;
        ftDe_Gordo_EnterThrown(fv->held_gordo, &vel, &offset);
        fp->death2_cb = NULL;
        fp->take_dmg_cb = NULL;
    }
}

/* Dedede_RemoveHeldGordo: the Gordo is gone, a new one may be pulled. */
void ftDe_SpecialS_ClearHeldGordo(HSD_GObj* gobj)
{
    Fighter* fp = GET_FIGHTER(gobj);
    ftDe_Vars(fp)->held_gordo = NULL;
    fp->death2_cb = NULL;
    fp->take_dmg_cb = NULL;
}

/* ---------------------------------------------------------------- states */

void ftDe_SpecialS_Anim(HSD_GObj* gobj)
{
    if (!ftAnim_IsFramesRemaining(gobj)) {
        ft_8008A2BC(gobj);
    }
}

void ftDe_SpecialAirS_Anim(HSD_GObj* gobj)
{
    if (!ftAnim_IsFramesRemaining(gobj)) {
        ftCo_Fall_Enter(gobj);
    }
}

void ftDe_SpecialS_IASA(HSD_GObj* gobj) {}
void ftDe_SpecialAirS_IASA(HSD_GObj* gobj) {}

void ftDe_SpecialS_Phys(HSD_GObj* gobj)
{
    ft_80084F3C(gobj);
}

void ftDe_SpecialAirS_Phys(HSD_GObj* gobj)
{
    ft_80084EEC(gobj);
}

/* SpecialS_PassLedge: walking off an edge mid-throw. */
static void SpecialS_Fall(HSD_GObj* gobj)
{
    Fighter* fp = GET_FIGHTER(gobj);
    ftCommon_8007D5D4(fp);
    Fighter_ChangeMotionState(gobj, ftDe_MS_SpecialAirS, ftCommon_GroundAirColl_MF | Ft_MF_KeepGfx,
                              fp->cur_anim_frame, 1.0f, 0.0f, NULL);
    SpecialS_SetCallbacks(gobj);
}

/* SpecialAirS_TouchGround */
static void SpecialAirS_Land(HSD_GObj* gobj)
{
    Fighter* fp = GET_FIGHTER(gobj);
    ftCommon_8007D7FC(fp);
    Fighter_ChangeMotionState(gobj, ftDe_MS_SpecialS, Ft_MF_UpdateCmd, fp->cur_anim_frame, 1.0f, 0.0f, NULL);
    SpecialS_SetCallbacks(gobj);
}

void ftDe_SpecialS_Coll(HSD_GObj* gobj)
{
    ft_8008403C(gobj, SpecialS_Fall);
}

void ftDe_SpecialAirS_Coll(HSD_GObj* gobj)
{
    ft_80082C74(gobj, SpecialAirS_Land);
}
