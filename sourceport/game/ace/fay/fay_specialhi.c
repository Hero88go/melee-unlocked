/* ACE's Fay: the up special (347 to 351).
 *
 * 347 / 348: a charge. fp+22DC is the aim (pi/2 at the start, up); the stick tilts it by 0.01 a
 * frame between 72 and 108 degrees, she turns around when it crosses straight up, and her root
 * joint leans with it. After 20 frames, B let go (or 35 frames whatever B) launches her: 349,
 * the speed 4.0 + 0.2 for every frame charged past 20, at the aim (half of it sideways). At
 * frame 6 of the launch she goes to 351, which ends in the special fall (landing lag 20).
 * 350 is a row nothing of hers enters.
 *
 * The charge's air physics and collision, and the launch's collision, never land her: the
 * common air collision's result is not looked at (only the ledge grab is). Kept. */
#include "fay.h"

#include <math.h>

#include <melee/ft/fighter.h>
#include <melee/ft/ft_081B.h>
#include <melee/ft/ftanim.h>
#include <melee/ft/ftcliffcommon.h>
#include <melee/ft/ftcommon.h>
#include <melee/ft/inlines.h>
#include <melee/ft/kinds/ftCommon/ftCo_AirCatch.h>
#include <melee/ft/kinds/ftCommon/ftCo_FallSpecial.h>
#include <melee/ft/kinds/ftCommon/ftCo_Landing.h>
#include <melee/ft/types.h>
#include <sysdolphin/baselib/controller.h>
#include <sysdolphin/baselib/gobj.h>
#include <sysdolphin/baselib/jobj.h>

/* +0CE4: pi/2 in single, the aim at the start. +332C: pi/2 in double, the aim's tests. */
#define FTFY_HI_START 0x1.921fb6p+0f
#define FTFY_HI_UP 1.5707963267948966

/* +07D8 and +08FC (slots 8 and 9): with the stick past 0.2 sideways (in double) she turns to it
 * first. The state; the vertical and depth speeds zeroed, the aim pi/2 and the charge count 0;
 * then the horizontal speed she keeps is brought toward 0 by 0.2 (or by 0.3 when it is over her
 * air drift maximum), in double. */
static void ftFy_SpecialHi_EnterState(HSD_GObj* gobj, FtMotionId msid)
{
    Fighter* fp = GET_FIGHTER(gobj);
    ftFy_FighterVars* fv = ftFy_FV(fp);
    float stick = fp->input.lstick[0].x;
    float vx;

    if (stick < 0.0f) {
        stick = -stick;
    }
    if ((double) stick > 0.2) {
        /* +08CC */
        ftCommon_UpdateFacing(fp);
    }
    Fighter_ChangeMotionState(gobj, msid, Ft_MF_None, 0.0f, 1.0f, 0.0f, NULL);
    vx = fp->self_vel.x;
    fp->self_vel.y = 0.0f;
    fp->self_vel.z = 0.0f;
    fv->hi_angle = FTFY_HI_START;
    fv->hi_frames = 0;
    if (vx == 0.0f) {
        return;
    }
    if (vx < 0.0f) {
        /* +08B4 */
        if (-vx <= fp->co_attrs.air_drift_max) {
            fp->self_vel.x = (float) (0.2 + (double) vx);
        } else {
            fp->self_vel.x = (float) (0.3 + (double) vx);
        }
    } else if (vx > fp->co_attrs.air_drift_max) {
        /* +08E4 */
        fp->self_vel.x = (float) (-0.3 + (double) vx);
    } else {
        fp->self_vel.x = (float) (-0.2 + (double) vx);
    }
}

/* +07D8 (slot 8) */
void ftFy_SpecialHi_Enter(HSD_GObj* gobj)
{
    ftFy_SpecialHi_EnterState(gobj, ftFy_MS_SpecialHi);
}

/* +08FC (slot 9) */
void ftFy_SpecialAirHi_Enter(HSD_GObj* gobj)
{
    ftFy_SpecialHi_EnterState(gobj, ftFy_MS_SpecialAirHi);
}

