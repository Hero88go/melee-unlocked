/* Charizard Side B, Flame Wheel (states 353-360).
 *
 * A start, a roll of specials_frames at specials_speed that tilts with the floor, and an end; the
 * air version falls slowly during the start. Script command variables drive it: cmd_vars[0] turns
 * on the deceleration, cmd_vars[1] lets the air end grab ledges, cmd_vars[2] (ground end) stops at
 * ledges and levels the model. The "Blown" states (355, 359) are in the table but nothing in
 * Charizard's code enters them. mv.specials.charged is only ever 0, so its slower variant (colour
 * overlay 0x44, 0.75 animation rate, two-thirds speed) never plays. */
#include "ftlizardon.h"

#include <math.h>

#include <melee/ef/eflib.h>
#include <melee/ft/fighter.h>
#include <melee/ft/ft_081B.h>
#include <melee/ft/ft_084E.h>
#include <melee/ft/ft_0892.h>
#include <melee/ft/ftanim.h>
#include <melee/ft/ftcliffcommon.h>
#include <melee/ft/ftcolanim.h>
#include <melee/ft/ftcommon.h>
#include <melee/ft/ftparts.h>
#include <melee/ft/inlines.h>
#include <melee/ft/kinds/ftCommon/ftCo_Fall.h>
#include <melee/ft/kinds/ftCommon/ftCo_FallSpecial.h>
#include <melee/ft/kinds/ftCommon/ftCo_Landing.h>
#include <melee/ft/types.h>

/* Colour overlay of the (unused) charged roll. */
#define FTLZ_SPECIALS_CHARGED_COLANIM 0x44

static void ftLz_SpecialS_SetHitlagCallbacks(Fighter* fp)
{
    Fighter_SetEffectHitlagCallbacks(fp);
}

/* The roll's friction: specials_friction, two thirds of it when charged. */
static float ftLz_SpecialS_Friction(ftLz_DatAttrs* da, ftLz_MotionVars* mv)
{
    float friction = da->specials_friction;
    if (mv->specials.charged) {
        friction = (float) (friction * (2.0 / 3.0));
    }
    return friction;
}

/* PlLz SpecialS_UpdateRotation: tilt the model with the floor under it. */
static void ftLz_SpecialS_UpdateRotation(HSD_GObj* gobj)
{
    Fighter* fp = GET_FIGHTER(gobj);
    ftPartSetRotZ(fp, 0,
                  -atan2f(fp->coll_data.floor.normal.x, fp->coll_data.floor.normal.y));
}

/* PlLz SpecialS_Start_OnEnter. */
static void ftLz_SpecialSStart_OnEnter(HSD_GObj* gobj)
{
    Fighter* fp = GET_FIGHTER(gobj);
    ftLz_MV(fp)->specials.charged = 0;
    ftLz_SpecialS_SetHitlagCallbacks(fp);
}

/* PlLz SpecialS_OnEnter: entering the roll. */
static void ftLz_SpecialS_OnEnter(HSD_GObj* gobj)
{
    Fighter* fp = GET_FIGHTER(gobj);
    ftLz_MotionVars* mv = ftLz_MV(fp);

    ftAnim_8006EBA4(gobj);
    mv->specials.frames = 0;
    if (mv->specials.charged) {
        ftCo_800BFFD0(fp, FTLZ_SPECIALS_CHARGED_COLANIM, false);
        ftAnim_SetAnimRate(gobj, 0.75F);
    }
    fp->cmd_vars[0] = 0;
    ftLz_SpecialS_SetHitlagCallbacks(fp);
}

/* PlLz SpecialS_End_OnEnter. */
static void ftLz_SpecialSEnd_OnEnter(HSD_GObj* gobj)
{
    Fighter* fp = GET_FIGHTER(gobj);
    fp->cmd_vars[0] = 0;
    fp->cmd_vars[1] = 0;
    fp->cmd_vars[2] = 0;
    ftLz_SpecialS_SetHitlagCallbacks(fp);
}

/* The roll's starting speed, unless the animation moves the fighter (x594_b0). */
static float ftLz_SpecialS_RollSpeed(Fighter* fp)
{
    ftLz_DatAttrs* da = ftLz_Attrs(fp);
    float speed = da->specials_speed;
    if (ftLz_MV(fp)->specials.charged) {
        speed = (float) (speed * (2.0 / 3.0));
    }
    return fp->facing_dir * speed;
}

/* A ground/air switch inside the move keeps the animation where it is. */
static void ftLz_SpecialS_SwitchState(HSD_GObj* gobj, FtMotionId msid)
{
    Fighter* fp = GET_FIGHTER(gobj);
    Fighter_ChangeMotionState(gobj, msid, ftLz_MF_SpecialS_Coll, fp->cur_anim_frame,
                              fp->frame_speed_mul, fp->x8A4_animBlendFrames, NULL);
}

