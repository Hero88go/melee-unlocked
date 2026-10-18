/* ACE's Metal Sonic, up special: Fox's Firefox rows (353 to 359) with his own entries and his own
 * animation callback for 355.
 *
 * His entries do not do what Fox's do (ftFx_SpecialHi_Enter / ftFx_SpecialAirHiStart_Enter):
 * instead of Fox's gravity delay, speed division and charge effect hook, a small rise
 * (ftCommon_Ascend with 0.1 and 0.2) and ftCommon_8007E2FC. Fox's rows then run as for Fox.
 *
 * Mod behaviour kept: fp->mv.fx.SpecialHi.gravityDelay (fp+2340) is not set by his entries, so
 * Fox's air hold physics (354) counts down whatever the state before left in that word. The
 * charge effect (Fox's 1163) is not armed by his entries; Fox's ground/air swaps of the hold
 * (ftFx_SpecialHiHold_GroundToAir, _AirToGround) and Fox's launch (ftFx_SpecialAirHi_Enter and the
 * swaps of 355/356) still arm Fox's effects 1163 and 1164 (see NOTES.md, retail effect ids). */
#include "metalsonic.h"

#include <melee/ft/fighter.h>
#include <melee/ft/ftanim.h>
#include <melee/ft/ftcommon.h>
#include <melee/ft/inlines.h>
#include <melee/ft/kinds/ftCommon/ftCo_FallSpecial.h>
#include <melee/ft/types.h>
#include <sysdolphin/baselib/gobj.h>

/* +0730 (slot 8, specialhi): ftCommon_Ascend (0.1, 0.2), ftCommon_8007E2FC, state 353 from
 * frame 0, ftAnim_8006EBA4. */
void ftNm_SpecialHi_Enter(HSD_GObj* gobj)
{
    Fighter* fp = GET_FIGHTER(gobj);

    ftCommon_Ascend(fp, 0.1f, 0.2f);
    ftCommon_8007E2FC(gobj);
    Fighter_ChangeMotionState(gobj, ftNm_MS_SpecialHiHold, Ft_MF_None, 0.0f, 1.0f, 0.0f, NULL);
    ftAnim_8006EBA4(gobj);
}

/* +07A4 (slot 9, specialairhi): the same into 354. */
void ftNm_SpecialAirHi_Enter(HSD_GObj* gobj)
{
    Fighter* fp = GET_FIGHTER(gobj);

    ftCommon_Ascend(fp, 0.1f, 0.2f);
    ftCommon_8007E2FC(gobj);
    Fighter_ChangeMotionState(gobj, ftNm_MS_SpecialHiHoldAir, Ft_MF_None, 0.0f, 1.0f, 0.0f,
                              NULL);
    ftAnim_8006EBA4(gobj);
}

/* +4C08: 355's animation callback (Fox's is 0x800E7634): at the end the special fall (1, 1, no
 * interrupt, mobility 0.02, landing 20), on the ground too. */
void ftNm_SpecialHi_Anim(HSD_GObj* gobj)
{
    if (!ftAnim_IsFramesRemaining(gobj)) {
        ftCo_80096900(gobj, 1, 1, false, 0.02f, 20.0f);
    }
}
