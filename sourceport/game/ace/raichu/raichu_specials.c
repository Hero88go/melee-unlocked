/* Raichu's side special. PlRc.dat +0574, +05D4, +0F00..+18C4,
 * +1F78..+21B8. Retail callbacks occur only in table rows 4, 6 and 9. */
#include "raichu.h"
#include <melee/ft/fighter.h>
#include <melee/ft/ft_081B.h>
#include <melee/ft/ft_084E.h>
#include <melee/ft/ft_0881.h>
#include <melee/ft/ftanim.h>
#include <melee/ft/ftcommon.h>
#include <melee/ft/inlines.h>
#include <melee/ft/kinds/ftCommon/ftCo_Fall.h>
#include <melee/ft/kinds/ftCommon/ftCo_Jump.h>

/* +0574. */
void ftRc_SpecialS_Enter(HSD_GObj* gobj)
{
    Fighter* fp = GET_FIGHTER(gobj);
    fp->cmd_vars[0] = 0;
    ftRc_Vars(fp)->charge = 0;
    Fighter_ChangeMotionState(gobj, 343, 0, 0, 1, 0, NULL);
    ftAnim_8006EBA4(gobj);
}

/* +05D4. */
void ftRc_SpecialAirS_Enter(HSD_GObj* gobj)
{
    Fighter* fp = GET_FIGHTER(gobj);
    fp->cmd_vars[0] = 0;
    ftRc_Vars(fp)->charge = 0;
    fp->self_vel.x = fp->self_vel.y = fp->gr_vel = fp->xF0_ground_kb_vel = 0;
    Fighter_ChangeMotionState(gobj, 348, 0, 0, 1, 0, NULL);
    ftAnim_8006EBA4(gobj);
}

/* +0F00. */
void ftRc_SpecialSStart_Anim(HSD_GObj* gobj)
{
    if (!ftAnim_IsFramesRemaining(gobj)) {
        Fighter* fp = GET_FIGHTER(gobj);
        Fighter_ChangeMotionState(gobj, 344, 0, 0, 1, 0, NULL);
        ft_800881D8(fp, 5088, 128, 50);
    }
}

/* +0F94: shared by seven table slots. */
void ftRc_Empty(HSD_GObj* gobj) { (void) gobj; }

/* +0F98: preserve the write to self_vel.x, not gr_vel. */
void ftRc_SpecialSStart_Phys(HSD_GObj* gobj)
{
    Fighter* fp = GET_FIGHTER(gobj);
    ftPikachuAttributes* at = ftRc_Attrs(fp);
    ft_80084F3C(gobj);
    fp->self_vel.x = at->x24 * fp->facing_dir;
}

/* +0FE0: the console does not change ground_or_air here. */
void ftRc_SpecialSStart_Coll(HSD_GObj* gobj)
{
    if (!ft_80082708(gobj)) {
        Fighter_ChangeMotionState(gobj, 352, 0, 1, 1, 0, NULL);
    }
}

/* +104C. A command pulse increments charge only below the attribute limit. */
void ftRc_SpecialSHold_Anim(HSD_GObj* gobj)
{
    Fighter* fp = GET_FIGHTER(gobj);
    if (fp->cmd_vars[0] && (float) ftRc_Vars(fp)->charge < ftRc_Attrs(fp)->x2C) {
        ftRc_Vars(fp)->charge++;
        fp->cmd_vars[0] = 0;
    }
    if (fp->input.pressed_buttons & 0x200) {
        fp->gr_vel = fp->self_vel.x;
        ft_80088A50(fp);
        Fighter_ChangeMotionState(gobj, 346, 0, 1, 1, 0, NULL);
    }
}

/* +111C. */
void ftRc_SpecialSHold_IASA(HSD_GObj* gobj)
{
    Fighter* fp = GET_FIGHTER(gobj);
    if (ftCo_Jump_CheckInput(gobj) == 1) {
        fp->gr_vel = fp->self_vel.x;
        ft_80088A50(fp);
    }
}