/* ---- entries ---- */

/* m-ex "specials" (PlLz SpecialS_Start_Enter). */
void ftLz_SpecialS_Enter(HSD_GObj* gobj)
{
    Fighter* fp = GET_FIGHTER(gobj);
    Fighter_ChangeMotionState(gobj, ftLz_MS_SpecialSStart, Ft_MF_None, 0.0F, 1.0F, 0.0F, NULL);
    ftLz_SpecialSStart_OnEnter(gobj);
    fp->self_vel.x = 0.0F;
    fp->self_vel.y = 0.0F;
}

/* m-ex "specialairs" (PlLz SpecialAirS_Start_Enter). */
void ftLz_SpecialAirS_Enter(HSD_GObj* gobj)
{
    Fighter* fp = GET_FIGHTER(gobj);
    ftLz_DatAttrs* da = ftLz_Attrs(fp);
    Fighter_ChangeMotionState(gobj, ftLz_MS_SpecialAirSStart, Ft_MF_None, 0.0F, 1.0F, 0.0F,
                              NULL);
    ftLz_SpecialSStart_OnEnter(gobj);
    ftCommon_8007D60C(fp);
    fp->self_vel.x *= da->specialairs_start_vel_x_mul;
    fp->self_vel.y = 0.0F;
}

/* PlLz SpecialS_Enter: start rolling on the ground. */
static void ftLz_SpecialS_EnterRoll(HSD_GObj* gobj)
{
    Fighter* fp = GET_FIGHTER(gobj);
    Fighter_ChangeMotionState(gobj, ftLz_MS_SpecialS, Ft_MF_KeepGfx, 0.0F, 1.0F, 0.0F, NULL);
    ftLz_SpecialS_OnEnter(gobj);
    if (!fp->x594_b0) {
        fp->gr_vel = ftLz_SpecialS_RollSpeed(fp);
        fp->xF0_ground_kb_vel = 0.0F;
    }
}

/* PlLz SpecialAirS_Enter: start rolling in the air. */
static void ftLz_SpecialAirS_EnterRoll(HSD_GObj* gobj)
{
    Fighter* fp = GET_FIGHTER(gobj);
    Fighter_ChangeMotionState(gobj, ftLz_MS_SpecialAirS, Ft_MF_KeepGfx, 0.0F, 1.0F, 0.0F,
                              NULL);
    ftLz_SpecialS_OnEnter(gobj);
    if (!fp->x594_b0) {
        fp->self_vel.x = ftLz_SpecialS_RollSpeed(fp);
        fp->self_vel.y = 0.0F;
    }
}

/* PlLz SpecialS_End_Enter. */
static void ftLz_SpecialSEnd_Enter(HSD_GObj* gobj)
{
    Fighter_ChangeMotionState(gobj, ftLz_MS_SpecialSEnd, Ft_MF_None, 0.0F, 1.0F, 0.0F, NULL);
    ftLz_SpecialSEnd_OnEnter(gobj);
}

/* PlLz SpecialAirS_End_Enter. */
static void ftLz_SpecialAirSEnd_Enter(HSD_GObj* gobj)
{
    Fighter* fp = GET_FIGHTER(gobj);
    ftLz_DatAttrs* da = ftLz_Attrs(fp);
    Fighter_ChangeMotionState(gobj, ftLz_MS_SpecialAirSEnd, Ft_MF_None, 0.0F, 1.0F, 0.0F,
                              NULL);
    ftLz_SpecialSEnd_OnEnter(gobj);
    fp->self_vel.x *= da->specials_end_vel_mul;
}

/* ---- 353 SpecialSStart ---- */

void ftLz_SpecialSStart_Anim(HSD_GObj* gobj)
{
    if (!ftAnim_IsFramesRemaining(gobj)) {
        ftLz_SpecialS_EnterRoll(gobj);
    }
}

void ftLz_SpecialSStart_IASA(HSD_GObj* gobj) {}

void ftLz_SpecialSStart_Phys(HSD_GObj* gobj)
{
    ft_80084FA8(gobj);
}

void ftLz_SpecialSStart_Coll(HSD_GObj* gobj)
{
    Fighter* fp = GET_FIGHTER(gobj);
    if (ft_80082708(gobj) == GA_Ground) {
        ftCommon_8007D60C(fp);
        ftLz_SpecialS_SwitchState(gobj, ftLz_MS_SpecialAirSStart);
        ftLz_SpecialS_SetHitlagCallbacks(fp);
    }
}

