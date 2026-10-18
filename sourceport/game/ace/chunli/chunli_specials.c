/* ACE's Chun-Li: the side special (states 345, 348, 359 to 363) (m-ex "ftFunction" of PlCn.dat).
 *
 * A start (345 on the ground, 348 in the air; the onframe cancels enter them from frame 12),
 * then at the end of its animation the kick (359, 360) at the attribute speed times the facing
 * with no vertical speed, then the end (361, 362). The air version can be used once per air
 * time (fp+2240, cleared on landing, on a ledge catch and on DeadDown).
 *
 * Kept as on the console:
 *  - 345 and 348 run two physics steps a frame (ft_80085154 and ft_80084FA8), the same on the
 *    ground and in the air;
 *  - 360 on landing changes to 360 again (its own number), not to 359, and grounds her;
 *  - the ground start, the kick and the end on the ground leave by a full state change (flags
 *    0) or the swap flags 0x0C4C5080, as each one is written. */
#include "chunli.h"

#include <melee/ft/fighter.h>
#include <melee/ft/ft_081B.h>
#include <melee/ft/ft_084E.h>
#include <melee/ft/ft_0892.h>
#include <melee/ft/ftanim.h>
#include <melee/ft/ftcliffcommon.h>
#include <melee/ft/ftcommon.h>
#include <melee/ft/inlines.h>
#include <melee/ft/kinds/ftCommon/ftCo_Fall.h>
#include <melee/ft/types.h>
#include <sysdolphin/baselib/gobj.h>

/* +29D0 ([specials] is +055C, a jump here): the ground or the air start, no vertical speed. */
void ftCn_SpecialS_Enter(HSD_GObj* gobj)
{
    Fighter* fp = GET_FIGHTER(gobj);

    if (fp->ground_or_air == GA_Ground) {
        Fighter_ChangeMotionState(gobj, ftCn_MS_SpecialSStart, Ft_MF_None, 0.0f, 1.0f, 0.0f,
                                  NULL);
        fp->self_vel.y = 0.0f;
        ftAnim_8006EBA4(gobj);
    } else {
        Fighter_ChangeMotionState(gobj, ftCn_MS_SpecialAirSStart, Ft_MF_None, 0.0f, 1.0f, 0.0f,
                                  NULL);
        fp->self_vel.y = 0.0f;
        ftAnim_8006EBA4(gobj);
    }
}

/* +0564, [specialairs]: once per air time. The word is cleared again at once when the start
 * left her on the ground. */
void ftCn_SpecialAirS_Enter(HSD_GObj* gobj)
{
    Fighter* fp = GET_FIGHTER(gobj);
    Fighter* fp2;

    if (ftCn_Vars(fp)->specialairs_used == 1) {
        return;
    }
    ftCn_SpecialS_Enter(gobj);
    ftCn_Vars(fp)->specialairs_used = 1;
    fp2 = GET_FIGHTER(gobj);
    if (fp2->ground_or_air == GA_Ground) {
        ftCn_Vars(fp2)->specialairs_used = 0;
    }
}

/* +2F74: the kick, on the ground or in the air, from frame 0. */
static void ftCn_SpecialS_EnterLoop(HSD_GObj* gobj)
{
    Fighter* fp = GET_FIGHTER(gobj);

    if (fp->ground_or_air == GA_Ground) {
        Fighter_ChangeMotionState(gobj, ftCn_MS_SpecialSLoop, Ft_MF_None, 0.0f, 1.0f, 0.0f,
                                  NULL);
    } else {
        Fighter_ChangeMotionState(gobj, ftCn_MS_SpecialAirSLoop, Ft_MF_None, 0.0f, 1.0f, 0.0f,
                                  NULL);
    }
}

/* +19D8, the animation callback of 345 (and +1AE8 of 348, the same instructions). */
void ftCn_SpecialSStart_Anim(HSD_GObj* gobj)
{
    if (!ftAnim_IsFramesRemaining(gobj)) {
        ftCn_SpecialS_EnterLoop(gobj);
    }
}

/* +1A2C, the physics callback of 345 (and +1B3C of 348, the same instructions). */
void ftCn_SpecialSStart_Phys(HSD_GObj* gobj)
{
    ft_80085154(gobj);
    ft_80084FA8(gobj);
}

/* +1A5C, the collision callback of 345: off the ground, 348 from the same frame, airborne, no
 * vertical speed. */
void ftCn_SpecialSStart_Coll(HSD_GObj* gobj)
{
    Fighter* fp;

    if (ft_80082708(gobj)) {
        return;
    }
    fp = GET_FIGHTER(gobj);
    Fighter_ChangeMotionState(gobj, ftCn_MS_SpecialAirSStart, FTCN_SPECIALS_COLL_MF,
                              fp->cur_anim_frame, 1.0f, 0.0f, NULL);
    ftCommon_8007D5D4(fp);
    fp->self_vel.y = 0.0f;
}

/* +1B6C, the collision callback of 348: landed, 345 from the same frame, grounded, no vertical
 * speed. */
