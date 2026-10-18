/* ACE's Chun-Li: the down special (states 355 and 357) (m-ex "ftFunction" of PlCn.dat).
 *
 * On the ground a kick in place (355); in the air (357) a hop at 3.75 times the facing forward
 * and 9.0 up, then her own gravity (+6C, +60) and a horizontal speed that loses +70 of itself
 * each frame and stops below 0.2. The air version can be used once per air time (fp+2244,
 * cleared on landing, on a ledge catch and on DeadDown). It lands into the common
 * LandingAirF (71) with a full state change. The onframe cancels enter both. */
#include "chunli.h"

#include <melee/ft/fighter.h>
#include <melee/ft/ft_081B.h>
#include <melee/ft/ftanim.h>
#include <melee/ft/ftcommon.h>
#include <melee/ft/inlines.h>
#include <melee/ft/types.h>
#include <sysdolphin/baselib/gobj.h>

/* +067C, [speciallw]: state 355 and its script. */
void ftCn_SpecialLw_Enter(HSD_GObj* gobj)
{
    Fighter_ChangeMotionState(gobj, ftCn_MS_SpecialLw, Ft_MF_None, 0.0f, 1.0f, 0.0f, NULL);
    ftAnim_8006EBA4(gobj);
}

/* +06CC, [specialairlw] (also called by the onframe cancels at +092C): once per air time,
 * state 357, its script, the hop. The word is cleared again at once when she is on the
 * ground. */
void ftCn_SpecialAirLw_Enter(HSD_GObj* gobj)
{
    Fighter* fp = GET_FIGHTER(gobj);
    Fighter* fp2;

    if (ftCn_Vars(fp)->specialairlw_used == 1) {
        return;
    }
    Fighter_ChangeMotionState(gobj, ftCn_MS_SpecialAirLw, Ft_MF_None, 0.0f, 1.0f, 0.0f, NULL);
    ftAnim_8006EBA4(gobj);
    fp2 = GET_FIGHTER(gobj);
    ftCn_Vars(fp)->specialairlw_used = 1;
    fp->self_vel.y = 9.0f;
    fp->self_vel.x = fp->facing_dir * 3.75f;
    if (fp2->ground_or_air == GA_Ground) {
        ftCn_Vars(fp2)->specialairlw_used = 0;
    }
}

/* +1FE8, the collision callback of 355: off the ground, 357 from the same frame (a full state
 * change, without the hop), airborne. */
void ftCn_SpecialLw_Coll(HSD_GObj* gobj)
{
    Fighter* fp = GET_FIGHTER(gobj);

    if (ft_80082708(gobj)) {
        return;
    }
    Fighter_ChangeMotionState(gobj, ftCn_MS_SpecialAirLw, Ft_MF_None, fp->cur_anim_frame, 1.0f,
                              0.0f, NULL);
    ftCommon_8007D5D4(fp);
}

/* +20C0, the physics callback of 357: her gravity, then the horizontal speed times
 * (1 - decay), zero once its size is under 0.2 (a double compare). */
MU_NO_CONTRACT void ftCn_SpecialAirLw_Phys(HSD_GObj* gobj)
{
    Fighter* fp = GET_FIGHTER(gobj);
    ftCn_DatAttrs* da = ftCn_Attrs(fp);
    float vel_x;
    float size;

    ftCommon_Fall(fp, da->specialairlw_gravity, da->specialairlw_terminal_vel);
    fp = GET_FIGHTER(gobj);
    da = ftCn_Attrs(fp);
    vel_x = 1.0f - da->specialairlw_vel_x_decay;
    vel_x = vel_x * fp->self_vel.x;
    size = vel_x < 0.0f ? -vel_x : vel_x;
    if ((double) size < 0.2) {
        vel_x = 0.0f;
    }
    fp->self_vel.x = vel_x;
}

/* +2150, the collision callback of 357: landed, grounded, the common LandingAirF from frame 0
 * with a full state change. */
void ftCn_SpecialAirLw_Coll(HSD_GObj* gobj)
{
    if (!ft_80081D0C(gobj)) {
        return;
    }
    ftCommon_8007D7FC(GET_FIGHTER(gobj));
    Fighter_ChangeMotionState(gobj, ftCo_MS_LandingAirF, Ft_MF_None, 0.0f, 1.0f, 0.0f, NULL);
}
