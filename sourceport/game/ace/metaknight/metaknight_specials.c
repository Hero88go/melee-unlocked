/* ACE's Meta Knight: the side special (states 346 to 350), his own code.
 *
 *   346  start on the ground; 349 start in the air (it holds him still)
 *   347  the drill: at 1.5 along the angle fp+2364 (0 facing right, pi facing left); the stick
 *        up or down past 0.275 turns the angle by 0.02 a frame (times his facing) and tilts the
 *        top joint's x rotation by 0.02 the other way. At the end: 350 in the air, 348 on the
 *        ground
 *   348  end on the ground
 *   350  end in the air, into the special fall (mobility 1, landing lag 30); while the script
 *        sets cmd_vars[1] it can grab a ledge */
#include "metaknight.h"

#include <math.h>

#include <melee/ft/fighter.h>
#include <melee/ft/ft_081B.h>
#include <melee/ft/ft_084E.h>
#include <melee/ft/ft_0892.h>
#include <melee/ft/ftanim.h>
#include <melee/ft/ftcommon.h>
#include <melee/ft/inlines.h>
#include <melee/ft/kinds/ftCommon/ftCo_Fall.h>
#include <melee/ft/kinds/ftCommon/ftCo_FallSpecial.h>
#include <melee/ft/types.h>
#include <sysdolphin/baselib/jobj.h>

static void ftMk_SpecialAirS_Land(HSD_GObj* gobj);

/* +0528 (slot 6, specials) */
void ftMk_SpecialS_Enter(HSD_GObj* gobj)
{
    FTMK_CHANGE(gobj, ftMk_MS_SpecialSStart);
}

/* +054C (slot 7, specialairs) */
void ftMk_SpecialAirS_Enter(HSD_GObj* gobj)
{
    FTMK_CHANGE(gobj, ftMk_MS_SpecialAirSStart);
}

/* +17D8: animation of 346 and 349: at the end the drill, every jump given back, the angle 0 (his
 * facing above 0) or pi. */
void ftMk_SpecialSStart_Anim(HSD_GObj* gobj)
{
    Fighter* fp = GET_FIGHTER(gobj);

    if (ftAnim_IsFramesRemaining(gobj)) {
        return;
    }
    FTMK_CHANGE(gobj, ftMk_MS_SpecialSDrill);
    fp->x1968_jumpsUsed = (u8) fp->co_attrs.max_jumps;
    if (!(fp->facing_dir <= 0.0f)) { /* ble: a NaN takes 0 */
        ftMk_MVars(fp)->x2364.f = 0.0f;
    } else {
        ftMk_MVars(fp)->x2364.f = 3.1415927f;
    }
}

/* +189C: collision of 346 and 348: off the floor, the common fall. */
void ftMk_SpecialS_GroundColl(HSD_GObj* gobj)
{
    if (ft_80082708(gobj) == 0) {
        ftCo_Fall_Enter(gobj);
    }
}

/* +18E8: animation of 347: at the end cmd_vars[1] = 0 (the result of the frame test), then 350
 * in the air or 348 on the ground. */
void ftMk_SpecialSDrill_Anim(HSD_GObj* gobj)
{
    Fighter* fp = GET_FIGHTER(gobj);
    const bool remaining = ftAnim_IsFramesRemaining(gobj);

    if (remaining) {
        return;
    }
    fp->cmd_vars[1] = remaining;
    if (fp->ground_or_air == GA_Air) {
        FTMK_CHANGE(gobj, ftMk_MS_SpecialAirSEnd);
    } else {
        FTMK_CHANGE(gobj, ftMk_MS_SpecialSEnd);
    }
}

/* +1978: physics of 347. Each turn is one double subtraction or addition for the joint and one
 * fused double multiply-add for the angle, each rounded to single. */
