/* Akaneia's Lucas: neutral special, PK Freeze. Structured like Ness's PK Flash: a start state
 * spawns the projectile (article 0), a hold state steers it until B is released or it bursts, and
 * an end state. The projectile itself is lucas_it_pkfreeze.c. */
#include "lucas.h"

#include <melee/ft/fighter.h>
#include <melee/ft/ft_081B.h>
#include <melee/ft/ft_084E.h>
#include <melee/ft/ft_0892.h>
#include <melee/ft/ftanim.h>
#include <melee/ft/ftcommon.h>
#include <melee/ft/ftparts.h>
#include <melee/ft/kinds/ftCommon/ftCo_Fall.h>
#include <melee/ft/kinds/ftCommon/ftCo_FallSpecial.h>
#include <melee/lb/lb_00B0.h>
#include <sysdolphin/baselib/controller.h>

/* code+0x3990 "SpecialN_Init" */
static void ftLc_SpecialN_Init(HSD_GObj* gobj)
{
    Fighter* fp = GET_FIGHTER(gobj);
    ftLucasAttributes* attrs = ftLc_Attrs(fp);
    ftLucas_MotionVars* mv = ftLc_MV(fp);

    ftLc_Vars(fp)->pkfreeze_gobj = NULL;
    fp->cmd_vars[0] = 0;
    fp->cmd_vars[1] = 0;
    fp->cmd_vars[2] = 0;
    fp->cmd_vars[3] = 0;
    ftLc_X233C(fp) = attrs->x0_PKFREEZE_UNK;
    mv->specialn.hold_min = attrs->x4_PKFREEZE_HOLD_MIN;
    mv->specialn.end_delay = attrs->x8_PKFREEZE_END_DELAY;
    mv->specialn.gravity_delay = attrs->xC_PKFREEZE_GRAVITY_DELAY;
    mv->specialn.release_delay = 10;
    fp->death2_cb = NULL;
    fp->take_dmg_cb = NULL;
}

/* m-ex specialn, code+0x5A0 */
void ftLc_SpecialN_Enter(HSD_GObj* gobj)
{
    Fighter_ChangeMotionState(gobj, ftLc_MS_SpecialNStart, 0, 0.0f, 1.0f, 0.0f, NULL);
    ftLc_SpecialN_Init(gobj);
    ftAnim_8006EBA4(gobj);
}

/* m-ex specialairn, code+0x5FC */
void ftLc_SpecialAirN_Enter(HSD_GObj* gobj)
{
    Fighter* fp = GET_FIGHTER(gobj);
    Fighter_ChangeMotionState(gobj, ftLc_MS_SpecialAirNStart, 0, 0.0f, 1.0f, 0.0f, NULL);
    ftLc_SpecialN_Init(gobj);
    fp->self_vel.y = 0.0f;
    ftAnim_8006EBA4(gobj);
}

/* code+0x5A78 "Lucas_RemoveSpecialItemGOBJ": destroy the PK Freeze and drop the callbacks. */
void ftLc_RemovePKFreeze(HSD_GObj* gobj)
{
    Fighter* fp;
    ftLucas_FighterVars* vars;
    if (gobj == NULL) {
        return;
    }
    fp = GET_FIGHTER(gobj);
    if (fp == NULL) {
        return;
    }
    vars = ftLc_Vars(fp);
    if (vars->pkfreeze_gobj != NULL) {
        Item_8026A8EC(vars->pkfreeze_gobj);
        vars->pkfreeze_gobj = NULL;
    }
    fp->death2_cb = NULL;
    fp->take_dmg_cb = NULL;
}

/* code+0x4398 "SpecialN_Start_AnimCB_Shared": at the end of the start animation, go to the hold
 * state and spawn the PK Freeze above his head. */
