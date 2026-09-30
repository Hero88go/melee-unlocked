/* Akaneia's Tails, native: up special (flight).
 *
 * Start (ground or air) -> Loop: Tails flies on a fuel budget (fv.fuel, refilled on landing).
 * Every frame of lift costs fuel; each B press is a flap that gives a burst of lift, restarts the
 * ascent timer and costs extra fuel. Holding B keeps some lift after the ascent runs out; letting
 * go makes him glide down. The stick turns him around, with the model turning smoothly. Empty
 * tank -> Exhaust (tired fall). Getting hit leaves only hi_fuel_on_hit fuel. */
#include "ftTs.h"

#include <math.h>

#include <melee/ft/fighter.h>
#include <melee/ft/ft_081B.h>
#include <melee/ft/ft_084E.h>
#include <melee/ft/ftanim.h>
#include <melee/ft/ftcliffcommon.h>
#include <melee/ft/ftcommon.h>
#include <melee/ft/ftparts.h>
#include <melee/ft/inlines.h>
#include <melee/ft/kinds/ftCommon/ftCo_FallSpecial.h>
#include <melee/ft/kinds/ftCommon/ftCo_Landing.h>
#include <melee/ft/types.h>

#define TS_HALF_PI 1.5707963267948966

/* code+0x54B0 SpecialHi_OnHit: take_dmg / death2 callback while flying. */
static void ftTs_SpecialHi_OnHit(Fighter_GObj* gobj)
{
    Fighter* fp = GET_FIGHTER(gobj);

    ftTs_Vars(fp)->fuel = ftTs_Attrs(fp)->hi_fuel_on_hit;
}

static inline void ftTs_SpecialHi_SetHitCallbacks(Fighter* fp)
{
    fp->take_dmg_cb = ftTs_SpecialHi_OnHit;
    fp->death2_cb = ftTs_SpecialHi_OnHit;
}

/* code+0x3094 SpecialHi_EnterAirOrGround (both the ground and the air export) */
void ftTs_SpecialHi_EnterAirOrGround(Fighter_GObj* gobj)
{
    Fighter* fp = GET_FIGHTER(gobj);

    if (fp->ground_or_air == GA_Air) {
        Fighter_ChangeMotionState(gobj, ftTs_MS_SpecialAirHiStart, Ft_MF_None, 0.0f, 1.0f, 0.0f,
                                  NULL);
    } else {
        Fighter_ChangeMotionState(gobj, ftTs_MS_SpecialHiStart, Ft_MF_None, 0.0f, 1.0f, 0.0f,
                                  NULL);
    }
    ftCommon_8007E2FC(gobj);
    ftTs_SpecialHi_SetHitCallbacks(fp);
    fp->cmd_vars[0] = 0;
    fp->cmd_vars[1] = 0;
}

/* code+0x4E70 Fighter_IASACheck_UpSpecial: the up-B part of ftCo_SpecialAir_CheckInput, used
 * out of the spin dash jump. */
bool ftTs_Fighter_IASACheck_UpSpecial(Fighter_GObj* gobj)
{
    Fighter* fp = GET_FIGHTER(gobj);

    if (!(fp->input.pressed_buttons & HSD_PAD_B)) {
        return false;
    }
    if (!(fp->input.lstick[0].y >= p_ftCommonData->x21C)) {
        return false;
    }
    ftTs_SpecialHi_EnterAirOrGround(gobj);
    fp->x2227_b5 = true;
    return true;
}