/* +1174: use the charge velocity only when its magnitude exceeds x24. */
void ftRc_SpecialSHold_Phys(HSD_GObj* gobj)
{
    Fighter* fp = GET_FIGHTER(gobj);
    ftPikachuAttributes* at = ftRc_Attrs(fp);
    float v = (float) ftRc_Vars(fp)->charge * at->x28;
    float mag = v >= 0.0f ? v : -v;
    fp->self_vel.x = (at->x24 < mag ? v : at->x24) * fp->facing_dir;
}

/* +1F78 and +2094 are separate callbacks: the first preserves x velocity on
 * a grounded hit, while post-hitlag clears it, on either ground or air. */
static void ftRc_SpecialS_Hit(HSD_GObj* gobj)
{
    Fighter* fp = GET_FIGHTER(gobj);
    float damage = __builtin_fmaf((float) ftRc_Vars(fp)->charge,
                                  ftRc_Attrs(fp)->specials_start_gravity,
                                  (float) (s32) fp->cmd_vars[0]);
    Fighter_TakeDamage_8006CC7C(fp, damage);
    if (fp->ground_or_air == GA_Ground) {
        ft_80088A50(fp);
        Fighter_ChangeMotionState(gobj, 346, 0, 1, 1, 0, NULL);
    } else {
        fp->self_vel.x = 0;
        ft_80088A50(fp);
        Fighter_ChangeMotionState(gobj, 351, 0, 1, 1, 0, NULL);
    }
}

static void ftRc_SpecialS_PostHitlag(HSD_GObj* gobj)
{
    Fighter* fp = GET_FIGHTER(gobj);
    ftPikachuAttributes* at = ftRc_Attrs(fp);
    float damage = __builtin_fmaf((float) ftRc_Vars(fp)->charge,
                                  at->specials_start_gravity,
                                  (float) (s32) fp->cmd_vars[0]);
    Fighter_TakeDamage_8006CC7C(fp, damage);
    fp->self_vel.x = 0;
    ft_80088A50(fp);
    Fighter_ChangeMotionState(gobj, fp->ground_or_air == GA_Ground ? 346 : 351,
                             0, 1, at->x3C, 0, NULL);
}

static void ftRc_SpecialS_SetHitCallbacks(Fighter* fp)
{
    if (ftRc_Vars(fp)->charge > 0) {
        float damage = __builtin_fmaf((float) ftRc_Vars(fp)->charge,
                                      ftRc_Attrs(fp)->specials_start_friction,
                                      ftRc_Attrs(fp)->x30);
        fp->x914[0].unk_count = (s32) damage;
        fp->x914[0].damage = (float) (s32) damage;
    }
    fp->deal_dmg_cb = ftRc_SpecialS_Hit;
    fp->post_hitlag_cb = ftRc_SpecialS_PostHitlag;
}

/* +11F8. */
void ftRc_SpecialSHold_Coll(HSD_GObj* gobj)
{
    Fighter* fp = GET_FIGHTER(gobj);
    ftPikachuAttributes* at = ftRc_Attrs(fp);
    if (!ft_80082708(gobj)) {
        float charge = (float) ftRc_Vars(fp)->charge;
        fp->self_vel.x = __builtin_fmaf(at->x48, charge, at->x44) * fp->facing_dir;
        fp->self_vel.y = __builtin_fmaf(charge, at->x50, at->x4C);
        ftCommon_8007D5D4(fp);
        Fighter_ChangeMotionState(gobj, 352, 0x5000, 0, 1, 0, NULL);
    }
    ftRc_SpecialS_SetHitCallbacks(fp);
}

/* +139C. */
void ftRc_SpecialSEnd_Coll(HSD_GObj* gobj)
{
    if (!ft_80082708(gobj)) {
        ftCommon_8007D5D4(GET_FIGHTER(gobj));
        Fighter_ChangeMotionState(gobj, 351, 0, 1, 1, 0, NULL);
    }
}

