/* ACE's Sonic BM: the side special, a spin dash (states 345, 346, 360 to 363, 369), the file's
 * own code.
 *
 * On the ground: a start (345), then the charge (361) moving at 2.6465 a frame; the script's
 * window (cmd_vars[2] = 1) lets him go on while the stick is held forward past 0.275 (the window
 * closes, cmd_vars[2] = 2), else he brakes (362, the retail RunBrake callbacks); at the end of
 * the charge he runs (the common Run state). Off an edge: the air versions. In the air: a start
 * (346, standing still), the charge (360), then an end (363, special fall) or, when the charge
 * began on the ground, a release (369, a fall). Once per air time (fp+22EC); the air start also
 * uses up the up special and his jumps. */
#include "sonicbm.h"

#include <dolphin/os.h>

#include <melee/ef/types.h>
#include <melee/ft/fighter.h>
#include <melee/ft/ft_081B.h>
#include <melee/ft/ft_084E.h>
#include <melee/ft/ft_0892.h>
#include <melee/ft/ftanim.h>
#include <melee/ft/ftcommon.h>
#include <melee/ft/inlines.h>
#include <melee/ft/kinds/ftCommon/ftCo_Fall.h>
#include <melee/ft/kinds/ftCommon/ftCo_FallSpecial.h>
#include <melee/ft/kinds/ftCommon/ftCo_Landing.h>
#include <melee/ft/kinds/ftCommon/ftCo_RunBrake.h>
#include <melee/ft/types.h>
#include <sysdolphin/baselib/gobj.h>
#include <sysdolphin/baselib/jobj.h>

/* The state change flags of the ground to air swap (0xA at +3560 and +35E4). */
#define ftSc_MF_SpecialS_ToAir (Ft_MF_KeepGfx | Ft_MF_SkipHit)

/* +06F8 (slot 6, specials): once per air time; cmd_vars[2] cleared, state 345. */
void ftSc_SpecialS_Enter(HSD_GObj* gobj)
{
    Fighter* fp = GET_FIGHTER(gobj);

    if (ftSc_FV(fp)->side_used == 1) {
        return;
    }
    ftSc_FV(fp)->side_used = 1;
    fp->cmd_vars[2] = 0;
    Fighter_ChangeMotionState(gobj, ftSc_MS_SpecialS, Ft_MF_None, 0.0f, 1.0f, 0.0f, NULL);
}

/* +073C (slot 7, specialairs): once per air time; also marks the up special used, state 346,
 * and no jumps left. When used already the press only sets his facing from his model's Y
 * rotation, as the neutral special's. */
void ftSc_SpecialAirS_Enter(HSD_GObj* gobj)
{
    Fighter* fp = GET_FIGHTER(gobj);

    if (ftSc_FV(fp)->side_used != 1) {
        ftSc_FV(fp)->side_used = 1;
        ftSc_FV(fp)->up_used = 1;
        fp->cmd_vars[2] = 0;
        Fighter_ChangeMotionState(gobj, ftSc_MS_SpecialAirS, Ft_MF_None, 0.0f, 1.0f, 0.0f,
                                  NULL);
        fp->x1968_jumpsUsed = (u8) fp->co_attrs.max_jumps;
        return;
    }
    if (!(GET_JOBJ(gobj)->rotate.y <= 0.0f)) {
        fp->facing_dir = 1.0f;
    } else {
        fp->facing_dir = -1.0f;
    }
}

/* +0EFC: state 345, animation: at the end, the charge (361), begun on the ground (flag 1). */
void ftSc_SpecialS_Anim(HSD_GObj* gobj)
{
    Fighter* fp = GET_FIGHTER(gobj);

    if (ftAnim_IsFramesRemaining(gobj)) {
        return;
    }
    Fighter_ChangeMotionState(gobj, ftSc_MS_SpecialSCharge, Ft_MF_None, 0.0f, 1.0f, 0.0f,
                              NULL);
    ftSc_MV(fp)->flag = 1;
}

