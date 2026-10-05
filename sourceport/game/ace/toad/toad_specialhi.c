/* ACE's Toad: the up special (states 347 and 348), the file's own code.
 *
 * Mario's up special (ft/kinds/ftMario/ftmariospecialhi.c) with Toad's attribute block. Read
 * against Mario's routines; the differences:
 *   - the entries clear only the fourth throw flag (bit 0x10 of fp+2210)
 *   - the interrupt callback takes the size of both angles by comparing with 0.0174533 (the
 *     degree constant, +16E0) where Mario compares with 0, and stores that size, where Mario
 *     stores the signed angle; its arithmetic is single but for the last product. The same
 *     code is in ACE's Metal Mario; kept as the file has it
 *   - the collision callback on the ground calls ft_800827A0 where Mario calls ft_80084104
 * The physics callback of state 347 (+128C) makes Mario's two calls: the retail function is
 * named in the table. */
#include "toad.h"

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

#define FTTD_SPECIALHI_DEG 0.017453292519943295 /* +16E0, a double */
#define FTTD_SPECIALHI_HALF_PI 1.5707964f       /* +15FC */

/* +05A0 (slot 8, specialhi) */
void ftTd_SpecialHi_Enter(HSD_GObj* gobj)
{
    Fighter* fp = GET_FIGHTER(gobj);

    fp->cmd_vars[0] = 0;
    fp->throw_flags_b3 = false;
    Fighter_ChangeMotionState(gobj, ftMr_MS_SpecialHi, Ft_MF_None, 0.0f, 1.0f, 0.0f, NULL);
    ftAnim_8006EBA4(gobj);
}

/* +0608 (slot 9, specialairhi) */
void ftTd_SpecialAirHi_Enter(HSD_GObj* gobj)
{
    Fighter* fp = GET_FIGHTER(gobj);
    ftTd_DatAttrs* sa = ftTd_Attrs(fp);

    fp->cmd_vars[0] = 0;
    fp->self_vel.y = 0.0f;
    fp->throw_flags_b3 = false;
    fp->self_vel.x = fp->self_vel.x * sa->specialairhi_vel_x_mul;
    Fighter_ChangeMotionState(gobj, ftMr_MS_SpecialAirHi, Ft_MF_None, 0.0f, 1.0f, 0.0f, NULL);
    ftAnim_8006EBA4(gobj);
}

/* +10F0 (state 347) and +12FC (state 348, the same code again): animation. */
void ftTd_SpecialHi_Anim(HSD_GObj* gobj)
{
    Fighter* fp = GET_FIGHTER(gobj);
    ftTd_DatAttrs* sa = ftTd_Attrs(fp);

    if (!ftAnim_IsFramesRemaining(gobj)) {
        ftCo_80096900(gobj, 0, 1, false, sa->specialhi_fall_mobility,
                      sa->specialhi_landing_lag);
    }
}

/* +1164 (state 347; state 348 branches to it from +1370): interrupt. */
void ftTd_SpecialHi_IASA(HSD_GObj* gobj)
{
    Fighter* fp = GET_FIGHTER(gobj);
    ftTd_DatAttrs* sa = ftTd_Attrs(fp);
    float stick_x = fp->input.lstick[0].x;
    float lstick_x = stick_x;

    if (stick_x < 0.0f) {
        lstick_x = -stick_x;
    }
    if (fp->cmd_vars[0] == 0 && lstick_x > sa->specialhi_momentum_stick) {
        float range = sa->specialhi_momentum_stick;
        /* +11D8 to +1208: (stick - range) / (1 - range) * angle_diff in single, then the
         * product with the double constant, rounded to single */
        float deg = ((lstick_x - range) / (1.0f - range)) * sa->specialhi_angle_diff;
        float rad = (float) ((double) deg * FTTD_SPECIALHI_DEG);
        float cur = fp->lstick_angle;

        if (!(stick_x <= 0.0f)) {
            rad = -rad;
        }
        if ((double) cur < FTTD_SPECIALHI_DEG) {
            cur = -cur;
        }
        if ((double) rad < FTTD_SPECIALHI_DEG) {
            rad = -rad;
        }
        if (rad > cur) {
            fp->lstick_angle = rad;
        }
    }
    if (!fp->throw_flags_b3) {
        return;
    }
    fp->throw_flags_b3 = false;
    if (sa->specialhi_reverse_stick < lstick_x) {
        /* +1250 */
        ftCommon_UpdateFacing(fp);
        ftPartSetRotY(fp, 0, fp->facing_dir * FTTD_SPECIALHI_HALF_PI);
    }
}

/* +1AD4: the landing of the up special. */
static void ftTd_SpecialHi_Landed(HSD_GObj* gobj)
{
    ftTd_DatAttrs* sa = ftTd_Attrs(GET_FIGHTER(gobj));

    ftCo_LandingFallSpecial_Enter(gobj, false, sa->specialhi_landing_lag);
}

/* +12A4 (state 347) and +140C (state 348, the same code again): collision. */
void ftTd_SpecialHi_Coll(HSD_GObj* gobj)
{
    Fighter* fp = GET_FIGHTER(gobj);

    if (fp->ground_or_air != GA_Air) {
        ft_800827A0(gobj);
        return;
    }
    /* falling (or a speed that does not compare) once the script set cmd_vars[0] */
    if (fp->cmd_vars[0] != 0 && !(fp->self_vel.y >= 0.0f)) {
        ft_800831CC(gobj, ftCo_80096CC8, ftTd_SpecialHi_Landed);
    } else {
        ft_80083B68(gobj);
    }
}

/* +1374: state 348, physics. Retail ftMr_SpecialAirHi_Phys with his attributes. */
void ftTd_SpecialAirHi_Phys(HSD_GObj* gobj)
{
    Fighter* fp = GET_FIGHTER(gobj);
    ftTd_DatAttrs* sa = ftTd_Attrs(fp);

    if (fp->cmd_vars[0] != 0) {
        float mul;

        ft_80085154(gobj);
        mul = sa->specialairhi_vel_mul;
        fp->self_vel.x *= mul;
        fp->self_vel.y *= mul;
        fp->self_vel.z *= mul;
    } else {
        ftCommon_Fall(fp, sa->specialairhi_grav, fp->co_attrs.terminal_velocity);
        ftCommon_CalcSelfAccel_DeaccelQuickAir(fp);
    }
}
