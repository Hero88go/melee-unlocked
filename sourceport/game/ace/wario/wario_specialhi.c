/* ACE's Wario: the up special (states 348 and 349), his own code.
 *
 * Both states run the same routines (the air state's animation callback is a branch to the
 * ground one; its physics and collision callbacks are second copies). During the first frames
 * (attribute +10, 4) the stick turns him as in the neutral special; the x speed follows the
 * stick every frame; the animation ends in the special fall. */
#include "wario.h"

#include <melee/ft/fighter.h>
#include <melee/ft/ft_081B.h>
#include <melee/ft/ft_084E.h>
#include <melee/ft/ftanim.h>
#include <melee/ft/ftcommon.h>
#include <melee/ft/inlines.h>
#include <melee/ft/kinds/ftCommon/ftCo_FallSpecial.h>
#include <melee/ft/kinds/ftCommon/ftCo_Landing.h>
#include <melee/ft/types.h>

/* +042C (slot 8, specialhi) */
void ftWr_SpecialHi_Enter(HSD_GObj* gobj)
{
    Fighter_ChangeMotionState(gobj, ftWr_MS_SpecialHi, Ft_MF_None, 0.0f, 1.0f, 0.0f, NULL);
    ftAnim_8006EBA4(gobj);
}

/* +047C (slot 9, specialairhi) */
void ftWr_SpecialAirHi_Enter(HSD_GObj* gobj)
{
    Fighter* fp = GET_FIGHTER(gobj);

    Fighter_ChangeMotionState(gobj, ftWr_MS_SpecialAirHi, Ft_MF_None, 0.0f, 1.0f, 0.0f, NULL);
    ftAnim_8006EBA4(gobj);
    fp->cmd_vars[0] = 0;
}

/* +19B0: states 348 and 349, animation. */
void ftWr_SpecialHi_Anim(HSD_GObj* gobj)
{
    Fighter* fp = GET_FIGHTER(gobj);
    ftWr_DatAttrs* sa = ftWr_Attrs(fp);

    if (fp->cur_anim_frame < (float) sa->specialhi_turn_frames) {
        float threshold = sa->specialhi_turn_stick;
        bool turn = false;

        if (fp->facing_dir > 0.0f) {
            turn = fp->input.lstick[0].x < -threshold;
        } else if (fp->facing_dir < 0.0f) {
            turn = fp->input.lstick[0].x > threshold;
        }
        if (turn) {
            ftCommon_UpdateFacing(fp);
            Fighter_ChangeMotionState(gobj, fp->motion_id, Ft_MF_None, fp->cur_anim_frame,
                                      1.0f, 0.0f, NULL);
        }
    }
    if (!ftAnim_IsFramesRemaining(gobj)) {
        ftCo_80096900(gobj, 1, 0, false, sa->specialhi_fall_mobility,
                      (float) sa->specialhi_landing_lag);
    }
}

/* +1B10: state 348, interrupt: a lone return. */
void ftWr_SpecialHi_IASA(HSD_GObj* gobj)
{
    (void) gobj;
}

/* +1B14: state 348, physics. */
void ftWr_SpecialHi_Phys(HSD_GObj* gobj)
{
    Fighter* fp = GET_FIGHTER(gobj);
    ftWr_DatAttrs* sa = ftWr_Attrs(fp);

    fp->self_vel.x = sa->specialhi_drift * fp->input.lstick[0].x;
    ft_800851C0(gobj);
}

/* +20C4 and +2100 (two copies): the landing of the up special. */
static void ftWr_SpecialHi_Landed(HSD_GObj* gobj)
{
    ftWr_DatAttrs* sa = ftWr_Attrs(GET_FIGHTER(gobj));

    ftCo_LandingFallSpecial_Enter(gobj, false, (float) sa->specialhi_landing_lag);
}

/* +1B30: state 348, collision. Falling: the air collision with wall jump and ledge grab.
 * Rising or level: the ground and ledge test. */
void ftWr_SpecialHi_Coll(HSD_GObj* gobj)
{
    Fighter* fp = GET_FIGHTER(gobj);

    if (fp->self_vel.y < 0.0f) {
        ft_800831CC(gobj, NULL, ftWr_SpecialHi_Landed);
    } else {
        ft_CheckGroundAndLedge(gobj, (int) fp->facing_dir);
    }
}

/* +1B78: state 349, animation: a branch to +19B0. */
void ftWr_SpecialAirHi_Anim(HSD_GObj* gobj)
{
    ftWr_SpecialHi_Anim(gobj);
}

/* +1B7C: state 349, interrupt: a lone return. */
void ftWr_SpecialAirHi_IASA(HSD_GObj* gobj)
{
    (void) gobj;
}

/* +1B80: state 349, physics: +1B14 again. */
void ftWr_SpecialAirHi_Phys(HSD_GObj* gobj)
{
    ftWr_SpecialHi_Phys(gobj);
}

/* +1B9C: state 349, collision: +1B30 again, with the copy of the landing routine at +2100. */
void ftWr_SpecialAirHi_Coll(HSD_GObj* gobj)
{
    ftWr_SpecialHi_Coll(gobj);
}