/* +1AF8: 347's and 348's animation callback.
 *   The aim: stick under -0.1 and the aim under 1.885 (108 degrees): +0.01 (a turn first when it
 *     is exactly pi/2 in double, which the single pi/2 of the start never is); stick over 0.1
 *     and the aim over 1.2566 (72 degrees): -0.01 (the same turn test). All in double.
 *   The lean: the root joint's (part 0) rotation x is pi/2 - aim facing right, aim - pi/2
 *     facing left (double, rounded), a plain store.
 *   The count: up to 19 only counts. From 20, B held counts on to 34; B let go, or 35 and on,
 *     launches: the facing from the stick's sign (unchanged at 0), 349, no horizontal speed,
 *     n = max(count, 20) - 20, speed = n * 0.2 + 4.0 (fused, double), vertical sin(aim) * speed,
 *     horizontal (cos(aim) * 0.5) * speed (double). The count goes up by one in every case. */
void ftFy_SpecialHi_Anim(HSD_GObj* gobj)
{
    Fighter* fp = GET_FIGHTER(gobj);
    ftFy_FighterVars* fv = ftFy_FV(fp);
    float stick = fp->input.lstick[0].x;
    float aim = fv->hi_angle;
    HSD_JObj* root;
    s32 count;

    if (!((double) stick >= -0.1)) {
        if ((double) aim < 1.8849555921538759) {
            /* +1D18 */
            if ((double) aim == FTFY_HI_UP) {
                ftCommon_UpdateFacing(fp);
                aim = fv->hi_angle;
            }
            aim = (float) ((double) aim + 0.01);
            fv->hi_angle = aim;
        }
    } else if (!((double) stick <= 0.1) && !((double) aim <= 1.2566370614359172)) {
        /* +1C98 */
        if ((double) aim == FTFY_HI_UP) {
            ftCommon_UpdateFacing(fp);
            aim = fv->hi_angle;
        }
        aim = (float) ((double) aim - 0.01);
        fv->hi_angle = aim;
    }

    /* +1B34 */
    root = fp->parts[0].joint;
    if (fp->facing_dir < 0.0f) {
        root->rotate.x = (float) ((double) aim - FTFY_HI_UP);
    } else {
        root->rotate.x = (float) (FTFY_HI_UP - (double) aim);
    }

    count = fv->hi_frames;
    if (count > 19) {
        if (!(fp->input.held_buttons[0] & HSD_PAD_B) || count > 34) {
            /* +1B84 */
            float s;
            float c;
            double speed;
            s32 n;

            stick = fp->input.lstick[0].x;
            if (stick != 0.0f) {
                if (stick <= 0.0f) {
                    fp->facing_dir = -1.0f;
                } else {
                    fp->facing_dir = 1.0f;
                }
            }
            Fighter_ChangeMotionState(gobj, ftFy_MS_SpecialHiLaunch, Ft_MF_None, 0.0f, 1.0f, 0.0f,
                                      NULL);
            count = fv->hi_frames;
            aim = fv->hi_angle;
            fp->self_vel.x = 0.0f;
            n = (count < 20 ? 20 : count) - 20;
            s = sinf(aim);
            speed = __builtin_fma((double) n, 0.2, 4.0);
            fp->self_vel.y = (float) ((double) s * speed);
            c = cosf(aim);
            fp->self_vel.x = (float) (((double) c * 0.5) * speed);
        }
    }
    /* +1C70 */
    fv->hi_frames = count + 1;
}

/* +1DAC: 348's physics: from frame 16 the vertical speed loses 0.015 a frame (in double), then
 * the common air friction (ftCommon_CalcSelfAccel_Deaccel with her air friction). */
void ftFy_SpecialAirHi_Phys(HSD_GObj* gobj)
{
    Fighter* fp = GET_FIGHTER(gobj);

    if (fp->cur_anim_frame >= 16.0f) {
        fp->self_vel.y = (float) ((double) fp->self_vel.y - 0.015);
    }
    ftCommon_CalcSelfAccel_Deaccel(fp, fp->co_attrs.aerial_friction);
}

/* +1DE8 and +1F28 (the same 17 words): 348's and 349's collision. The common air collision is
 * run and its result dropped; falling (vertical speed under 0), the ledge grab. */
void ftFy_SpecialAirHi_Coll(HSD_GObj* gobj)
{
    Fighter* fp = GET_FIGHTER(gobj);

    (void) ft_80081D0C(gobj);
    if (fp->self_vel.y < 0.0f) {
        if (ftCo_800C3A14(gobj) && ft_80082E3C(gobj) == NULL) {
            ftCliffCommon_80081370(gobj);
        }
    }
}

