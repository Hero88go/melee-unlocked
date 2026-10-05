/* ACE's Zero: the side special, the dash (states 350 to 355; +0488 and +0B4C to +1234 and +2100
 * of the "ftFunction" block of PlZx.dat).
 *
 * On the ground the move is entered by Link's ftLk_SpecialS_Enter (the m-ex default of slot 6),
 * which picks state 350 or 352 by Link's "used_boomerang" word; all three ground rows have the
 * same three callbacks of his own. In the air his own entry allows one dash per airtime.
 *
 * C stick thresholds are compared in double on the console (0.81 and 0.67 on the ground, 0.31 in
 * the air: +20D0 to +20F8); the casts below keep that. Speeds written as a double product are
 * rounded to single as the file does (fmul, frsp). */
#include "zero.h"

#include <melee/ft/fighter.h>
#include <melee/ft/ft_081B.h>
#include <melee/ft/ft_084E.h>
#include <melee/ft/ftanim.h>
#include <melee/ft/ftcommon.h>
#include <melee/ft/ftwalljump.h>
#include <melee/ft/inlines.h>
#include <melee/ft/kinds/ftCommon/ftCo_Fall.h>
#include <melee/ft/kinds/ftCommon/ftCo_Jump.h>
#include <melee/ft/kinds/ftCommon/ftCo_JumpAerial.h>
#include <melee/ft/kinds/ftCommon/ftCo_Wait.h>
#include <melee/ft/types.h>
#include <sysdolphin/baselib/controller.h>

#define FTZX_SPECIALS_CANCEL_FRAME 4.0f /* +1DC4 */

/* ---- ground ---- */

/* The C stick cancel of the ground dash (+0C40, +0C84, +0CC8, +0D0C): half his facing as air
 * speed, then the standing interrupts. Answers true when the callback must end. */
static bool ftZx_SpecialS_StickCancel(HSD_GObj* gobj, Fighter* fp)
{
    fp->self_vel.x = fp->facing_dir * 0.5f;
    ftCo_Wait_IASA(gobj);
    return fp->cur_anim_frame < FTZX_SPECIALS_CANCEL_FRAME;
}

/* +0B4C "SpecialS_IASA": from frame 4 the dash can be left: A runs the standing interrupts, each
 * C stick direction past its threshold does the same with a push, and a jump input jumps with
 * the dash speed (2.2) kept on the ground and in the air. Every test is made even after an
 * earlier one acted, as the file does; only a frame that went back under 4 ends the callback. */
void ftZx_SpecialS_IASA(HSD_GObj* gobj)
{
    Fighter* fp = GET_FIGHTER(gobj);

    if (fp->cur_anim_frame < FTZX_SPECIALS_CANCEL_FRAME) {
        return;
    }
    if (fp->input.pressed_buttons & HSD_PAD_A) {
        /* +0C14 */
        ftCo_Wait_IASA(gobj);
        if (fp->cur_anim_frame < FTZX_SPECIALS_CANCEL_FRAME) {
            return;
        }
    }
    if ((double) fp->input.cstick[0].x >= 0.81 && ftZx_SpecialS_StickCancel(gobj, fp)) {
        return;
    }
    if ((double) fp->input.cstick[0].x <= -0.81 && ftZx_SpecialS_StickCancel(gobj, fp)) {
        return;
    }
    if ((double) fp->input.cstick[0].y >= 0.67 && ftZx_SpecialS_StickCancel(gobj, fp)) {
        return;
    }
    if ((double) fp->input.cstick[0].y <= -0.67 && ftZx_SpecialS_StickCancel(gobj, fp)) {
        return;
    }
    if (ftCo_800CB870(gobj)) {
        /* +0D50 */
        if (ftCo_Jump_GetInput(gobj) != 0) {
            float speed;

            ftCo_Jump_CheckInput(gobj);
            speed = (float) ((double) fp->facing_dir * 2.2);
            fp->self_vel.x = speed;
            fp->gr_vel = speed;
        }
    }
}

/* +0DA4 "SpecialS_Phys": the common ground physics, then from frame 4 the ground speed is set
 * every frame: 2.2 up to frame 18, 0.6 from it. */
void ftZx_SpecialS_Phys(HSD_GObj* gobj)
{
    Fighter* fp = GET_FIGHTER(gobj);
    float frame;

    ft_80084F3C(gobj);
    frame = fp->cur_anim_frame;
    if (frame < FTZX_SPECIALS_CANCEL_FRAME) {
        return;
    }
    if (frame >= 18.0f) {
        fp->gr_vel = (float) ((double) fp->facing_dir * 0.6);
    } else {
        fp->gr_vel = (float) ((double) fp->facing_dir * 2.2);
    }
}

/* +0E30 "SpecialS_Coll": off the floor the air dash goes on at the same frame, at air speed 1
 * times his facing. The file changes the state and nothing else. */
void ftZx_SpecialS_Coll(HSD_GObj* gobj)
{
    Fighter* fp = GET_FIGHTER(gobj);
    ftZx_FighterVars* fv = ftZx_Vars(fp);

    if (!ft_80082708(gobj)) {
        fp->self_vel.x = fp->facing_dir;
        fv->specialairs_count++;
        Fighter_ChangeMotionState(gobj, ftZx_MS_SpecialAirS, Ft_MF_None,
                                  fp->cur_anim_frame, 1.0f, 0.0f, NULL);
    }
}

/* ---- air ---- */

/* +0488 "SpecialAirS" (slot 7). The first use of an airtime enters state 353 and clears command
 * variable 3. A later use only turns him: his facing is set from fp+30 when that is exactly 1
 * or -1, and no state is entered. */
