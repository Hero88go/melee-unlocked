/* ACE's Toad: the down special (states 349 and 350), the file's own code.
 *
 * On the ground the animation script raises cmd_vars[1] once: the meter goes up one level (3 at
 * most) and its timer is set to 1200 frames (toad_meter.c applies the level). The air version
 * is the pose alone: it never raises the meter. */
#include "toad.h"

#include <melee/ft/fighter.h>
#include <melee/ft/ft_081B.h>
#include <melee/ft/ft_084E.h>
#include <melee/ft/ft_0892.h>
#include <melee/ft/ftanim.h>
#include <melee/ft/inlines.h>
#include <melee/ft/kinds/ftCommon/ftCo_Fall.h>
#include <melee/ft/kinds/ftCommon/ftCo_Landing.h>
#include <melee/ft/types.h>

#define FTTD_METER_MAX 3
#define FTTD_METER_FRAMES 1200            /* 0x4B0 */
#define FTTD_SPECIALAIRLW_LANDING_LAG 8.0f /* +1600 */

/* +0688 (slot 10) and +06F0 (slot 11), with the state. */
static void ftTd_SpecialLw_EnterState(HSD_GObj* gobj, FtMotionId msid)
{
    Fighter* fp = GET_FIGHTER(gobj);

    fp->cmd_vars[0] = 0;
    fp->cmd_vars[1] = 0;
    fp->cmd_vars[2] = 0;
    fp->cmd_vars[3] = 0;
    Fighter_ChangeMotionState(gobj, msid, Ft_MF_None, 0.0f, 1.0f, 0.0f, NULL);
    ftAnim_8006EBA4(gobj);
}

/* +0688 */
void ftTd_SpecialLw_Enter(HSD_GObj* gobj)
{
    ftTd_SpecialLw_EnterState(gobj, ftMr_MS_SpecialLw);
}

/* +06F0 */
void ftTd_SpecialAirLw_Enter(HSD_GObj* gobj)
{
    ftTd_SpecialLw_EnterState(gobj, ftMr_MS_SpecialAirLw);
}

/* +1464: state 349, animation. */
void ftTd_SpecialLw_Anim(HSD_GObj* gobj)
{
    Fighter* fp = GET_FIGHTER(gobj);

    if (fp->cmd_vars[1] != 0) {
        ftTd_MeterVars* mv = ftTd_Meter(fp);
        int level = mv->level + 1;

        fp->cmd_vars[1] = 0;
        if (level > FTTD_METER_MAX) {
            level = FTTD_METER_MAX;
        }
        mv->level = level;
        mv->timer = FTTD_METER_FRAMES;
    }
    if (!ftAnim_IsFramesRemaining(gobj)) {
        ft_8008A2BC(gobj);
    }
}

/* +14F4: state 349, physics: a branch to the retail routine. */
void ftTd_SpecialLw_Phys(HSD_GObj* gobj)
{
    ft_80084F3C(gobj);
}

/* +14F8: state 349, collision: the fall unless the floor test gives exactly 1. */
void ftTd_SpecialLw_Coll(HSD_GObj* gobj)
{
    if ((int) ft_800827A0(gobj) != 1) {
        ftCo_Fall_Enter(gobj);
    }
}

/* +1544: state 350, animation. */
void ftTd_SpecialAirLw_Anim(HSD_GObj* gobj)
{
    if (!ftAnim_IsFramesRemaining(gobj)) {
        ftCo_Fall_Enter(gobj);
    }
}

/* +1594: state 350, physics: a branch to the retail routine. */
void ftTd_SpecialAirLw_Phys(HSD_GObj* gobj)
{
    ft_80084DB0(gobj);
}

/* +1598: state 350, collision. */
void ftTd_SpecialAirLw_Coll(HSD_GObj* gobj)
{
    if (ft_80081D0C(gobj)) {
        ftCo_LandingFallSpecial_Enter(gobj, false, FTTD_SPECIALAIRLW_LANDING_LAG);
    }
}
