/* Akaneia's Wolf: side special, Wolf Flash.
 *
 * A wind-up (347 on the ground, 350 in the air), then a dash that always goes airborne (351, which
 * the stick can angle up or down by up to 10 degrees and B cuts short), then the slash (352). The
 * slash ends in special fall unless it hit something. State 348 keeps Fox's Illusion callbacks and
 * 349 is Wolf's own, but none of Wolf's transitions lead to either. */
#include "wolf.h"

#include <melee/ft/forward.h>

#include <melee/ft/fighter.h>
#include <melee/ft/ft_081B.h>
#include <melee/ft/ft_084E.h>
#include <melee/ft/ft_0892.h>
#include <melee/ft/ftanim.h>
#include <melee/ft/ftcliffcommon.h>
#include <melee/ft/ftcommon.h>
#include <melee/ft/inlines.h>
#include <melee/ft/kinds/ftCommon/ftCo_Fall.h>
#include <melee/ft/kinds/ftCommon/ftCo_FallSpecial.h>
#include <melee/ft/kinds/ftCommon/ftCo_Landing.h>
#include <melee/ft/types.h>
#include <sysdolphin/baselib/controller.h>

#define DEG_TO_RAD 0.017453292519943295

/* The dash's stick range: past half tilt, up to this many degrees either way. */
#define FTWF_FLASH_MAX_ANGLE 10.0f

/* [SpecialS]: the specials slot. */
void ftWf_SpecialS_Enter(HSD_GObj* gobj)
{
    Fighter* fp = GET_FIGHTER(gobj);
    ftWolf_DatAttrs* da = ftWf_Attrs(fp);

    Fighter_ChangeMotionState(gobj, ftWf_MS_SpecialSStart, 0, 0.0f, 1.0f, 0.0f, NULL);
    ftAnim_8006EBA4(gobj);

    fp->x1968_jumpsUsed = fp->co_attrs.max_jumps;
    fp->gr_vel /= da->x28_FLASH_START_VEL_DIV;
    ftWf_MV(fp)->SpecialS.gravity_delay = da->x24_FLASH_GRAVITY_DELAY;
    ftWf_MV(fp)->SpecialS.x4 = 0.0f;
}

/* [SpecialAirS]: the specialairs slot. */
void ftWf_SpecialAirS_Enter(HSD_GObj* gobj)
{
    Fighter* fp = GET_FIGHTER(gobj);
    ftWolf_DatAttrs* da = ftWf_Attrs(fp);

    Fighter_ChangeMotionState(gobj, ftWf_MS_SpecialAirSStart, 0, 0.0f, 1.0f, 0.0f, NULL);
    ftAnim_8006EBA4(gobj);

    fp->x1968_jumpsUsed = fp->co_attrs.max_jumps;
    fp->self_vel.x /= da->x28_FLASH_START_VEL_DIV;
    fp->self_vel.y = 0.0f;
    ftWf_MV(fp)->SpecialS.gravity_delay = da->x24_FLASH_GRAVITY_DELAY;
    ftWf_MV(fp)->SpecialS.x4 = 0.0f;
}

/* [SpecialAirSEnd_OnHit]: the slash's deal_dmg_cb. A hit gives back one jump and skips special
 * fall. */
static void ftWf_SpecialAirSEnd_OnHit(HSD_GObj* gobj)
{
    Fighter* fp = GET_FIGHTER(gobj);

    ftWf_MV(fp)->SpecialS.has_hit = true;
    fp->x1968_jumpsUsed = 1;
}

/* [SpecialAirSMid]: the dash (351), from either wind-up. The stick's vertical tilt past one half
 * angles the dash by up to ten degrees; ft_80085154 then turns the animation's own movement by
 * fp->lstick_angle. */
static void ftWf_SpecialAirS_EnterDash(HSD_GObj* gobj)
{
    Fighter* fp = GET_FIGHTER(gobj);
    float stick_y;
    float sign = 1.0f;

    Fighter_ChangeMotionState(gobj, ftWf_MS_SpecialAirS, 0, 0.0f, 1.0f, 0.0f, NULL);

    stick_y = fp->input.lstick[0].y;
    if (stick_y < 0.0f) {
        stick_y = -stick_y;
        sign = -1.0f;
    }

    if (stick_y >= 0.5f) {
        fp->lstick_angle = (float) (((stick_y - 0.5f) * 2.0f * sign * FTWF_FLASH_MAX_ANGLE) *
                                    DEG_TO_RAD * fp->facing_dir);
    } else {
        fp->lstick_angle = 0.0f;
    }
}