void ftZx_SpecialAirS_Enter(HSD_GObj* gobj)
{
    Fighter* fp = GET_FIGHTER(gobj);
    ftZx_FighterVars* fv = ftZx_Vars(fp);

    if (fv->specialairs_used == 1) {
        if (fp->facing_dir1 == 1.0f) {
            fp->facing_dir = 1.0f;
        } else if (fp->facing_dir1 == -1.0f) {
            fp->facing_dir = -1.0f;
        }
        return;
    }
    Fighter_ChangeMotionState(gobj, ftZx_MS_SpecialAirS, Ft_MF_None, 0.0f, 1.0f, 0.0f, NULL);
    fv->specialairs_used++;
    fp->cmd_vars[3] = 0;
}

/* +0EB0 "SpecialAirS_Anim" */
void ftZx_SpecialAirS_Anim(HSD_GObj* gobj)
{
    Fighter* fp = GET_FIGHTER(gobj);

    if (!ftAnim_IsFramesRemaining(gobj)) {
        fp->self_vel.x = fp->facing_dir;
        ftCo_Fall_Enter(gobj);
    }
}

/* The cancel of the air dash (+0FCC, +1000, +1038, +1070, +10A8): air speed 1 times his facing,
 * then the falling interrupts. Answers true when the callback must end. */
static bool ftZx_SpecialAirS_Cancel(HSD_GObj* gobj, Fighter* fp)
{
    fp->self_vel.x = fp->facing_dir;
    ftCo_Fall_IASA_Inner(gobj);
    return fp->cur_anim_frame < FTZX_SPECIALS_CANCEL_FRAME;
}

/* +0F04 "SpecialAirS_IASA": as the ground one with the falling interrupts, one threshold (0.31)
 * for the four C stick directions, and a jump input that enters the forward double jump state
 * directly at 0.3 times his facing. */
void ftZx_SpecialAirS_IASA(HSD_GObj* gobj)
{
    Fighter* fp = GET_FIGHTER(gobj);

    if (fp->cur_anim_frame < FTZX_SPECIALS_CANCEL_FRAME) {
        return;
    }
    if ((fp->input.pressed_buttons & HSD_PAD_A) && ftZx_SpecialAirS_Cancel(gobj, fp)) {
        return;
    }
    if ((double) fp->input.cstick[0].x >= 0.31 && ftZx_SpecialAirS_Cancel(gobj, fp)) {
        return;
    }
    if ((double) fp->input.cstick[0].x <= -0.31 && ftZx_SpecialAirS_Cancel(gobj, fp)) {
        return;
    }
    if ((double) fp->input.cstick[0].y >= 0.31 && ftZx_SpecialAirS_Cancel(gobj, fp)) {
        return;
    }
    if ((double) fp->input.cstick[0].y <= -0.31 && ftZx_SpecialAirS_Cancel(gobj, fp)) {
        return;
    }
    if (ftCo_800CB870(gobj)) {
        /* +10E0 */
        if (ftCo_Jump_GetInput(gobj) != 0) {
            fp->self_vel.x = (float) ((double) fp->facing_dir * 0.3);
            Fighter_ChangeMotionState(gobj, ftCo_MS_JumpAerialF, Ft_MF_None, 0.0f, 1.0f, 0.0f,
                                      NULL);
        }
    }
}

/* +1150 "SpecialAirS_Phys". No common physics call at all: on frame 1 the counter goes up; once
 * the script has set command variable 1, from frame 4 the velocity is written every frame (1.6
 * forward and 0.1 up), and from frame 23 the forward speed is 0 times his facing. Outside those
 * conditions the velocity is left as it is. */
void ftZx_SpecialAirS_Phys(HSD_GObj* gobj)
{
    Fighter* fp = GET_FIGHTER(gobj);
    ftZx_FighterVars* fv = ftZx_Vars(fp);
    float frame = fp->cur_anim_frame;
    float facing;

    if (frame == 1.0f) {
        fv->specialairs_count++;
        return;
    }
    if (fp->cmd_vars[1] != 1) {
        return;
    }
    if (frame < FTZX_SPECIALS_CANCEL_FRAME) {
        return;
    }
    facing = fp->facing_dir;
    fp->self_vel.x = (float) ((double) facing * 1.6);
    fp->self_vel.y = 0.1f;
    if (frame < 23.0f) {
        return;
    }
    fp->self_vel.x = facing * 0.0f;
}

/* +2100 "SpecialAirSLand": the common landing state, entered directly. */
static void ftZx_SpecialAirS_Land(HSD_GObj* gobj)
{
    ftCommon_8007D7FC(GET_FIGHTER(gobj));
    Fighter_ChangeMotionState(gobj, ftCo_MS_Landing, Ft_MF_None, 0.0f, 1.0f, 0.0f, NULL);
}

/* +11E8 "SpecialAirS_Coll": ft_800831CC(gobj, NULL, land), then the wall jump test once more
 * when r3 is not zero. ft_800831CC returns nothing: r3 is what its last call left, which is 1
 * after a wall jump or a ledge catch, 0 when neither happened, and unknown after the landing
 * callback (the leftovers of Fighter_ChangeMotionState). Reproduced by the state: a change to
 * anything but the landing state repeats the wall jump test; the landing case does not. */
void ftZx_SpecialAirS_Coll(HSD_GObj* gobj)
{
    Fighter* fp = GET_FIGHTER(gobj);
    FtMotionId before = fp->motion_id;

    ft_800831CC(gobj, NULL, ftZx_SpecialAirS_Land);
    if (fp->motion_id != before && fp->motion_id != ftCo_MS_Landing) {
        ftWallJump_8008169C(gobj);
    }
}
