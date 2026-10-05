/* ACE's Zero: the up special (states 356 and 357; +1234 to +17D0 of the "ftFunction" block of
 * PlZx.dat).
 *
 * Both entries are Link's (ftLk_SpecialHi_Enter and ftLk_SpecialAirHi_Enter, the m-ex defaults of
 * slots 8 and 9), with Link's effect callback. The ground state runs Link's callbacks through
 * his own wrappers and becomes the air state at frame 3; the air state has his own physics.
 * Three retail calls are made by absolute address in the file (0x800EBC10, 0x800EBD30,
 * 0x800EBCB0): ftLk_SpecialHi_Anim, ftLk_SpecialHi_Phys and ftLk_SpecialAirHi_IASA. */
#include "zero.h"

#include <melee/ft/fighter.h>
#include <melee/ft/ftcommon.h>
#include <melee/ft/inlines.h>
#include <melee/ft/kinds/ftCommon/ftCo_FallSpecial.h>
#include <melee/ft/kinds/ftLink/ftlinkspecialhi.h>
#include <melee/ft/types.h>

#define FTZX_STICK_TILT 0.3 /* +20C0 and +20C8, doubles */

/* +1234 "SpecialHi_Anim" (state 356): from frame 3 the air state at the same frame (no flags
 * kept, no air bookkeeping), then Link's ftLk_SpecialHi_Anim either way. */
void ftZx_SpecialHi_Anim(HSD_GObj* gobj)
{
    Fighter* fp = GET_FIGHTER(gobj);
    float frame = fp->cur_anim_frame;

    if (frame >= 3.0f) {
        Fighter_ChangeMotionState(gobj, ftZx_MS_SpecialAirHi, Ft_MF_None, frame, 1.0f, 0.0f,
                                  NULL);
    }
    ftLk_SpecialHi_Anim(gobj);
}

/* +12B0 "SpecialHi_Phys" (state 356): Link's ftLk_SpecialHi_Phys. In the air that is Link's
 * ftLk_SpecialAirHi_Phys, which reads three fields of a ftLk_DatAttrs (NOTES.md, "Link's
 * block"). */
void ftZx_SpecialHi_Phys(HSD_GObj* gobj)
{
    ftLk_SpecialHi_Phys(gobj);
}

/* +12DC "SpecialAirHi_IASA" (state 357): Link's ftLk_SpecialAirHi_IASA, which is empty. */
void ftZx_SpecialAirHi_IASA(HSD_GObj* gobj)
{
    ftLk_SpecialAirHi_IASA(gobj);
}

/* The start of the first block (+1538): fall at 0.05 up to 2, then the rise speed 1.75 written
 * over it. The later blocks do the same with their sideways speed written in between. */
static void ftZx_SpecialAirHi_Rise(Fighter* fp)
{
    ftCommon_Fall(fp, 0.05f, 2.0f);
    fp->self_vel.y = 1.75f;
}

/* +1308 "SpecialAirHi_Phys" (state 357). No common physics call. By the frame:
 *   under 6      rise (1.75 a frame) at 0.2 times his facing; a stick past 0.3 either way turns
 *                him and starts the state again at the current frame
 *   over 6, up to 10   by his facing and the stick, rise with a fixed sideways speed:
 *                facing left:  stick left -0.65, stick right 0.65
 *                facing right: stick right 0.6, stick left 0.6 and the state started again
 *   20           nothing
 *   21 and on    0.2 times his facing, fall at 0.11 up to 2; at frame 38 the special fall
 *                (mobility 1, landing lag 20)
 * Between those windows nothing is written and he keeps the velocity he has. The file's branch
 * order is kept; the labels name its offsets. The sideways speed is written before the fall
 * call in two blocks and after it in the others, and ftCommon_CalcSelfAccel_DriftSimple(fp, 0,
 * 0.5, 0.5) follows each rise. */
