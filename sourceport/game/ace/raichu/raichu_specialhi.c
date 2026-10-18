/* PlRc.dat +0644, +0688, +18C4..+1A90, +21C8..+21FC. */
#include "raichu.h"
#include <dolphin/os.h>
#include <melee/ft/fighter.h>
#include <melee/ft/ft_081B.h>
#include <melee/ft/ftanim.h>
#include <melee/ft/ftcommon.h>
#include <melee/ft/inlines.h>
#include <melee/ft/kinds/ftCommon/ftCo_Fall.h>
#include <melee/ft/kinds/ftCommon/ftCo_Landing.h>
#include <melee/ft/kinds/ftPikachu/ftpikachuspecialhi.h>

void ftRc_SpecialHi_Enter(HSD_GObj* gobj)
{
    ftPk_SpecialHi_Enter(gobj);
    ftRc_Vars(GET_FIGHTER(gobj))->can_cancel_hi = 0;
}

void ftRc_SpecialAirHi_Enter(HSD_GObj* gobj)
{
    ftPk_SpecialAirHi_Enter(gobj);
    ftRc_Vars(GET_FIGHTER(gobj))->can_cancel_hi = 0;
}

/* +18C4: only the early cancellation is checked here, not Pikachu's IASA. */
void ftRc_SpecialAirHiEnd_IASA(HSD_GObj* gobj)
{
    Fighter* fp = GET_FIGHTER(gobj);
    if (ftRc_Vars(fp)->can_cancel_hi == 1 && fp->cur_anim_frame < 13.0f) {
        fp->x1968_jumpsUsed = 1;
        ftRc_Vars(fp)->can_cancel_hi = 0;
        if (fp->facing_dir == -1.0f) {
            OSReport("Left\n");
            Fighter_ChangeMotionState(gobj, 368, 0, 0, 1, 0, NULL);
        } else {
            OSReport("Right\n");
            Fighter_ChangeMotionState(gobj, 367, 0, 0, 1, 0, NULL);
        }
    }
}

/* +21C8. */
static void ftRc_SpecialHi_Land(HSD_GObj* gobj)
{
    ftCo_LandingFallSpecial_Enter(gobj, false, 28.0f);
}

/* +21D8. */
static void ftRc_SpecialHi_OnHit(HSD_GObj* gobj)
{
    ftRc_Vars(GET_FIGHTER(gobj))->can_cancel_hi = 1;
}

/* +19A8: the damage callback is installed even when the collision helper changes state. */
void ftRc_SpecialAirHiEnd_Coll(HSD_GObj* gobj)
{
    Fighter* fp = GET_FIGHTER(gobj);
    ft_80083A48(gobj, ftRc_SpecialHi_Land);
    fp->deal_dmg_cb = ftRc_SpecialHi_OnHit;
}

/* +19E8. */
void ftRc_SpecialHiCancel_Anim(HSD_GObj* gobj)
{
    if (!ftAnim_IsFramesRemaining(gobj)) {
        ftCommon_8007D92C(gobj);
    }
}

/* +1A34: keep the mod's per-frame diagnostic. */
void ftRc_SpecialHiCancel_IASA(HSD_GObj* gobj)
{
    OSReport("Should be working\n");
    ftCo_Fall_IASA_Inner(gobj);
}

/* +1A74, row 27 only. Row 26 uses Pikachu's original callback. */
void ftRc_SpecialHiCancel_Phys(HSD_GObj* gobj)
{
    Fighter* fp = GET_FIGHTER(gobj);
    if (fp->cmd_vars[0]) {
        ftCommon_Fall(fp, fp->co_attrs.gravity, fp->co_attrs.terminal_velocity);
    }
}