static void ftLc_SpecialNStart_AnimShared(HSD_GObj* gobj, FtMotionId next)
{
    Fighter* fp = GET_FIGHTER(gobj);
    ftLucas_FighterVars* vars = ftLc_Vars(fp);

    if (ftAnim_IsFramesRemaining(gobj)) {
        return;
    }
    Fighter_ChangeMotionState(gobj, next, 0, 0.0f, 1.0f, 0.0f, NULL);
    if (vars->pkfreeze_gobj == NULL) {
        Vec3 pos;
        lb_8000B1CC(fp->parts[ftLc_Part_Head].joint, NULL, &pos);
        pos.y += fp->x34_scale.y * 3.0f;
        pos.z = 0.0f;
        vars->pkfreeze_gobj = ftLc_PKFreeze_Spawn(
            gobj, &pos, mu_ak_article_kind(gobj, ftLc_Art_PKFreeze), fp->facing_dir);
        if (vars->pkfreeze_gobj != NULL) {
            fp->death2_cb = ftLc_RemoveAllArticles;
            fp->take_dmg_cb = ftLc_RemoveAllArticles;
        }
    }
    /* No double jump after PK Freeze. */
    fp->x1968_jumpsUsed = fp->co_attrs.max_jumps;
}

/* code+0x44A0 "SpecialN_Hold_AnimCB_Shared": hold until the minimum time is over and the freeze
 * has left its flying state (or is gone), then wait out the end delay. */
static void ftLc_SpecialNHold_AnimShared(HSD_GObj* gobj, FtMotionId next)
{
    Fighter* fp = GET_FIGHTER(gobj);
    ftLucas_MotionVars* mv = ftLc_MV(fp);
    Item_GObj* freeze = ftLc_Vars(fp)->pkfreeze_gobj;

    if (mv->specialn.hold_min != 0) {
        mv->specialn.hold_min--;
        return;
    }
    if (freeze != NULL && GET_ITEM(freeze)->msid == 0) {
        return;
    }
    if (mv->specialn.end_delay > 0) {
        mv->specialn.end_delay--;
        return;
    }
    Fighter_ChangeMotionState(gobj, next, 0, 0.0f, 1.0f, 0.0f, NULL);
}

/* code+0x452C "SpecialN_End_AnimCB_Shared": let go of the freeze. */
static void ftLc_SpecialNEnd_AnimShared(HSD_GObj* gobj)
{
    Fighter* fp = GET_FIGHTER(gobj);
    ftLc_Vars(fp)->pkfreeze_gobj = NULL;
    fp->death2_cb = NULL;
    fp->take_dmg_cb = NULL;
}

/* code+0x131C "SpecialN_Hold_IASACB_Shared": after a short delay, releasing B lets go of the
 * freeze (it sees its owner's pointer cleared and bursts). */
void ftLc_SpecialNHold_IASA(HSD_GObj* gobj)
{
    Fighter* fp = GET_FIGHTER(gobj);
    ftLucas_MotionVars* mv = ftLc_MV(fp);
    ftLucas_FighterVars* vars = ftLc_Vars(fp);

    if (mv->specialn.release_delay > 0) {
        mv->specialn.release_delay--;
        return;
    }
    if (fp->input.held_buttons[0] & HSD_PAD_B) {
        return;
    }
    if (vars->pkfreeze_gobj != NULL) {
        vars->pkfreeze_gobj = NULL;
        fp->death2_cb = NULL;
        fp->take_dmg_cb = NULL;
    }
}

/* Grounded states. */

/* code+0x1228 */
void ftLc_SpecialNStart_Anim(HSD_GObj* gobj)
{
    ftLc_SpecialNStart_AnimShared(gobj, ftLc_MS_SpecialNHold);
}

/* code+0x124C */
void ftLc_SpecialNStart_IASA(HSD_GObj* gobj) {}

/* code+0x1250: the gravity delay counts down on the ground too. */
void ftLc_SpecialNStart_Phys(HSD_GObj* gobj)
{
    Fighter* fp = GET_FIGHTER(gobj);
    ftLc_MV(fp)->specialn.gravity_delay--;
    ft_80084F3C(gobj);
}

static void ftLc_SpecialN_GroundToAir(HSD_GObj* gobj, FtMotionId msid)
{
    Fighter* fp = GET_FIGHTER(gobj);
    if (!ft_80082708(gobj)) {
        ftCommon_8007D5D4(fp);
        Fighter_ChangeMotionState(gobj, msid, ftLc_MF_Switch, fp->cur_anim_frame, 1.0f, 0.0f,
                                  NULL);
    }
}

/* code+0x1280 */
void ftLc_SpecialNStart_Coll(HSD_GObj* gobj)
{
    ftLc_SpecialN_GroundToAir(gobj, ftLc_MS_SpecialAirNStart);
}