/* code+0x419C SpecialHi_Loop_Enter */
static void ftTs_SpecialHi_Loop_Enter(Fighter_GObj* gobj)
{
    Fighter* fp = GET_FIGHTER(gobj);
    ftTails_DatAttrs* da = ftTs_Attrs(fp);
    ftTails_SpecialHiVars* mv = ftTs_HiVars(fp);
    ftTails_FighterVars* fv = ftTs_Vars(fp);
    float rot;

    ftCommon_8007D5D4(fp);
    Fighter_ChangeMotionState(gobj, ftTs_MS_SpecialHiLoop, Ft_MF_None, 0.0f, 1.0f, 0.0f, NULL);
    ftCommon_8007E2FC(gobj);

    rot = (float) (fp->facing_dir * TS_HALF_PI);
    mv->target_rot = rot;
    mv->rot = rot;
    mv->ascent = da->hi_ascent_frames;
    ftTs_SpecialHi_SetHitCallbacks(fp);
    if (fv->fuel < da->hi_min_fuel) {
        fv->fuel = da->hi_min_fuel;
    }
}

/* code+0x425C SpecialAirHi_Start_Trans */
static void ftTs_SpecialAirHi_Start_Trans(Fighter_GObj* gobj)
{
    Fighter* fp = GET_FIGHTER(gobj);

    ftCommon_8007D5D4(fp);
    Fighter_ChangeMotionState(gobj, ftTs_MS_SpecialAirHiStart, Ft_MF_None, fp->cur_anim_frame,
                              1.0f, 0.0f, NULL);
    ftTs_SpecialHi_SetHitCallbacks(fp);
}

/* code+0x42D0 SpecialHi_Start_Trans */
static void ftTs_SpecialHi_Start_Trans(Fighter_GObj* gobj)
{
    Fighter* fp = GET_FIGHTER(gobj);

    ftCommon_8007D7FC(fp);
    Fighter_ChangeMotionState(gobj, ftTs_MS_SpecialHiStart, Ft_MF_None, fp->cur_anim_frame, 1.0f,
                              0.0f, NULL);
    ftTs_SpecialHi_SetHitCallbacks(fp);
}

/* code+0x4344 SpecialHi_RotateToFacingDirection: turn with the stick, easing the model's y
 * rotation toward +-pi/2 by hi_turn_step a frame. */
static void ftTs_SpecialHi_RotateToFacingDirection(Fighter_GObj* gobj)
{
    Fighter* fp = GET_FIGHTER(gobj);
    ftTails_DatAttrs* da = ftTs_Attrs(fp);
    ftTails_SpecialHiVars* mv = ftTs_HiVars(fp);
    float next;

    if (fp->facing_dir == 1.0f) {
        if (fp->input.lstick[0].x < -0.5f) {
            fp->facing_dir = -1.0f;
            mv->target_rot = -1.5707964f;
        }
    } else if (fp->facing_dir == -1.0f) {
        if (fp->input.lstick[0].x > 0.5f) {
            fp->facing_dir = 1.0f;
            mv->target_rot = 1.5707964f;
        }
    }

    if (mv->rot > mv->target_rot) {
        next = mv->rot - da->hi_turn_step;
        if (!(mv->target_rot < next)) {
            next = mv->target_rot;
        }
        mv->rot = next;
        ftPartSetRotY(fp, 0, next);
    } else if (mv->rot < mv->target_rot) {
        next = mv->rot + da->hi_turn_step;
        if (!(mv->target_rot > next)) {
            next = mv->target_rot;
        }
        mv->rot = next;
        ftPartSetRotY(fp, 0, next);
    }
}

/* code+0x4464 SpecialHi_Exhaust_Enter: out of fuel. The tired animation starts at a frame
 * proportional to where the flight loop was. */
static void ftTs_SpecialHi_Exhaust_Enter(Fighter_GObj* gobj)
{
    Fighter* fp = GET_FIGHTER(gobj);
    ftTails_DatAttrs* da = ftTs_Attrs(fp);
    int frame;

    ftCommon_8007D5D4(fp);
    frame = (int) (fp->cur_anim_frame / da->exhaust_frame_div * da->exhaust_frame_mul);
    Fighter_ChangeMotionState(gobj, ftTs_MS_SpecialHiExhaust, Ft_MF_None, (float) frame, 1.0f,
                              4.0f, NULL);
    ftTs_SpecialHi_SetHitCallbacks(fp);
}