void ftMk_SpecialSDrill_Phys(HSD_GObj* gobj)
{
    Fighter* fp = GET_FIGHTER(gobj);
    ftMk_MotionVars* mv = ftMk_MVars(fp);
    HSD_JObj* jobj = fp->parts[0].joint;
    const float ly = fp->input.lstick[0].y;

    if (!(ly <= 0.275)) { /* ble: a NaN steers up */
        const float rot = (float) ((double) jobj->rotate.x - 0.02);
        const float angle =
            (float) __builtin_fma((double) fp->facing_dir, 0.02, (double) mv->x2364.f);
        mv->x2364.f = angle;
        jobj->rotate.x = rot;
        if (fp->ground_or_air == GA_Ground) {
            /* +1AA0 */
            ftCommon_8007D5D4(fp);
        }
    } else if (!(ly >= -0.275)) {
        /* +1A24 */
        const float rot = (float) ((double) jobj->rotate.x + 0.02);
        const float angle =
            (float) __builtin_fma((double) -fp->facing_dir, 0.02, (double) mv->x2364.f);
        mv->x2364.f = angle;
        jobj->rotate.x = rot;
    }

    /* +19E4 */
    fp->self_vel.y = sinf(mv->x2364.f) * 1.5f;
    fp->self_vel.x = cosf(mv->x2364.f) * 1.5f;
}

/* +1AEC: collision of 347: in the air a landing (or a ledge) puts him on the ground; on the
 * ground a walk-off puts him in the air. The state stays. */
void ftMk_SpecialSDrill_Coll(HSD_GObj* gobj)
{
    Fighter* fp = GET_FIGHTER(gobj);

    if (fp->ground_or_air == GA_Air) {
        /* +1B44 */
        if (ft_CheckGroundAndLedge(gobj, 0)) {
            ftCommon_8007D6A4(fp);
        }
    } else {
        if (ft_80082708(gobj) == 0) {
            ftCommon_8007D5D4(fp);
        }
    }
}

/* +1B6C: animation of 348. */
void ftMk_SpecialSEnd_Anim(HSD_GObj* gobj)
{
    if (!ftAnim_IsFramesRemaining(gobj)) {
        ft_8008A2BC(gobj);
    }
}

/* +1BB8: physics of 349: no speed. */
void ftMk_SpecialAirSStart_Phys(HSD_GObj* gobj)
{
    Fighter* fp = GET_FIGHTER(gobj);

    fp->self_vel.x = 0.0f;
    fp->self_vel.y = 0.0f;
}

/* +1BD0: collision of 349: on landing, +3210. */
void ftMk_SpecialAirSStart_Coll(HSD_GObj* gobj)
{
    if (ft_80081D0C(gobj)) {
        ftMk_SpecialAirS_Land(gobj);
    }
}

/* +1C1C: animation of 350: at the end the special fall (no interrupt, mobility 1, landing lag
 * 30), every jump given back. */
void ftMk_SpecialAirSEnd_Anim(HSD_GObj* gobj)
{
    Fighter* fp = GET_FIGHTER(gobj);

    if (ftAnim_IsFramesRemaining(gobj)) {
        return;
    }
    ftCo_80096900(gobj, 0, 1, false, 1.0f, 30.0f);
    fp->x1968_jumpsUsed = (u8) fp->co_attrs.max_jumps;
}

/* +1CA0: physics of 350: a branch to ft_80084EEC. */
void ftMk_SpecialAirSEnd_Phys(HSD_GObj* gobj)
{
    ft_80084EEC(gobj);
}

/* +1CA4: collision of 350: with cmd_vars[1] == 1 the ledge-grabbing landing test
 * ft_800831CC(gobj, NULL, +3210) (the file passes no floor callback: the collision code skips a
 * NULL one); else a plain landing test with +3210. */
void ftMk_SpecialAirSEnd_Coll(HSD_GObj* gobj)
{
    Fighter* fp = GET_FIGHTER(gobj);

    if ((s32) fp->cmd_vars[1] == 1) {
        /* +1D00 */
        ft_800831CC(gobj, NULL, ftMk_SpecialAirS_Land);
        return;
    }
    if (ft_80081D0C(gobj)) {
        ftMk_SpecialAirS_Land(gobj);
    }
}

/* +3210: the landing of 349 and 350: on the ground, 349 goes on as 346 and 350 as 348 from the
 * same frame. */
static void ftMk_SpecialAirS_Land(HSD_GObj* gobj)
{
    Fighter* fp = GET_FIGHTER(gobj);

    ftCommon_8007D7FC(fp);
    if (fp->motion_id == ftMk_MS_SpecialAirSStart) {
        FTMK_CHANGE_AT_FRAME(gobj, fp, ftMk_MS_SpecialSStart);
    } else if (fp->motion_id == ftMk_MS_SpecialAirSEnd) {
        FTMK_CHANGE_AT_FRAME(gobj, fp, ftMk_MS_SpecialSEnd);
    }
}
