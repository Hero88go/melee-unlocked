/* ACE's Metal Mario: the up special (m-ex "ftFunction" of PlMM.dat, slots 8 and 9, the state
 * callbacks of his own and the dive state 351).
 *
 * The routines are Mario's (ft/kinds/ftMario/ftmariospecialhi.c) with these changes: at the end
 * of the animation, with B held, he goes into his own state 351 (straight down at 4 a frame with
 * his own effect); the stick angle test of the interrupt callback compares with another
 * constant and stores an unsigned angle; on the ground the collision callback does not fall.
 * The physics callbacks of states 347 and 348 are Mario's own functions, named in the move table
 * (metalmario.c): the file's copies at code+0x878 and +0x9AC make the same calls. */
#include "metalmario.h"

#include <melee/ef/efsync.h>
#include <melee/ft/fighter.h>
#include <melee/ft/ft_081B.h>
#include <melee/ft/ftanim.h>
#include <melee/ft/ftcommon.h>
#include <melee/ft/ftparts.h>
#include <melee/ft/inlines.h>
#include <melee/ft/kinds/ftCommon/ftCo_FallSpecial.h>
#include <melee/ft/kinds/ftMario/ftmariospecialhi.h>
#include <melee/ft/types.h>
#include <sysdolphin/baselib/controller.h>
#include <sysdolphin/baselib/gobj.h>

/* code+0xE08, a double: 0.0174533. Mario's routine multiplies degrees by pi / 180 and compares
 * the two angles with 0 to take their size. This file multiplies by this constant and also
 * compares with it (code+0x834 and +0x84C load the same word). */
#define FTMM_SPECIALHI_DEG 0.0174533

/* code+0xB04, a single: 1.5707964 (pi / 2 rounded to single). */
#define FTMM_SPECIALHI_TURN 1.5707964f

/* The effect of the dive: m-ex id 6014 of the effect file MxDt gives him (EfMrData.dat). */
#define ftMM_Gfx_SpecialHiDive 6014

/* code+0x324, [specialhi]. Same as retail ftMr_SpecialHi_Enter, except that only the fourth
 * throw flag is cleared (Mario clears the whole word). */
void ftMM_SpecialHi_Enter(HSD_GObj* gobj)
{
    Fighter* fp = GET_FIGHTER(gobj);

    fp->cmd_vars[0] = 0;
    fp->throw_flags_b3 = false;
    Fighter_ChangeMotionState(gobj, ftMr_MS_SpecialHi, 0, 0.0f, 1.0f, 0.0f,
                              NULL);
    ftAnim_8006EBA4(gobj);
}

/* code+0x38C, [specialairhi]. Same as retail ftMr_SpecialAirHi_Enter, with the same
 * difference. */
void ftMM_SpecialAirHi_Enter(HSD_GObj* gobj)
{
    Fighter* fp = GET_FIGHTER(gobj);
    ftMario_DatAttrs* sa = fp->dat_attrs;

    fp->cmd_vars[0] = 0;
    fp->throw_flags_b3 = false;
    fp->self_vel.y = 0.0f;
    fp->self_vel.x = fp->self_vel.x * sa->specialhi.vel_x;
    Fighter_ChangeMotionState(gobj, ftMr_MS_SpecialAirHi, 0, 0.0f, 1.0f, 0.0f,
                              NULL);
    ftAnim_8006EBA4(gobj);
}

/* code+0x698 (tail at +0x6E8), the animation callback of states 347 and 351, and its copy at
 * code+0x8E8 (tail at +0x938) for state 348. Retail ftMr_SpecialHi_Anim, then: with B held at
 * that moment, the dive state in place of the special fall just entered. */
void ftMM_SpecialHi_Anim(HSD_GObj* gobj)
{
    Fighter* fp = GET_FIGHTER(gobj);
    ftMario_DatAttrs* sa = fp->dat_attrs;

    if (!ftAnim_IsFramesRemaining(gobj)) {
        ftCo_80096900(gobj, 0, 1, 0, sa->specialhi.freefall_mobility,
                      sa->specialhi.landing_lag);
        if (fp->input.held_buttons[0] & HSD_PAD_B) {
            Fighter_ChangeMotionState(gobj, ftMM_MS_SpecialHiDive, 0, 0.0f, 1.0f,
                                      0.0f, NULL);
            ftAnim_8006EBA4(gobj);
        }
    }
}

/* code+0x758, the interrupt callback of states 347 and 351 (348 jumps to it from code+0x9A8).
 * Retail ftMr_SpecialHi_IASA in single precision, with two differences that are kept as the
 * file has them: the size of each angle is taken by comparing with 0.0174533 where Mario
 * compares with 0, and the angle stored is that size, where Mario stores the signed angle. */
void ftMM_SpecialHi_IASA(HSD_GObj* gobj)
{
    Fighter* fp = GET_FIGHTER(gobj);
    ftMario_DatAttrs* sa = fp->dat_attrs;
    float stick_x = fp->input.lstick[0].x;
    float lstick_x = stick_x;

    if (stick_x < 0.0f) {
        lstick_x = -stick_x;
    }
    if (fp->cmd_vars[0] == 0 && lstick_x > sa->specialhi.momentum_stick_range)
    {
        float range = sa->specialhi.momentum_stick_range;
        /* code+0x7F4 to +0x824: (stick - range) / (1 - range) * angle_diff in single, then
         * the product with the double constant, rounded to single */
        float deg = ((lstick_x - range) / (1.0f - range)) * sa->specialhi.angle_diff;
        float rad = (float) ((double) deg * FTMM_SPECIALHI_DEG);
        float cur = fp->lstick_angle;

        if (!(stick_x <= 0.0f)) {
            rad = -rad;
        }
        if ((double) cur < FTMM_SPECIALHI_DEG) {
            cur = -cur;
        }
        if ((double) rad < FTMM_SPECIALHI_DEG) {
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
    if (sa->specialhi.reverse_stick_range < lstick_x) {
        ftCommon_UpdateFacing(fp);
        ftPartSetRotY(fp, 0, fp->facing_dir * FTMM_SPECIALHI_TURN);
    }
}

/* code+0x890, the collision callback of states 347 and 351, and its copy at code+0xA40 for
 * state 348. Retail ftMr_SpecialHi_Coll in the air (the landing callback at code+0xE10 makes
 * the same call as ftMr_SpecialHi_CheckLanding). On the ground it only runs the ground
 * collision (ft_800827A0) and does not start a fall when the floor is gone, where Mario calls
 * ft_80084104. */
void ftMM_SpecialHi_Coll(HSD_GObj* gobj)
{
    Fighter* fp = GET_FIGHTER(gobj);

    if (fp->ground_or_air == GA_Air) {
        if (fp->cmd_vars[0] == 0 || fp->self_vel.y >= 0.0f) {
            ft_80083B68(gobj);
        } else {
            ft_800831CC(gobj, &ftCo_80096CC8, &ftMr_SpecialHi_CheckLanding);
        }
    } else {
        ft_800827A0(gobj);
    }
}

/* code+0xA98, the physics callback of state 351 (his own). In the air, every frame: his effect
 * on the fighter's root object, no sideways speed, 4 a frame downward. On the ground: nothing. */
void ftMM_SpecialHiDive_Phys(HSD_GObj* gobj)
{
    Fighter* fp = GET_FIGHTER(gobj);

    if (fp->ground_or_air != GA_Air) {
        return;
    }
    efSync_Spawn(ftMM_Gfx_SpecialHiDive, gobj, gobj->hsd_obj);
    fp->self_vel.y = -4.0f;
    fp->self_vel.x = 0.0f;
}