/* ---- 354 SpecialS ---- */

void ftLz_SpecialS_Anim(HSD_GObj* gobj)
{
    Fighter* fp = GET_FIGHTER(gobj);
    ftLz_DatAttrs* da = ftLz_Attrs(fp);
    ftLz_MotionVars* mv = ftLz_MV(fp);

    mv->specials.frames += 1;
    if (mv->specials.frames >= da->specials_frames) {
        /* the end keeps the roll's ground speed */
        float gr_vel = fp->gr_vel;
        ftLz_SpecialSEnd_Enter(gobj);
        fp->gr_vel = gr_vel;
    }
}

void ftLz_SpecialS_IASA(HSD_GObj* gobj) {}

void ftLz_SpecialS_Phys(HSD_GObj* gobj)
{
    Fighter* fp = GET_FIGHTER(gobj);
    ftLz_DatAttrs* da = ftLz_Attrs(fp);

    ftLz_SpecialS_UpdateRotation(gobj);
    if (fp->x594_b0) {
        ft_80084FA8(gobj);
    } else if (fp->cmd_vars[0] != 0) {
        ftCommon_CalcGroundAccel_Deaccel(fp, ftLz_SpecialS_Friction(da, ftLz_MV(fp)));
    }
    ftCommon_SetSelfMovementFromGroundedMovement(gobj);
}

void ftLz_SpecialS_Coll(HSD_GObj* gobj)
{
    Fighter* fp = GET_FIGHTER(gobj);
    if (ft_80082708(gobj) == GA_Ground) {
        ftCommon_8007D60C(fp);
        ftLz_SpecialS_SwitchState(gobj, ftLz_MS_SpecialAirS);
        fp->self_vel.x = fp->gr_vel;
        fp->self_vel.y = fp->xF0_ground_kb_vel;
        fp->self_vel.z = fp->xF4_ground_attacker_shield_kb_vel;
        ftLz_SpecialS_SetHitlagCallbacks(fp);
    }
}

/* ---- 355 SpecialSBlown (unused) ---- */

void ftLz_SpecialSBlown_Anim(HSD_GObj* gobj)
{
    if (!ftAnim_IsFramesRemaining(gobj)) {
        ftCo_80096900(gobj, 1, 0, true, 1.0F, 30.0F);
    }
}

void ftLz_SpecialSBlown_IASA(HSD_GObj* gobj) {}

void ftLz_SpecialSBlown_Phys(HSD_GObj* gobj) {}

void ftLz_SpecialSBlown_Coll(HSD_GObj* gobj) {}

/* ---- 356 SpecialSEnd ---- */

void ftLz_SpecialSEnd_Anim(HSD_GObj* gobj)
{
    if (!ftAnim_IsFramesRemaining(gobj)) {
        ft_8008A2BC(gobj);
    }
}

void ftLz_SpecialSEnd_IASA(HSD_GObj* gobj) {}

void ftLz_SpecialSEnd_Phys(HSD_GObj* gobj)
{
    Fighter* fp = GET_FIGHTER(gobj);

    if (fp->cmd_vars[0] != 0) {
        ftCommon_CalcGroundAccel_Deaccel(fp, ftLz_Attrs(fp)->specials_end_friction);
    }
    ftCommon_SetSelfMovementFromGroundedMovement(gobj);
    if (fp->cmd_vars[2] != 0) {
        ftPartSetRotZ(fp, 0, 0.0F);
    } else {
        ftLz_SpecialS_UpdateRotation(gobj);
    }
}

void ftLz_SpecialSEnd_Coll(HSD_GObj* gobj)
{
    Fighter* fp = GET_FIGHTER(gobj);
    ftLz_DatAttrs* da = ftLz_Attrs(fp);

    if (fp->cmd_vars[2] == 0 && ft_80082708(gobj) == GA_Ground) {
        ftCommon_8007D60C(fp);
        ftLz_SpecialS_SwitchState(gobj, ftLz_MS_SpecialAirSEnd);
        fp->self_vel.x *= da->specials_end_vel_mul;
        ftLz_SpecialS_SetHitlagCallbacks(fp);
    } else if (!ft_800827A0(gobj)) {
        ftCo_Fall_Enter(gobj);
        ftCommon_8007E2FC(gobj);
    }
}

/* ---- 357 SpecialAirSStart ---- */

void ftLz_SpecialAirSStart_Anim(HSD_GObj* gobj)
{
    if (!ftAnim_IsFramesRemaining(gobj)) {
        ftLz_SpecialAirS_EnterRoll(gobj);
    }
}

void ftLz_SpecialAirSStart_IASA(HSD_GObj* gobj) {}