void ftZx_SpecialAirHi_Phys(HSD_GObj* gobj)
{
    Fighter* fp = GET_FIGHTER(gobj);
    float frame;
    float stick;

    if (fp->cur_anim_frame < 6.0f) {
        /* +1530 */
        ftZx_SpecialAirHi_Rise(fp);
        fp->self_vel.x = (float) ((double) fp->facing_dir * 0.2);
        ftCommon_CalcSelfAccel_DriftSimple(fp, 0.0f, 0.5f, 0.5f);
        stick = fp->input.lstick[0].x;
        if ((double) stick >= FTZX_STICK_TILT || !((double) stick > -FTZX_STICK_TILT)) {
            /* +15BC */
            ftCommon_UpdateFacing(fp);
            Fighter_ChangeMotionState(gobj, ftZx_MS_SpecialAirHi, Ft_MF_None,
                                      fp->cur_anim_frame, 1.0f, 0.0f, NULL);
        }
    }

    /* +1330 */
    if (fp->facing_dir != -1.0f) {
        goto facing_right;
    }
    frame = fp->cur_anim_frame;
    if (!(frame > 6.0f)) {
        return;
    }
    if (frame > 10.0f) {
        goto late;
    }
    stick = fp->input.lstick[0].x;
    if ((double) stick >= FTZX_STICK_TILT) {
        /* +1604 */
        ftCommon_Fall(fp, 0.05f, 2.0f);
        fp->self_vel.x = 0.65f;
        fp->self_vel.y = 1.75f;
        ftCommon_CalcSelfAccel_DriftSimple(fp, 0.0f, 0.5f, 0.5f);
        if (fp->cur_anim_frame > 10.0f) {
            goto facing_right;
        }
        if (!((double) fp->input.lstick[0].x > -FTZX_STICK_TILT)) {
            goto left_push;
        }
        if (fp->facing_dir != 1.0f) {
            return;
        }
        /* +140C */
        frame = fp->cur_anim_frame;
        if (!(frame > 6.0f)) {
            return;
        }
        if (frame > 10.0f) {
            goto late;
        }
        goto facing_right_stick;
    }
    if (!((double) stick <= -FTZX_STICK_TILT)) {
        return;
    }

left_push:
    /* +13A4 */
    ftCommon_Fall(fp, 0.05f, 2.0f);
    fp->self_vel.x = -0.65f;
    fp->self_vel.y = 1.75f;
    ftCommon_CalcSelfAccel_DriftSimple(fp, 0.0f, 0.5f, 0.5f);

facing_right:
    /* +13F8 */
    if (fp->facing_dir != 1.0f) {
        goto late;
    }
    frame = fp->cur_anim_frame;
    if (!(frame > 6.0f)) {
        return;
    }
    if (frame > 10.0f) {
        goto late;
    }

facing_right_stick:
    /* +1434 */
    if ((double) fp->input.lstick[0].x >= FTZX_STICK_TILT) {
        /* +16B4 */
        ftCommon_Fall(fp, 0.05f, 2.0f);
        fp->self_vel.x = 0.6f;
        fp->self_vel.y = 1.75f;
        ftCommon_CalcSelfAccel_DriftSimple(fp, 0.0f, 0.5f, 0.5f);
        if (fp->cur_anim_frame > 10.0f) {
            goto late;
        }
    }
    /* +144C */
    if ((double) fp->input.lstick[0].x > -FTZX_STICK_TILT) {
        return;
    }
    ftCommon_Fall(fp, 0.05f, 2.0f);
    fp->self_vel.x = 0.6f;
    fp->self_vel.y = 1.75f;
    ftCommon_CalcSelfAccel_DriftSimple(fp, 0.0f, 0.5f, 0.5f);
    Fighter_ChangeMotionState(gobj, ftZx_MS_SpecialAirHi, Ft_MF_None, fp->cur_anim_frame,
                              1.0f, 0.0f, NULL);

late:
    /* +14F4 */
    frame = fp->cur_anim_frame;
    if (frame < 20.0f) {
        return;
    }
    if (frame < 21.0f) {
        return;
    }
    /* +1738 */
    fp->self_vel.x = (float) ((double) fp->facing_dir * 0.2);
    ftCommon_Fall(fp, 0.11f, 2.0f);
    if (fp->cur_anim_frame < 38.0f) {
        return;
    }
    ftCo_80096900(gobj, 0, 1, false, 1.0f, 20.0f);
}
