/* PlRc.dat +0474, +04F4, +0CA0..+0F00, +1AC4. */
#include "raichu.h"
#include <melee/ft/fighter.h>
#include <melee/ft/ft_081B.h>
#include <melee/ft/ft_084E.h>
#include <melee/ft/ft_0892.h>
#include <melee/ft/ftanim.h>
#include <melee/ft/ftcommon.h>
#include <melee/ft/inlines.h>
#include <melee/ft/kinds/ftCommon/ftCo_Fall.h>
#include <melee/lb/lb_00B0.h>

/* +1AC4: consume only bit 0 of the first throw-flag byte. */
static void ftRc_SpecialN_Spawn(HSD_GObj* gobj)
{
    Fighter* fp = GET_FIGHTER(gobj);
    if (fp->throw_flags_b0) {
        Vec3 pos;
        fp->throw_flags_b0 = false;
        /* parts+0xE0 / console sizeof(FighterBone)==0x10: joint 14. */
        lb_8000B1CC(fp->parts[14].joint, NULL, &pos);
        itRc_Jolt_Spawn(gobj, &pos, mu_ak_article_kind(gobj, 1),
                       fp->ground_or_air, fp->facing_dir);
    }
}

/* +0474. The console clears the top bit of the first throw_flags byte. */
void ftRc_SpecialN_Enter(HSD_GObj* gobj)
{
    Fighter* fp = GET_FIGHTER(gobj);
    fp->cmd_vars[0] = 0;
    fp->throw_flags_b0 = false;
    Fighter_ChangeMotionState(gobj, 341, 0, 0, 1, 0, NULL);
    ftAnim_8006EBA4(gobj);
    fp->accessory4_cb = ftRc_SpecialN_Spawn;
}

/* +04F4. */
void ftRc_SpecialAirN_Enter(HSD_GObj* gobj)
{
    Fighter* fp = GET_FIGHTER(gobj);
    fp->cmd_vars[0] = 0;
    fp->throw_flags_b0 = false;
    Fighter_ChangeMotionState(gobj, 342, 0, 0, 1, 0, NULL);
    ftAnim_8006EBA4(gobj);
    fp->accessory4_cb = ftRc_SpecialN_Spawn;
}

/* +0CA0, +0D80, +134C: same body, end through ft_8008A2BC. */
void ftRc_SpecialN_Anim(HSD_GObj* gobj)
{
    if (!ftAnim_IsFramesRemaining(gobj)) {
        ft_8008A2BC(gobj);
    }
}

/* +0CEC and +1398: exact single tail call. */
void ftRc_SpecialN_Phys(HSD_GObj* gobj)
{
    ft_80084F3C(gobj);
}

/* +0CF0. */
void ftRc_SpecialN_Coll(HSD_GObj* gobj)
{
    if (!ft_80082708(gobj)) {
        Fighter* fp = GET_FIGHTER(gobj);
        ftCommon_8007D5D4(fp);
        Fighter_ChangeMotionState(gobj, 342, 0x5000, fp->cur_anim_frame, 1, 0, NULL);
        fp->accessory4_cb = ftRc_SpecialN_Spawn;
    }
}

/* +0DCC. The assembly uses a double FMA with the literal 0.7. */
void ftRc_SpecialAirN_Phys(HSD_GObj* gobj)
{
    Fighter* fp = GET_FIGHTER(gobj);
    ft_80084DB0(gobj);
    if (fp->cur_anim_frame == 32.0f) {
        fp->self_vel.x = (float) __builtin_fma(-(double) fp->facing_dir, 0.7,
                                             (double) fp->self_vel.x);
        /* +0E18 loads just the high word of the same double: 0x3FE66666,
         * which is 1.8f, rather than converting the double 0.7 to float. */
        fp->self_vel.y = 1.8f;
    } else if (fp->cur_anim_frame == 60.0f) {
        ftCo_Fall_Enter(gobj);
    }
}

/* +0E70. */
void ftRc_SpecialAirN_Coll(HSD_GObj* gobj)
{
    if (ft_80081D0C(gobj)) {
        Fighter* fp = GET_FIGHTER(gobj);
        ftCommon_8007D7FC(fp);
        Fighter_ChangeMotionState(gobj, 341, 0x5000, fp->cur_anim_frame, 1, 0, NULL);
        fp->accessory4_cb = ftRc_SpecialN_Spawn;
    }
}