/* code+0x12F8 */
void ftLc_SpecialNHold_Anim(HSD_GObj* gobj)
{
    ftLc_SpecialNHold_AnimShared(gobj, ftLc_MS_SpecialNEnd);
}

/* code+0x1364 */
void ftLc_SpecialNHold_Phys(HSD_GObj* gobj)
{
    ft_80084F3C(gobj);
}

/* code+0x1384 */
void ftLc_SpecialNHold_Coll(HSD_GObj* gobj)
{
    ftLc_SpecialN_GroundToAir(gobj, ftLc_MS_SpecialAirNHold);
}

/* code+0x13FC */
void ftLc_SpecialNEnd_Anim(HSD_GObj* gobj)
{
    Fighter* fp = GET_FIGHTER(gobj);
    ftLc_SpecialNEnd_AnimShared(gobj);
    ftPartSetRotX(fp, 0, 0.0f);
    if (!ftAnim_IsFramesRemaining(gobj)) {
        ft_8008A2BC(gobj);
    }
}

/* code+0x1464 */
void ftLc_SpecialNEnd_Phys(HSD_GObj* gobj)
{
    ft_80084F3C(gobj);
}

/* Aerial states. */

/* code+0x1484 */
void ftLc_SpecialAirNStart_Anim(HSD_GObj* gobj)
{
    ftLc_SpecialNStart_AnimShared(gobj, ftLc_MS_SpecialAirNHold);
}

/* code+0x14A8 */
void ftLc_SpecialAirNStart_IASA(HSD_GObj* gobj) {}

/* code+0x14AC / 0x15B0 / 0x1718 (three identical copies on the disc). */
void ftLc_SpecialAirN_Phys(HSD_GObj* gobj)
{
    Fighter* fp = GET_FIGHTER(gobj);
    ftLucas_MotionVars* mv = ftLc_MV(fp);
    if (mv->specialn.gravity_delay != 0) {
        mv->specialn.gravity_delay--;
    } else {
        ftCommon_Fall(fp, ftLc_Attrs(fp)->x14_PKFREEZE_FALL_ACCEL,
                      fp->co_attrs.terminal_velocity);
    }
    ftCommon_CalcSelfAccel_Deaccel(fp, fp->co_attrs.aerial_friction);
}

static void ftLc_SpecialN_AirToGround(HSD_GObj* gobj, FtMotionId msid)
{
    Fighter* fp = GET_FIGHTER(gobj);
    ftCollisionBox box = ftCollisionBox_FromDisc(&ftLc_Attrs(fp)->x20_PKFREEZE_LAND_BOX);
    if (ft_800824A0(gobj, &box)) {
        ftCommon_8007D7FC(fp);
        Fighter_ChangeMotionState(gobj, msid, ftLc_MF_Switch, fp->cur_anim_frame, 1.0f, 0.0f,
                                  NULL);
    }
}

/* code+0x150C */
void ftLc_SpecialAirNStart_Coll(HSD_GObj* gobj)
{
    ftLc_SpecialN_AirToGround(gobj, ftLc_MS_SpecialNStart);
}

/* code+0x158C */
void ftLc_SpecialAirNHold_Anim(HSD_GObj* gobj)
{
    ftLc_SpecialNHold_AnimShared(gobj, ftLc_MS_SpecialAirNEnd);
}

/* code+0x1610 */
void ftLc_SpecialAirNHold_Coll(HSD_GObj* gobj)
{
    ftLc_SpecialN_AirToGround(gobj, ftLc_MS_SpecialNHold);
}

/* code+0x1690 */
void ftLc_SpecialAirNEnd_Anim(HSD_GObj* gobj)
{
    Fighter* fp = GET_FIGHTER(gobj);
    float landing_lag = ftLc_Attrs(fp)->x1C_PKFREEZE_LANDING_LAG;
    ftLc_SpecialNEnd_AnimShared(gobj);
    if (!ftAnim_IsFramesRemaining(gobj)) {
        if (landing_lag == 0.0f) {
            ftCo_Fall_Enter(gobj);
        } else {
            ftCo_80096900(gobj, 1, 0, true, 1.0f, landing_lag);
        }
    }
}

/* code+0x1778 */
void ftLc_SpecialAirNEnd_Coll(HSD_GObj* gobj)
{
    ftLc_SpecialN_AirToGround(gobj, ftLc_MS_SpecialNEnd);
}
