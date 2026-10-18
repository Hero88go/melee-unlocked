/* Literal native bodies from PlLb.dat's ftFunction block. Lone-return
 * exports are intentional behavior in the mod, including grounded neutral
 * and both down-special entries. */
#include "luigiboo_entries.h"

#include <melee/ft/fighter.h>
#include <melee/ft/ftanim.h>
#include <melee/ft/ftcliffcommon.h>
#include <melee/ft/ftcommon.h>
#include <melee/ft/inlines.h>
#include <melee/ft/kinds/ftCommon/ftCo_FallSpecial.h>
#include <melee/ft/kinds/ftLuigi/ftluigispecialhi.h>
#include <melee/ft/types.h>
#include <sysdolphin/baselib/gobj.h>
#include <sysdolphin/baselib/jobj.h>

#include <stddef.h>

/* +052C, +06AC, +06B0 and +06B4 are each exactly blr. */
void ftLb_SpecialN_Enter(Fighter_GObj* gobj) { (void) gobj; }
void ftLb_SpecialLw_Enter(Fighter_GObj* gobj) { (void) gobj; }
void ftLb_SpecialAirLw_Enter(Fighter_GObj* gobj) { (void) gobj; }
void ftLb_OnFrame(Fighter_GObj* gobj) { (void) gobj; }

/* +0530 and +0590 have identical instruction words. They only synchronize
 * facing with the model; they do not change motion state. */
void ftLb_SpecialAirN_Enter(Fighter_GObj* gobj)
{
    Fighter* fp = GET_FIGHTER(gobj);
    HSD_JObj* joint = gobj->hsd_obj;
    fp->facing_dir = joint->rotate.y > 0.0f ? 1.0f : -1.0f;
}

void ftLb_SpecialAirS_Enter(Fighter_GObj* gobj)
{
    Fighter* fp = GET_FIGHTER(gobj);
    HSD_JObj* joint = gobj->hsd_obj;
    fp->facing_dir = joint->rotate.y > 0.0f ? 1.0f : -1.0f;
}

/* +056C: state 343, flag word 0x20, no follow-up ftAnim call. */
void ftLb_SpecialS_Enter(Fighter_GObj* gobj)
{
    Fighter_ChangeMotionState(gobj, 343, 0x20, 0.0f, 1.0f, 0.0f, NULL);
}

/* +0A1C. */
void ftLb_SpecialS_Anim(Fighter_GObj* gobj)
{
    if (!ftAnim_IsFramesRemaining(gobj)) {
        ftCommon_8007D92C(gobj);
    }
}

/* +0F3C clears console byte 2210's 0x10 bit before the retail callback.
 * That bit is throw_flags.b3 in the native Fighter layout. */
void ftLb_SpecialAirHi_IASA(Fighter_GObj* gobj)
{
    Fighter* fp = GET_FIGHTER(gobj);
    fp->throw_flags_b3 = false;
    ftLg_SpecialAirHi_IASA(gobj);
}

/* +0F5C. */
void ftLb_SpecialAirHi_Phys(Fighter_GObj* gobj)
{
    ftCommon_CalcSelfAccel_DriftSimple(GET_FIGHTER(gobj), 0.0f, 0.75f, 1.5f);
    ftLg_SpecialAirHi_Phys(gobj);
}

/* +10A4. */
void ftLb_Recovery_Anim(Fighter_GObj* gobj)
{
    Fighter* fp = GET_FIGHTER(gobj);
    if (!ftAnim_IsFramesRemaining(gobj)) {
        ftCo_80096900(gobj, 0, 1, 1, fp->co_attrs.air_drift_stick_mul, 20.0f);
    }
}

/* +1118. */
void ftLb_Recovery_IASA(Fighter_GObj* gobj)
{
    if (ftCliffCommon_80081298(gobj)) {
        ftCo_CliffCatch_Phys(gobj);
        Fighter_ChangeMotionState(gobj, 0xFC, 0, 0.0f, 1.0f, 0.0f, NULL);
    }
}