/* [SpecialAirSEnd]: the slash (352), from the dash. */
static void ftWf_SpecialAirSEnd_Enter(HSD_GObj* gobj)
{
    Fighter* fp = GET_FIGHTER(gobj);
    ftWolf_DatAttrs* da = ftWf_Attrs(fp);

    Fighter_ChangeMotionState(gobj, ftWf_MS_SpecialAirSEnd, 0, 0.0f, 1.0f, 0.0f, NULL);
    ftAnim_8006EBA4(gobj);

    fp->self_vel.x = fp->facing_dir * da->x3C_FLASH_END_VEL_X;
    fp->self_vel.y = 0.0f;
    ftWf_MV(fp)->SpecialS.gravity_delay = da->x44_FLASH_END_GRAVITY_DELAY;
    ftWf_MV(fp)->SpecialS.has_hit = false;
    fp->deal_dmg_cb = ftWf_SpecialAirSEnd_OnHit;
}

/* [SpecialAirS_Trans]: ground wind-up to air wind-up. */
static void ftWf_SpecialSStart_GroundToAir(HSD_GObj* gobj)
{
    Fighter* fp = GET_FIGHTER(gobj);

    Fighter_ChangeMotionState(gobj, ftWf_MS_SpecialAirSStart, Ft_MF_KeepGfx, fp->cur_anim_frame,
                              1.0f, 0.0f, NULL);
    ftCommon_8007D5D4(fp);
}

/* [SpecialS_Trans]: air wind-up to ground wind-up. */
static void ftWf_SpecialAirSStart_AirToGround(HSD_GObj* gobj)
{
    Fighter* fp = GET_FIGHTER(gobj);

    Fighter_ChangeMotionState(gobj, ftWf_MS_SpecialSStart, Ft_MF_KeepGfx, fp->cur_anim_frame,
                              1.0f, 0.0f, NULL);
    ftCommon_8007D6A4(fp);
}

/* ---- 347: ground wind-up [SpecialS] ---- */

/* The dash is an air state, so the ground wind-up leaves the ground when it ends. */
void ftWf_SpecialSStart_Anim(HSD_GObj* gobj)
{
    Fighter* fp = GET_FIGHTER(gobj);

    if (!ftAnim_IsFramesRemaining(gobj)) {
        ftWf_SpecialAirS_EnterDash(gobj);
        ftCommon_8007D5D4(fp);
    }
}

void ftWf_SpecialSStart_IASA(HSD_GObj* gobj) {}

void ftWf_SpecialSStart_Phys(HSD_GObj* gobj)
{
    Fighter* fp = GET_FIGHTER(gobj);

    if (ftWf_MV(fp)->SpecialS.gravity_delay > 0) {
        ftWf_MV(fp)->SpecialS.gravity_delay--;
    }
    ft_80084F3C(gobj);
}

void ftWf_SpecialSStart_Coll(HSD_GObj* gobj)
{
    if (!ft_80082708(gobj)) {
        ftWf_SpecialSStart_GroundToAir(gobj);
    }
}

/* ---- 349: [SpecialSEnd], not entered by Wolf's own code ---- */

void ftWf_SpecialSEnd_Anim(HSD_GObj* gobj)
{
    if (!ftAnim_IsFramesRemaining(gobj)) {
        ft_8008A2BC(gobj);
    }
}

void ftWf_SpecialSEnd_IASA(HSD_GObj* gobj) {}

void ftWf_SpecialSEnd_Phys(HSD_GObj* gobj)
{
    Fighter* fp = GET_FIGHTER(gobj);

    if (ftWf_MV(fp)->SpecialS.gravity_delay > 0) {
        ftWf_MV(fp)->SpecialS.gravity_delay--;
    }
    ft_80084F3C(gobj);
    ftCommon_SetSelfMovementFromGroundedMovement(gobj);
}

void ftWf_SpecialSEnd_Coll(HSD_GObj* gobj)
{
    if (!ft_80082708(gobj)) {
        ftCo_Fall_Enter(gobj);
    }
}

/* ---- 350: air wind-up [SpecialAirS] ---- */

void ftWf_SpecialAirSStart_Anim(HSD_GObj* gobj)
{
    if (!ftAnim_IsFramesRemaining(gobj)) {
        ftWf_SpecialAirS_EnterDash(gobj);
    }
}