/* +1E70: 349's animation callback: from frame 6, 351, and atan2f of the speed (with the
 * horizontal part made positive) is computed and dropped (a tail call whose result nobody
 * reads). */
void ftFy_SpecialHiLaunch_Anim(HSD_GObj* gobj)
{
    Fighter* fp = GET_FIGHTER(gobj);
    float vx;
    float vy;

    if (!(fp->cur_anim_frame >= 6.0f)) {
        return;
    }
    Fighter_ChangeMotionState(gobj, ftFy_MS_SpecialHiFall, Ft_MF_None, 0.0f, 1.0f, 0.0f, NULL);
    vx = fp->self_vel.x;
    vy = fp->self_vel.y;
    if (vx < 0.0f) {
        vx = -vx;
    }
    (void) atan2f(vy, vx);
}

/* +1F00, +2018, +201C, +208C: each a lone blr. */
void ftFy_SpecialHi_Nothing(HSD_GObj* gobj)
{
    (void) gobj;
}

/* +1F04: 349's physics: the vertical speed damped by 0.95 (in double), then the common air
 * friction. */
void ftFy_SpecialHiLaunch_Phys(HSD_GObj* gobj)
{
    Fighter* fp = GET_FIGHTER(gobj);

    fp->self_vel.y = (float) ((double) fp->self_vel.y * 0.95);
    ftCommon_CalcSelfAccel_Deaccel(fp, fp->co_attrs.aerial_friction);
}

/* +1FB0: 350's animation callback: the special fall at the end (no interrupt flag 0, the
 * second argument 1, interrupts allowed, mobility 0.0, landing lag 20). */
void ftFy_SpecialHiUnused_Anim(HSD_GObj* gobj)
{
    if (!ftAnim_IsFramesRemaining(gobj)) {
        ftCo_80096900(gobj, 0, 1, true, 0.0f, 20.0f);
    }
}

/* +2020: 350's collision: the common air collision (a tail call). */
void ftFy_SpecialHiUnused_Coll(HSD_GObj* gobj)
{
    (void) ft_80081D0C(gobj);
}

/* +2024: 351's animation callback: the special fall at the end, mobility 0.1 (single). */
void ftFy_SpecialHiFall_Anim(HSD_GObj* gobj)
{
    if (!ftAnim_IsFramesRemaining(gobj)) {
        ftCo_80096900(gobj, 0, 1, true, 0.1f, 20.0f);
    }
}

/* +2090: 351's physics. The root joint's lean decays (rotation x * 0.95 in double); the stick
 * drifts her (horizontal speed + stick x * air drift stick factor, fused); up to frame 7 the
 * vertical speed is damped by 0.9 (in double), after it the common fall and its basic
 * friction. */
void ftFy_SpecialHiFall_Phys(HSD_GObj* gobj)
{
    HSD_JObj* jobj = gobj->hsd_obj;
    Fighter* fp = GET_FIGHTER(gobj);

    jobj->rotate.x = (float) ((double) jobj->rotate.x * 0.95);
    fp->self_vel.x = MU_FMADDS(fp->input.lstick[0].x, fp->co_attrs.air_drift_stick_mul,
                               fp->self_vel.x);
    if (!(fp->cur_anim_frame <= 7.0f)) {
        /* +20F8 */
        ftCommon_Fall(fp, fp->co_attrs.gravity, fp->co_attrs.terminal_velocity);
        ftCommon_FallBasic(fp);
        return;
    }
    fp->self_vel.y = (float) ((double) fp->self_vel.y * 0.9);
}

/* +2128: 351's collision. Landing: ftCommon_8007D7FC and the special landing (no interrupt,
 * lag 20); then, when falling, the ledge grab (also right after the landing, as the file
 * tests the speed again). */
void ftFy_SpecialHiFall_Coll(HSD_GObj* gobj)
{
    Fighter* fp = GET_FIGHTER(gobj);

    if (ft_80081D0C(gobj) != false) {
        /* +217C */
        ftCommon_8007D7FC(fp);
        ftCo_LandingFallSpecial_Enter(gobj, false, 20.0f);
    }
    if (fp->self_vel.y < 0.0f) {
        /* +21AC */
        if (ftCo_800C3A14(gobj) && ft_80082E3C(gobj) == NULL) {
            ftCliffCommon_80081370(gobj);
        }
    }
}