void ftLz_SpecialAirSStart_Phys(HSD_GObj* gobj)
{
    Fighter* fp = GET_FIGHTER(gobj);
    ftLz_DatAttrs* da = ftLz_Attrs(fp);
    ftCommon_Fall(fp, da->specialairs_start_gravity, da->specialairs_start_terminal_vel);
}

void ftLz_SpecialAirSStart_Coll(HSD_GObj* gobj)
{
    Fighter* fp = GET_FIGHTER(gobj);
    if (ft_80081D0C(gobj) != GA_Ground) {
        ftCommon_8007D7FC(fp);
        ftLz_SpecialS_SwitchState(gobj, ftLz_MS_SpecialSStart);
        ftLz_SpecialS_SetHitlagCallbacks(fp);
    }
}

/* ---- 358 SpecialAirS ---- */

void ftLz_SpecialAirS_Anim(HSD_GObj* gobj)
{
    Fighter* fp = GET_FIGHTER(gobj);
    ftLz_DatAttrs* da = ftLz_Attrs(fp);
    ftLz_MotionVars* mv = ftLz_MV(fp);

    mv->specials.frames += 1;
    if (mv->specials.frames >= da->specials_frames) {
        ftLz_SpecialAirSEnd_Enter(gobj);
    }
}

void ftLz_SpecialAirS_IASA(HSD_GObj* gobj) {}

void ftLz_SpecialAirS_Phys(HSD_GObj* gobj)
{
    Fighter* fp = GET_FIGHTER(gobj);
    ftLz_DatAttrs* da = ftLz_Attrs(fp);

    if (fp->x594_b0) {
        ft_80085134(gobj);
    } else if (fp->cmd_vars[0] != 0) {
        ftCommon_CalcSelfAccel_Deaccel(fp, ftLz_SpecialS_Friction(da, ftLz_MV(fp)));
    }
}

/* The air roll does not land: the collision result is dropped, as on the console. */
void ftLz_SpecialAirS_Coll(HSD_GObj* gobj)
{
    ft_80081D0C(gobj);
}

/* ---- 359 SpecialAirSBlown (unused) ---- */

void ftLz_SpecialAirSBlown_Anim(HSD_GObj* gobj)
{
    if (!ftAnim_IsFramesRemaining(gobj)) {
        ftCo_80096900(gobj, 1, 0, true, 1.0F, 30.0F);
    }
}

void ftLz_SpecialAirSBlown_IASA(HSD_GObj* gobj) {}

void ftLz_SpecialAirSBlown_Phys(HSD_GObj* gobj) {}

void ftLz_SpecialAirSBlown_Coll(HSD_GObj* gobj) {}

/* ---- 360 SpecialAirSEnd ---- */

void ftLz_SpecialAirSEnd_Anim(HSD_GObj* gobj)
{
    Fighter* fp = GET_FIGHTER(gobj);
    ftLz_DatAttrs* da = ftLz_Attrs(fp);
    if (!ftAnim_IsFramesRemaining(gobj)) {
        ftCo_80096900(gobj, 1, 0, true, 1.0F, da->specialairs_end_landing_lag);
    }
}

void ftLz_SpecialAirSEnd_IASA(HSD_GObj* gobj) {}

void ftLz_SpecialAirSEnd_Phys(HSD_GObj* gobj)
{
    Fighter* fp = GET_FIGHTER(gobj);
    ftLz_DatAttrs* da = ftLz_Attrs(fp);

    if (fp->cmd_vars[0] != 0) {
        ftCommon_CalcSelfAccel_Deaccel(fp, da->specialairs_end_friction);
    }
    ftCommon_Fall(fp, da->specialairs_end_gravity, fp->co_attrs.terminal_velocity);
}

void ftLz_SpecialAirSEnd_Coll(HSD_GObj* gobj)
{
    Fighter* fp = GET_FIGHTER(gobj);
    bool landed;

    if (fp->cmd_vars[1] != 0) {
        landed = ft_CheckGroundAndLedge(gobj, fp->facing_dir > 0.0F ? 1 : -1);
    } else {
        landed = ft_80081D0C(gobj) != GA_Ground;
    }
    if (landed) {
        ftLz_DatAttrs* da = ftLz_Attrs(fp);
        ftCommon_8007D7FC(fp);
        ftCo_LandingFallSpecial_Enter(gobj, false, da->specialairs_end_landing_lag);
        fp->gr_vel = fp->self_vel.x;
        fp->xF0_ground_kb_vel = fp->self_vel.y;
        fp->xF4_ground_attacker_shield_kb_vel = fp->self_vel.z;
    } else {
        ftCliffCommon_80081298(gobj);
    }
}