/* +0F88: state 345, physics: a branch to the retail ground physics. */
void ftSc_SpecialS_Phys(HSD_GObj* gobj)
{
    ft_80084F3C(gobj);
}

/* +350C: off the floor in 345 or 361. Into the air; 345 goes on as 346 and 361 as 360 at the
 * same frame (flags 0xA), any other state falls. The spin ball is hidden when the ground charge
 * goes on in the air.
 *
 * The console hides the ball through fp+22D8 without a check: that is the last ball spawned,
 * which may have been destroyed since (efLib_DestroyAll). The native guard skips a NULL pointer
 * only; a destroyed ball is not detected (as on the console). */
static void ftSc_SpecialS_ToAir(HSD_GObj* gobj)
{
    Fighter* fp = GET_FIGHTER(gobj);

    ftCommon_8007D5D4(fp);
    if (fp->motion_id == ftSc_MS_SpecialS) {
        Fighter_ChangeMotionState(gobj, ftSc_MS_SpecialAirS, ftSc_MF_SpecialS_ToAir,
                                  fp->cur_anim_frame, 1.0f, 0.0f, NULL);
        ftSc_FV(fp)->side_used = 1;
        return;
    }
    if (fp->motion_id == ftSc_MS_SpecialSCharge) {
        EF_Effect* fx;
        Fighter_ChangeMotionState(gobj, ftSc_MS_SpecialAirSCharge, ftSc_MF_SpecialS_ToAir,
                                  fp->cur_anim_frame, 1.0f, 0.0f, NULL);
        fx = ftSc_FV(fp)->spin_fx;
        if (fx != NULL && fx->gobj != NULL) {
            HSD_JObjSetFlagsAll(fx->gobj->hsd_obj, JOBJ_HIDDEN);
        }
        ftSc_FV(fp)->side_used = 1;
        return;
    }
    ftCo_Fall_Enter(gobj);
    ftSc_FV(fp)->side_used = 1;
}

/* +0F8C: states 345 and 361, collision. */
void ftSc_SpecialS_Coll(HSD_GObj* gobj)
{
    if (!ft_80082708(gobj)) {
        ftSc_SpecialS_ToAir(gobj);
    }
}

/* +0FD8: state 346, animation: at the end, the air charge (360), begun in the air (flag 0). */
void ftSc_SpecialAirS_Anim(HSD_GObj* gobj)
{
    Fighter* fp = GET_FIGHTER(gobj);

    if (ftAnim_IsFramesRemaining(gobj)) {
        return;
    }
    Fighter_ChangeMotionState(gobj, ftSc_MS_SpecialAirSCharge, Ft_MF_None, 0.0f, 1.0f, 0.0f,
                              NULL);
    ftSc_MV(fp)->flag = 0;
}

/* +1068: state 346, physics: no speed. */
void ftSc_SpecialAirS_Phys(HSD_GObj* gobj)
{
    Fighter* fp = GET_FIGHTER(gobj);

    fp->self_vel.x = 0.0f;
    fp->self_vel.y = 0.0f;
}

/* +1080: states 346, 360, 363 and 369, collision. On landing, except in 346 and 360: grounded
 * and the special landing with 28 frames. In 346 and 360 a landing does nothing (he slides on
 * as an air state; kept). */
void ftSc_SpecialAirS_Coll(HSD_GObj* gobj)
{
    Fighter* fp;

    if (!ft_80081D0C(gobj)) {
        return;
    }
    fp = GET_FIGHTER(gobj);
    if (fp->motion_id == ftSc_MS_SpecialAirS || fp->motion_id == ftSc_MS_SpecialAirSCharge) {
        return;
    }
    ftCommon_8007D6A4(fp);
    ftCo_LandingFallSpecial_Enter(gobj, false, 28.0f);
}

/* +2110: state 360, animation: at the end, 369 when the charge began on the ground, else 363
 * (flags 2: the effects stay). */
