/* ACE's Chun-Li: the up special (states 349 and 352) (m-ex "ftFunction" of PlCn.dat).
 *
 * The shape of Mario's up special (ft/kinds/ftMario/ftmariospecialhi.c): the special fall at
 * the end, the stick tilting the move while command variable 0 is clear, a turn when the
 * script raises the fourth throw flag. Her own numbers and these differences, all kept:
 *  - the tilt reads +0C, which holds the integer 23, as a float (3.2e-44): the tilt it computes
 *    rounds to zero;
 *  - the two "absolute values" compare with 0.0174533 (her degree-to-radian constant), not 0,
 *    and the stored value is the "absolute" one, not the signed tilt;
 *  - the degree-to-radian product is a double product rounded to single, the turn angle a
 *    single product with (float) pi/2;
 *  - in the air the physics adds the simple drift of +50/+58 after the two air steps;
 *  - the collision tests the vertical speed: rising, the floor and ledge test; falling, the
 *    landing test into the special landing. */
#include "chunli.h"

#include <melee/ft/fighter.h>
#include <melee/ft/ft_081B.h>
#include <melee/ft/ft_084E.h>
#include <melee/ft/ftanim.h>
#include <melee/ft/ftcommon.h>
#include <melee/ft/ftparts.h>
#include <melee/ft/inlines.h>
#include <melee/ft/kinds/ftCommon/ftCo_FallSpecial.h>
#include <melee/ft/kinds/ftCommon/ftCo_Landing.h>
#include <melee/ft/types.h>
#include <sysdolphin/baselib/gobj.h>

/* The double constants of the block: +2FB4 (0.0174533, her degree-to-radian factor). */
static const double ftCn_DegToRad = 0.0174533;

/* +05C4, [specialhi]: state 349, its script, airborne. */
void ftCn_SpecialHi_Enter(HSD_GObj* gobj)
{
    Fighter* fp = GET_FIGHTER(gobj);

    Fighter_ChangeMotionState(gobj, ftCn_MS_SpecialHi, Ft_MF_None, 0.0f, 1.0f, 0.0f, NULL);
    ftAnim_8006EBA4(gobj);
    ftCommon_8007D5D4(fp);
}

/* +062C, [specialairhi]: state 352, its script. */
void ftCn_SpecialAirHi_Enter(HSD_GObj* gobj)
{
    Fighter_ChangeMotionState(gobj, ftCn_MS_SpecialAirHi, Ft_MF_None, 0.0f, 1.0f, 0.0f, NULL);
    ftAnim_8006EBA4(gobj);
}

/* +1BF8, the animation callback of 349 (and +1E54 of 352, the same instructions): the special
 * fall with the attributes' mobility and landing lag. */
void ftCn_SpecialHi_Anim(HSD_GObj* gobj)
{
    Fighter* fp = GET_FIGHTER(gobj);
    ftCn_DatAttrs* da = ftCn_Attrs(fp);

    if (!ftAnim_IsFramesRemaining(gobj)) {
        ftCo_80096900(gobj, 1, 0, false, da->specialhi_freefall_mobility,
                      da->specialhi_landing_lag);
    }
}

/* +1C70, the interrupt callback of 349 (and of 352 and 357: +1ECC and +20BC are jumps here). */
MU_NO_CONTRACT void ftCn_SpecialHi_IASA(HSD_GObj* gobj)
{
    Fighter* fp = GET_FIGHTER(gobj);
    ftCn_DatAttrs* da = ftCn_Attrs(fp);
    float stick_x = fp->input.lstick[0].x;
    float stick_abs = stick_x >= 0.0f ? stick_x : -stick_x;

    if (fp->cmd_vars[0] == 0) {
        float range = da->specialhi_momentum_stick_range;

        if (stick_abs > range) {
            /* +1D08 */
            float num = stick_abs - range;
            float den = 1.0f - range;
            float diff = da->specialhi_angle_diff;
            float rad;
            float old;

            rad = num / den;
            rad = rad * diff;
            rad = (float) ((double) rad * ftCn_DegToRad);
            if (!(stick_x <= 0.0f)) {
                rad = -rad;
            }
            old = fp->lstick_angle;
            if (!((double) old >= ftCn_DegToRad)) {
                old = -old;
            }
            if (!((double) rad >= ftCn_DegToRad)) {
                rad = -rad;
            }
            if (rad > old) {
                fp->lstick_angle = rad;
            }
        }
    }

    /* +1CAC */
    if (!fp->throw_flags_b3) {
        return;
    }
    fp->throw_flags_b3 = false;
    if (!(da->specialhi_reverse_stick_range < stick_abs)) {
        return;
    }
    ftCommon_UpdateFacing(fp);
    ftPartSetRotY(fp, 0, fp->facing_dir * 1.5707964f);
}

/* +1D90, the physics callback of 349 (and +1ED0 of 352, the same instructions). In the air the
 * attributes are read before the two air steps, then the simple drift and the drift. */
void ftCn_SpecialHi_Phys(HSD_GObj* gobj)
{
    Fighter* fp = GET_FIGHTER(gobj);
    ftCn_DatAttrs* da;

    if (fp->ground_or_air != GA_Air) {
        ft_80084FA8(gobj);
        return;
    }
    da = ftCn_Attrs(fp);
    ft_80085154(gobj);
    ft_800851C0(gobj);
    ftCommon_CalcSelfAccel_DriftSimple(fp, 0.0f, da->specialhi_drift_accel,
                                       da->specialhi_drift_max);
    ftCommon_CalcSelfAccel_Drift(fp);
}

/* +2FC4 (and +2FD8, the same instructions): the landing callback of the two collision
 * callbacks, the special landing with the attributes' lag. */
static void ftCn_SpecialHi_OnLand(HSD_GObj* gobj)
{
    Fighter* fp = GET_FIGHTER(gobj);

    ftCo_LandingFallSpecial_Enter(gobj, false, ftCn_Attrs(fp)->specialhi_landing_lag);
}

/* +1E0C, the collision callback of 349: rising (or a NaN speed), the floor and ledge test with
 * the facing as the direction (its result unused); falling, the landing test. */
void ftCn_SpecialHi_Coll(HSD_GObj* gobj)
{
    Fighter* fp = GET_FIGHTER(gobj);

    if (fp->self_vel.y < 0.0f) {
        ft_800831CC(gobj, NULL, ftCn_SpecialHi_OnLand);
        return;
    }
    ft_CheckGroundAndLedge(gobj, (int) fp->facing_dir);
}

/* +1F4C, the collision callback of 352: the same as +1E0C with the landing callback +2FD8. */
void ftCn_SpecialAirHi_Coll(HSD_GObj* gobj)
{
    Fighter* fp = GET_FIGHTER(gobj);

    if (fp->self_vel.y < 0.0f) {
        ft_800831CC(gobj, NULL, ftCn_SpecialHi_OnLand);
        return;
    }
    ft_CheckGroundAndLedge(gobj, (int) fp->facing_dir);
}