void ftCn_SpecialAirSStart_Coll(HSD_GObj* gobj)
{
    Fighter* fp;

    if ((int) ft_80081D0C(gobj) != 1) {
        return;
    }
    fp = GET_FIGHTER(gobj);
    Fighter_ChangeMotionState(gobj, ftCn_MS_SpecialSStart, FTCN_SPECIALS_COLL_MF,
                              fp->cur_anim_frame, 1.0f, 0.0f, NULL);
    ftCommon_8007D7FC(fp);
    fp->self_vel.y = 0.0f;
}

/* +21C4, the animation callback of 359: the end. */
void ftCn_SpecialSLoop_Anim(HSD_GObj* gobj)
{
    if (!ftAnim_IsFramesRemaining(gobj)) {
        Fighter_ChangeMotionState(gobj, ftCn_MS_SpecialSEnd, Ft_MF_None, 0.0f, 1.0f, 0.0f,
                                  NULL);
    }
}

/* +2234, the physics callback of 359 (and +2354 of 360, the same instructions): no vertical
 * speed, the attribute speed times the facing. */
void ftCn_SpecialSLoop_Phys(HSD_GObj* gobj)
{
    Fighter* fp = GET_FIGHTER(gobj);
    ftCn_DatAttrs* da = ftCn_Attrs(fp);

    fp->self_vel.y = 0.0f;
    fp->self_vel.x = fp->facing_dir * da->specials_vel_x;
}

/* +225C, the collision callback of 359: off the ground, 360 from the same frame, airborne. */
void ftCn_SpecialSLoop_Coll(HSD_GObj* gobj)
{
    Fighter* fp = GET_FIGHTER(gobj);

    if (ft_80082708(gobj)) {
        return;
    }
    Fighter_ChangeMotionState(gobj, ftCn_MS_SpecialAirSLoop, Ft_MF_None, fp->cur_anim_frame,
                              1.0f, 0.0f, NULL);
    ftCommon_8007D5D4(fp);
}

/* +22E4, the animation callback of 360: the air end. */
void ftCn_SpecialAirSLoop_Anim(HSD_GObj* gobj)
{
    if (!ftAnim_IsFramesRemaining(gobj)) {
        Fighter_ChangeMotionState(gobj, ftCn_MS_SpecialAirSEnd, Ft_MF_None, 0.0f, 1.0f, 0.0f,
                                  NULL);
    }
}

/* +237C, the collision callback of 360: the floor and ledge test with the facing as the
 * direction. On a floor it changes to 360 (its own number: mod bug, kept) and grounds her; with
 * no floor, the ledge catch test. */
void ftCn_SpecialAirSLoop_Coll(HSD_GObj* gobj)
{
    Fighter* fp = GET_FIGHTER(gobj);

    if (ft_CheckGroundAndLedge(gobj, (int) fp->facing_dir)) {
        Fighter_ChangeMotionState(gobj, ftCn_MS_SpecialAirSLoop, Ft_MF_None, fp->cur_anim_frame,
                                  1.0f, 0.0f, NULL);
        ftCommon_8007D7FC(fp);
        return;
    }
    ftCliffCommon_80081298(gobj);
}

/* +2468, the physics callback of 361 (and of 363). */
void ftCn_SpecialSEnd_Phys(HSD_GObj* gobj)
{
    ftCommon_8007E2FC(gobj);
    ft_80085154(gobj);
}

/* +2498, the collision callback of 361 (and of 363): off the ground, 362 from the same frame
 * with the swap flags, airborne. */
void ftCn_SpecialSEnd_Coll(HSD_GObj* gobj)
{
    Fighter* fp;

    if (ft_800827A0(gobj)) {
        return;
    }
    fp = GET_FIGHTER(gobj);
    Fighter_ChangeMotionState(gobj, ftCn_MS_SpecialAirSEnd, FTCN_SPECIALS_COLL_MF,
                              fp->cur_anim_frame, 1.0f, 0.0f, NULL);
    ftCommon_8007D5D4(fp);
}

/* +2568, the physics callback of 362: the attributes are read before the two physics steps,
 * then the horizontal friction of +34. */
void ftCn_SpecialAirSEnd_Phys(HSD_GObj* gobj)
{
    Fighter* fp = GET_FIGHTER(gobj);
    ftCn_DatAttrs* da = ftCn_Attrs(fp);

    ft_80085154(gobj);
    ft_80084EEC(gobj);
    ftCommon_CalcSelfAccel_Deaccel(fp, da->specialairsend_friction);
}

/* +25BC, the collision callback of 362: landed, 361 from the same frame with the swap flags,
 * grounded. */
void ftCn_SpecialAirSEnd_Coll(HSD_GObj* gobj)
{
    Fighter* fp;

    if ((int) ft_80081D0C(gobj) != 1) {
        return;
    }
    fp = GET_FIGHTER(gobj);
    Fighter_ChangeMotionState(gobj, ftCn_MS_SpecialSEnd, FTCN_SPECIALS_COLL_MF,
                              fp->cur_anim_frame, 1.0f, 0.0f, NULL);
    ftCommon_8007D7FC(fp);
}