void ftWf_SpecialAirSStart_IASA(HSD_GObj* gobj) {}

/* Note the fall's speed cap: m-ex passes attribute +0x60 (Fire Wolf's fall acceleration, 0.015)
 * where every other Wolf state passes fp->co_attrs.terminal_velocity. Kept as Akaneia plays. */
void ftWf_SpecialAirSStart_Phys(HSD_GObj* gobj)
{
    Fighter* fp = GET_FIGHTER(gobj);
    ftWolf_DatAttrs* da = ftWf_Attrs(fp);

    if (ftWf_MV(fp)->SpecialS.gravity_delay > 0) {
        ftWf_MV(fp)->SpecialS.gravity_delay--;
    } else {
        ftCommon_Fall(fp, da->x30_FLASH_START_FALL_ACCEL, da->x60_FIREWOLF_FALL_ACCEL);
    }
    ftCommon_CalcSelfAccel_Deaccel(fp, da->x2C_FLASH_START_DECEL_X);
}

void ftWf_SpecialAirSStart_Coll(HSD_GObj* gobj)
{
    Fighter* fp = GET_FIGHTER(gobj);

    if (ft_CheckGroundAndLedge(gobj, (int) fp->facing_dir)) {
        ftWf_SpecialAirSStart_AirToGround(gobj);
        return;
    }
    ftCliffCommon_80081298(gobj);
}

/* ---- 351: the dash [SpecialAirSMid] ---- */

void ftWf_SpecialAirS_Anim(HSD_GObj* gobj)
{
    if (!ftAnim_IsFramesRemaining(gobj)) {
        ftWf_SpecialAirSEnd_Enter(gobj);
    }
}

/* B slashes early. */
void ftWf_SpecialAirS_IASA(HSD_GObj* gobj)
{
    Fighter* fp = GET_FIGHTER(gobj);

    if (fp->input.pressed_buttons & HSD_PAD_B) {
        ftWf_SpecialAirSEnd_Enter(gobj);
    }
}

void ftWf_SpecialAirS_Phys(HSD_GObj* gobj)
{
    ft_80085154(gobj);
}

/* Touching ground does not end the dash: only the ledge is checked while airborne. */
void ftWf_SpecialAirS_Coll(HSD_GObj* gobj)
{
    Fighter* fp = GET_FIGHTER(gobj);

    if (!ft_CheckGroundAndLedge(gobj, (int) fp->facing_dir)) {
        ftCliffCommon_80081298(gobj);
    }
}

/* ---- 352: the slash [SpecialAirSEnd] ---- */

void ftWf_SpecialAirSEnd_Anim(HSD_GObj* gobj)
{
    Fighter* fp = GET_FIGHTER(gobj);
    ftWolf_DatAttrs* da = ftWf_Attrs(fp);

    if (ftAnim_IsFramesRemaining(gobj)) {
        return;
    }
    if (!ftWf_MV(fp)->SpecialS.has_hit) {
        ftCo_80096900(gobj, 1, 0, true, da->x4C_FLASH_FREEFALL_MOBILITY, da->x50_FLASH_LANDING_LAG);
    } else {
        ftCo_Fall_Enter(gobj);
    }
}

void ftWf_SpecialAirSEnd_IASA(HSD_GObj* gobj) {}

void ftWf_SpecialAirSEnd_Phys(HSD_GObj* gobj)
{
    Fighter* fp = GET_FIGHTER(gobj);
    ftWolf_DatAttrs* da = ftWf_Attrs(fp);

    if (ftWf_MV(fp)->SpecialS.gravity_delay > 0) {
        ftWf_MV(fp)->SpecialS.gravity_delay--;
    } else {
        ftCommon_Fall(fp, da->x48_FLASH_END_FALL_ACCEL, fp->co_attrs.terminal_velocity);
    }
    ftCommon_CalcSelfAccel_Deaccel(fp, da->x40_FLASH_END_DECEL_X);
}

void ftWf_SpecialAirSEnd_Coll(HSD_GObj* gobj)
{
    Fighter* fp = GET_FIGHTER(gobj);
    ftWolf_DatAttrs* da = ftWf_Attrs(fp);

    if (ft_CheckGroundAndLedge(gobj, (int) fp->facing_dir)) {
        ftCo_LandingFallSpecial_Enter(gobj, false, da->x50_FLASH_LANDING_LAG);
        return;
    }
    ftCliffCommon_80081298(gobj);
}