/* ------------------------------------------------------------------------------------------------
 * Start (343 ground, 344 air)
 * --------------------------------------------------------------------------------------------- */

/* code+0x1478 */
void ftTs_SpecialHi_Start_Anim(Fighter_GObj* gobj)
{
    if (!ftAnim_IsFramesRemaining(gobj)) {
        ftTs_SpecialHi_Loop_Enter(gobj);
    }
}

void ftTs_SpecialHi_Start_IASA(Fighter_GObj* gobj) {}

/* code+0x14C0 */
void ftTs_SpecialHi_Start_Phys(Fighter_GObj* gobj)
{
    ft_80084F3C(gobj);
}

/* code+0x14E0 */
void ftTs_SpecialHi_Start_Coll(Fighter_GObj* gobj)
{
    if (!ft_80082708(gobj)) {
        ftTs_SpecialAirHi_Start_Trans(gobj);
    }
}

/* code+0x1524 */
void ftTs_SpecialAirHi_Start_Anim(Fighter_GObj* gobj)
{
    if (!ftAnim_IsFramesRemaining(gobj)) {
        ftTs_SpecialHi_Loop_Enter(gobj);
    }
}

void ftTs_SpecialAirHi_Start_IASA(Fighter_GObj* gobj) {}

/* code+0x156C: slow fall while winding up. */
void ftTs_SpecialAirHi_Start_Phys(Fighter_GObj* gobj)
{
    Fighter* fp = GET_FIGHTER(gobj);

    ftCommon_CalcSelfAccel_Deaccel(fp, fp->co_attrs.aerial_friction);
    ftCommon_Fall(fp, 0.02f, fp->co_attrs.terminal_velocity);
}

/* code+0x15B4 */
void ftTs_SpecialAirHi_Start_Coll(Fighter_GObj* gobj)
{
    Fighter* fp = GET_FIGHTER(gobj);

    if (ft_CheckGroundAndLedge(gobj, ftGetFacingDirInt(fp))) {
        ftTs_SpecialHi_Start_Trans(gobj);
    } else if (fp->cmd_vars[0] == 1) {
        ftCliffCommon_80081298(gobj);
    }
}

/* ------------------------------------------------------------------------------------------------
 * Loop (345): flying
 * --------------------------------------------------------------------------------------------- */

void ftTs_SpecialHi_Loop_Anim(Fighter_GObj* gobj) {}
void ftTs_SpecialHi_Loop_IASA(Fighter_GObj* gobj) {}

/* code+0x1634 */
void ftTs_SpecialHi_Loop_Phys(Fighter_GObj* gobj)
{
    Fighter* fp = GET_FIGHTER(gobj);
    ftTails_DatAttrs* da = ftTs_Attrs(fp);
    ftTails_SpecialHiVars* mv = ftTs_HiVars(fp);
    ftTails_FighterVars* fv = ftTs_Vars(fp);

    if ((fp->input.held_buttons[0] & HSD_PAD_B) || mv->ascent > 0) {
        fv->fuel -= da->hi_fuel_per_frame;
        if (mv->ascent > 0) {
            fp->self_vel.y += da->hi_ascent_lift;
        } else {
            fp->self_vel.y += da->hi_hold_lift;
        }
        if (fp->input.pressed_buttons & HSD_PAD_B) {
            /* A flap. */
            fp->self_vel.y += da->hi_flap_boost;
            mv->ascent = da->hi_ascent_frames;
            fv->fuel -= da->hi_fuel_per_flap;
        }
        if (fp->self_vel.y > da->hi_max_vel_y) {
            fp->self_vel.y = da->hi_max_vel_y;
        }
        if (mv->ascent > 0) {
            mv->ascent--;
        }
        ftAnim_SetAnimRate(gobj, da->hi_fly_anim_rate);
    } else {
        ftCommon_Fall(fp, da->hi_gravity, da->hi_terminal_vel);
        ftAnim_SetAnimRate(gobj, 1.0f);
    }

    if (fv->fuel <= 0) {
        ftTs_SpecialHi_Exhaust_Enter(gobj);
        return;
    }
    ftCommon_CalcSelfAccel_DriftSimple(fp, 0.0f, da->hi_drift_accel, da->hi_drift_max);
    ftTs_SpecialHi_RotateToFacingDirection(gobj);
}

