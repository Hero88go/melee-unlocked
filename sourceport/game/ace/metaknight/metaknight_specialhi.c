/* ACE's Meta Knight: the up special (states 351, 352, 353, 370), his own code.
 *
 *   351  start on the ground; 353 start in the air (its speed set to 0 at the entry). The stick
 *        past 0.2 sideways turns him. Neither state has a physics or a collision callback
 *   352  the rise: the air physics, plus 0.25 along the stick's angle while the stick is past 0.3
 *        sideways; a landing goes to the special landing (lag 30)
 *   370  the end: the special fall with his air drift as the mobility, landing lag 30 */
#include "metaknight.h"

#include <math.h>

#include <melee/ft/fighter.h>
#include <melee/ft/ft_081B.h>
#include <melee/ft/ft_084E.h>
#include <melee/ft/ftanim.h>
#include <melee/ft/ftcommon.h>
#include <melee/ft/inlines.h>
#include <melee/ft/kinds/ftCommon/ftCo_FallSpecial.h>
#include <melee/ft/kinds/ftCommon/ftCo_Landing.h>
#include <melee/ft/types.h>

/* +0570 (slot 8, specialhi) */
void ftMk_SpecialHi_Enter(HSD_GObj* gobj)
{
    FTMK_CHANGE(gobj, ftMk_MS_SpecialHiStart);
}

/* +0594 (slot 9, specialairhi) */
void ftMk_SpecialAirHi_Enter(HSD_GObj* gobj)
{
    Fighter* fp = GET_FIGHTER(gobj);

    fp->self_vel.x = 0.0f;
    fp->self_vel.y = 0.0f;
    FTMK_CHANGE(gobj, ftMk_MS_SpecialAirHiStart);
}

/* +1D10: animation of 351 (and of 353, +1F68 is a branch to it). */
void ftMk_SpecialHiStart_Anim(HSD_GObj* gobj)
{
    Fighter* fp = GET_FIGHTER(gobj);
    const float lx = fp->input.lstick[0].x;
    const float ax = lx >= 0.0f ? lx : -lx;

    if (ax > 0.2) {
        /* +1D84 */
        ftCommon_UpdateFacing(fp);
    }
    if (ftAnim_IsFramesRemaining(gobj)) {
        return;
    }
    /* +1D9C */
    FTMK_CHANGE(gobj, ftMk_MS_SpecialHiRise);
    fp->x1968_jumpsUsed = (u8) fp->co_attrs.max_jumps;
}

/* +1F68: animation of 353. */
void ftMk_SpecialAirHiStart_Anim(HSD_GObj* gobj)
{
    ftMk_SpecialHiStart_Anim(gobj);
}

/* +1DE4: animation of 352: at the end 370. */
void ftMk_SpecialHiRise_Anim(HSD_GObj* gobj)
{
    if (ftAnim_IsFramesRemaining(gobj)) {
        return;
    }
    FTMK_CHANGE(gobj, ftMk_MS_SpecialHiEnd);
}

/* +1E50: physics of 352: ft_80085154, then with the stick past 0.3 sideways 0.25 along its
 * angle (two fused double multiply-adds, each rounded to single). */
void ftMk_SpecialHiRise_Phys(HSD_GObj* gobj)
{
    Fighter* fp = GET_FIGHTER(gobj);
    float lx;
    float ax;
    float angle;

    ft_80085154(gobj);
    lx = fp->input.lstick[0].x;
    ax = lx >= 0.0f ? lx : -lx;
    if (!(ax > 0.3)) {
        return;
    }
    /* +1EA8 */
    angle = atan2f(fp->input.lstick[0].y, lx);
    fp->self_vel.x = (float) __builtin_fma((double) cosf(angle), (double) 0.25f,
                                           (double) fp->self_vel.x);
    fp->self_vel.y = (float) __builtin_fma((double) sinf(angle), 0.25, (double) fp->self_vel.y);
}

/* +1F10: collision of 352 and 370: on landing, the special landing (no interrupt, lag 30). */
void ftMk_SpecialHi_AirColl(HSD_GObj* gobj)
{
    if (ft_80081D0C(gobj)) {
        ftCo_LandingFallSpecial_Enter(gobj, false, 30.0f);
    }
}

/* +2BD8: animation of 370: at the end the special fall (no interrupt, his air drift as the
 * mobility, landing lag 30), every jump given back. */
void ftMk_SpecialHiEnd_Anim(HSD_GObj* gobj)
{
    Fighter* fp = GET_FIGHTER(gobj);

    if (ftAnim_IsFramesRemaining(gobj)) {
        return;
    }
    ftCo_80096900(gobj, 0, 1, false, fp->co_attrs.air_drift_max, 30.0f);
    fp->x1968_jumpsUsed = (u8) fp->co_attrs.max_jumps;
}

/* +2C58: physics of 370: a branch to ft_80084EEC. */
void ftMk_SpecialHiEnd_Phys(HSD_GObj* gobj)
{
    ft_80084EEC(gobj);
}
