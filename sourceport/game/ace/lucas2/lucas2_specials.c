/* PlLc2.dat native C. Ness layouts, local callbacks. Review status in PROGRESS.md. */
#include "lucas2_specials.h"

#include <Runtime/platform.h>

#include <melee/ft/forward.h>

#include <melee/ft/kinds/ftNess/types.h>
#include <dolphin/mtx.h>
#include <melee/ft/fighter.h>
#include <melee/ft/ft_081B.h>
#include <melee/ft/ft_084E.h>
#include <melee/ft/ft_0892.h>
#include <melee/ft/ftanim.h>
#include <melee/ft/ftparts.h>
#include "lucas2.h"
#include <melee/ft/kinds/ftCommon/ftCo_Fall.h>
#include <melee/ft/kinds/ftCommon/ftCo_Landing.h>
#include <melee/ft/types.h>
#include <melee/it/kinds/itnesspkfire.h>
#include <melee/lb/lb_00B0.h>

/* +40F0: article zero, bone remap 0x17, fixed-speed fire shot. */
void ftLc2_SpecialS_ItemPKFireSpawn(HSD_GObj* gobj)
{
    Fighter* fp = GET_FIGHTER(gobj);
    Vec3 pos;
    if (!fp->throw_flags_b0) return;
    fp->throw_flags_b0 = false;
    lb_8000B1CC(fp->parts[ftParts_GetBoneIndex(fp, 0x17)].joint, NULL, &pos);
    ftLc2_Fire_Spawn(gobj, &pos, mu_ak_article_kind(gobj, 0), fp->facing_dir);
}

void ftLc2_SpecialS_Enter(
    HSD_GObj* gobj) 
{
    Fighter* fp;

    fp = GET_FIGHTER(gobj);
    fp->throw_flags = 0; 
    fp->cmd_vars[0] = 0; 
    Fighter_ChangeMotionState(gobj, ftNs_MS_SpecialS, 0, 0.0f, 1.0f, 0.0f,
                              NULL);
    ftAnim_8006EBA4(gobj);
    fp->accessory4_cb =
        ftLc2_SpecialS_ItemPKFireSpawn; 
}

void ftLc2_SpecialAirS_Enter(
    HSD_GObj* gobj) 
{
    Fighter* fp;

    fp = GET_FIGHTER(gobj);
    fp->throw_flags = 0; 
    fp->cmd_vars[0] = 0; 
    Fighter_ChangeMotionState(gobj, ftNs_MS_SpecialAirS, 0, 0.0f, 1.0f, 0.0f,
                              NULL);
    ftAnim_8006EBA4(gobj);
    fp->accessory4_cb = ftLc2_SpecialS_ItemPKFireSpawn;
}

void ftLc2_SpecialS_Anim(
    HSD_GObj* gobj) 
{
    if (!ftAnim_IsFramesRemaining(gobj)) {
        ft_8008A2BC(gobj);
    }
}

void ftLc2_SpecialAirS_Anim(
    HSD_GObj* gobj) 
{
    if (!ftAnim_IsFramesRemaining(gobj)) {
        ftCo_Fall_Enter(gobj);
    }
}

void ftLc2_SpecialS_Phys(
    HSD_GObj* gobj) 
{
    ft_80084F3C(gobj);
}

void ftLc2_SpecialAirS_Phys(
    HSD_GObj* gobj) 
{
    ft_80084EEC(gobj);
}

void ftLc2_SpecialS_Coll(
    HSD_GObj* gobj) 
{
    if (ft_800827A0(gobj) == false) {
        ftCo_Fall_Enter(gobj);
    }
}

void ftLc2_SpecialAirS_Coll(
    HSD_GObj* gobj) 
{
    Fighter* fp = GET_FIGHTER(gobj);
    ftNessAttributes* ness_attr;

    ness_attr = fp->dat_attrs;
    if (ft_80081D0C(gobj) != false) {
        ftCo_LandingFallSpecial_Enter(gobj, false,
                                      ness_attr->x38_PKFIRE_LANDING_LAG);
    }
}