/* code+0x1798: land with special lag, otherwise look for a ledge. */
void ftTs_SpecialHi_Loop_Coll(Fighter_GObj* gobj)
{
    Fighter* fp = GET_FIGHTER(gobj);
    ftTails_DatAttrs* da = ftTs_Attrs(fp);

    if (ft_CheckGroundAndLedge(gobj, ftGetFacingDirInt(fp))) {
        ftCo_LandingFallSpecial_Enter(gobj, false, ftTs_Frames(da->hi_landing_lag));
    } else {
        ftCliffCommon_80081298(gobj);
    }
}

/* ------------------------------------------------------------------------------------------------
 * Exhaust (346)
 * --------------------------------------------------------------------------------------------- */

void ftTs_SpecialHi_Exhaust_Anim(Fighter_GObj* gobj) {}
void ftTs_SpecialHi_Exhaust_IASA(Fighter_GObj* gobj) {}

/* code+0x1838 */
void ftTs_SpecialHi_Exhaust_Phys(Fighter_GObj* gobj)
{
    Fighter* fp = GET_FIGHTER(gobj);
    ftTails_DatAttrs* da = ftTs_Attrs(fp);

    ftCommon_Fall(fp, da->exhaust_gravity, da->exhaust_terminal_vel);
    ftCommon_CalcSelfAccel_DriftSimple(fp, 0.0f, da->exhaust_drift_accel, da->exhaust_drift_max);
    ftTs_SpecialHi_RotateToFacingDirection(gobj);
}

/* code+0x18A8 */
void ftTs_SpecialHi_Exhaust_Coll(Fighter_GObj* gobj)
{
    ftTs_SpecialHi_Loop_Coll(gobj);
}

/* ------------------------------------------------------------------------------------------------
 * Cancel (347). Nothing in the code enters this state; it is kept so the table matches the disc.
 * cmd_vars[1] is driven by its animation script: 1 = hop (vel.y 2) then 2 = keep falling.
 * --------------------------------------------------------------------------------------------- */

/* code+0x18C8 */
void ftTs_SpecialHi_Cancel_Anim(Fighter_GObj* gobj)
{
    Fighter* fp = GET_FIGHTER(gobj);
    ftTails_DatAttrs* da = ftTs_Attrs(fp);

    if (!ftAnim_IsFramesRemaining(gobj)) {
        ftCo_80096900(gobj, 1, 1, false, da->hi_cancel_fall_mobility,
                      ftTs_Frames(da->hi_landing_lag));
    }
}

void ftTs_SpecialHi_Cancel_IASA(Fighter_GObj* gobj) {}

/* code+0x1954 */
void ftTs_SpecialHi_Cancel_Phys(Fighter_GObj* gobj)
{
    Fighter* fp = GET_FIGHTER(gobj);
    ftTails_DatAttrs* da = ftTs_Attrs(fp);

    switch (fp->cmd_vars[1]) {
    case 1:
        fp->self_vel.y = 2.0f;
        fp->cmd_vars[1] = 2;
        ft_80084EEC(gobj);
        break;
    case 2:
        ft_80084EEC(gobj);
        break;
    default:
        ftCommon_Fall(fp, da->hi_gravity, da->hi_terminal_vel);
        break;
    }
}

/* code+0x19BC */
void ftTs_SpecialHi_Cancel_Coll(Fighter_GObj* gobj)
{
    Fighter* fp = GET_FIGHTER(gobj);

    if (ft_80081D0C(gobj)) {
        ftCo_LandingFallSpecial_Enter(gobj, false, ftTs_Frames(ftTs_Attrs(fp)->hi_landing_lag));
    }
}