void ftSc_SpecialAirSCharge_Anim(HSD_GObj* gobj)
{
    Fighter* fp = GET_FIGHTER(gobj);

    if (ftAnim_IsFramesRemaining(gobj)) {
        return;
    }
    if (ftSc_MV(fp)->flag == 1) {
        Fighter_ChangeMotionState(gobj, ftSc_MS_SpecialAirSRelease, Ft_MF_KeepGfx, 0.0f, 1.0f,
                                  0.0f, NULL);
    } else {
        Fighter_ChangeMotionState(gobj, ftSc_MS_SpecialAirSEnd, Ft_MF_KeepGfx, 0.0f, 1.0f, 0.0f,
                                  NULL);
    }
}

/* +219C (and +22F4, the same eight words): states 360 and 361, physics: x speed facing *
 * 2.6465 (in double). */
void ftSc_SpecialSCharge_Phys(HSD_GObj* gobj)
{
    Fighter* fp = GET_FIGHTER(gobj);

    fp->self_vel.x = (float) (fp->facing_dir * 2.6465);
}

/* +21BC: state 361, animation. In the script's window (cmd_vars[2] = 1): the stick past 0.275
 * the way he faces closes the window (2) and he goes on, anything else brakes (362) keeping his
 * speed as ground speed. Out of the window: at the end, the common run with that speed. Both
 * with flags 0x20. */
void ftSc_SpecialSCharge_Anim(HSD_GObj* gobj)
{
    Fighter* fp = GET_FIGHTER(gobj);
    float x;

    if (fp->cmd_vars[2] == 1) {
        x = fp->input.lstick[0].x;
        if (x > 0.275) {
            if (fp->facing_dir > 0.0f) {
                goto keep;
            }
            goto brake;
        }
        if (x >= -0.275) {
            goto brake;
        }
        if (fp->facing_dir >= 0.0f) {
            goto brake;
        }
    keep:
        fp->cmd_vars[2] = 2;
        if (ftAnim_IsFramesRemaining(gobj)) {
            return;
        }
        goto run;
    brake:
        fp->gr_vel = fp->self_vel.x;
        Fighter_ChangeMotionState(gobj, ftSc_MS_SpecialSBrake, Ft_MF_SkipAnimVel, 0.0f, 1.0f,
                                  0.0f, NULL);
        return;
    }
    if (ftAnim_IsFramesRemaining(gobj)) {
        return;
    }
run:
    fp->gr_vel = fp->self_vel.x;
    Fighter_ChangeMotionState(gobj, ftCo_MS_Run, Ft_MF_SkipAnimVel, 0.0f, 1.0f, 0.0f, NULL);
}

/* +2314: state 362, animation: at the end, the retail wait. */
void ftSc_SpecialSBrake_Anim(HSD_GObj* gobj)
{
    if (!ftAnim_IsFramesRemaining(gobj)) {
        ft_8008A2BC(gobj);
    }
}

/* +2360: state 362, interrupt: the retail RunBrake interrupt (0x800CADB0, through a register)
 * once the script sets cmd_vars[1]. */
void ftSc_SpecialSBrake_IASA(HSD_GObj* gobj)
{
    if (GET_FIGHTER(gobj)->cmd_vars[1] == 1) {
        ftCo_RunBrake_IASA(gobj);
    }
}

/* +2380: state 363, animation: at the end, the special fall (mobility 1.0, landing 28). */
void ftSc_SpecialAirSEnd_Anim(HSD_GObj* gobj)
{
    if (!ftAnim_IsFramesRemaining(gobj)) {
        ftCo_80096900(gobj, 0, 1, true, 1.0f, 28.0f);
    }
}

/* +23E8: states 363 and 369, physics: the retail air physics, then x speed times 0.92 (in
 * double). */
void ftSc_SpecialAirSEnd_Phys(HSD_GObj* gobj)
{
    Fighter* fp = GET_FIGHTER(gobj);

    ft_80084EEC(gobj);
    fp->self_vel.x = (float) (fp->self_vel.x * 0.92);
}

/* +28AC: state 369, animation: at the end, the retail fall. */
void ftSc_SpecialAirSRelease_Anim(HSD_GObj* gobj)
{
    if (!ftAnim_IsFramesRemaining(gobj)) {
        ftCo_Fall_Enter(gobj);
    }
}