/* +1410. */
void ftRc_SpecialAirSStart_Anim(HSD_GObj* gobj)
{
    if (!ftAnim_IsFramesRemaining(gobj)) {
        Fighter_ChangeMotionState(gobj, 349, 0, 0, 1, 0, NULL);
    }
}

/* +147C, also used by the air hold and air end. */
void ftRc_SpecialAirSStart_Phys(HSD_GObj* gobj)
{
    Fighter* fp = GET_FIGHTER(gobj);
    ftCommon_Fall(fp, fp->co_attrs.gravity, ftRc_Attrs(fp)->x40);
}

/* +1490: deliberately preserve the mod's D5D4 call on landing. */
void ftRc_SpecialAirSStart_Coll(HSD_GObj* gobj)
{
    if (ft_80081D0C(gobj)) {
        ftCommon_8007D5D4(GET_FIGHTER(gobj));
        Fighter_ChangeMotionState(gobj, 343, 0, 1, 1, 0, NULL);
    }
}

/* +1504. Release also occurs once charge exceeds the limit, even without B. */
void ftRc_SpecialAirSHold_Anim(HSD_GObj* gobj)
{
    Fighter* fp = GET_FIGHTER(gobj);
    ftPikachuAttributes* at = ftRc_Attrs(fp);
    float charge = (float) ftRc_Vars(fp)->charge;
    if (fp->cmd_vars[0] && at->x2C > charge) {
        ftRc_Vars(fp)->charge++;
        fp->cmd_vars[0] = 0;
        charge = (float) ftRc_Vars(fp)->charge;
    }
    if ((fp->input.pressed_buttons & 0x200) || at->x2C < charge) {
        fp->self_vel.x = __builtin_fmaf(at->x48, charge, at->x44) * fp->facing_dir;
        fp->self_vel.y = __builtin_fmaf(at->x50, charge, at->x4C);
        Fighter_ChangeMotionState(gobj, 352, 0, 0, 1, 0, NULL);
    }
}

/* +15F0. */
void ftRc_SpecialAirSHold_Coll(HSD_GObj* gobj)
{
    if (ft_80081D0C(gobj)) {
        ftCommon_8007D6A4(GET_FIGHTER(gobj));
        Fighter_ChangeMotionState(gobj, 344, 0, 1, 1, 0, NULL);
    }
}

/* +1664. */
void ftRc_SpecialAirSEnd_Anim(HSD_GObj* gobj)
{
    if (!ftAnim_IsFramesRemaining(gobj)) {
        ftCo_Fall_Enter(gobj);
    }
}

/* +16B0. */
void ftRc_SpecialAirSEnd_Coll(HSD_GObj* gobj)
{
    if (ft_80081D0C(gobj)) {
        Fighter_ChangeMotionState(gobj, 346, 0, 1, 1, 0, NULL);
    }
}

/* +171C. */
void ftRc_SpecialAirS0_Anim(HSD_GObj* gobj)
{
    if (!ftAnim_IsFramesRemaining(gobj)) {
        Fighter_ChangeMotionState(gobj, 351, 0, 0, 1, 0, NULL);
    }
}

/* +1788: direction, rather than the velocity sign, chooses the deceleration. */
void ftRc_SpecialAirS0_Phys(HSD_GObj* gobj)
{
    Fighter* fp = GET_FIGHTER(gobj);
    ftPikachuAttributes* at = ftRc_Attrs(fp);
    if (fp->self_vel.x < 0.0f || fp->self_vel.x > 0.0f) {
        fp->self_vel.x = __builtin_fmaf(fp->facing_dir, -at->x54, fp->self_vel.x);
    }
    ftCommon_Fall(fp, at->x58, at->x40);
}

/* +17C8. */
void ftRc_SpecialAirS0_Coll(HSD_GObj* gobj)
{
    Fighter* fp = GET_FIGHTER(gobj);
    if (ft_80081D0C(gobj)) {
        Fighter_ChangeMotionState(gobj, 346, 0, 1, 1, 0, NULL);
    }
    ftRc_SpecialS_SetHitCallbacks(fp);
}
