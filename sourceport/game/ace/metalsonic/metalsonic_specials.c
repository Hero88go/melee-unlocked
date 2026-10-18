/* ACE's Metal Sonic, side special: a charged dash (states 347, 348, 350, 351; 349 and 352 are
 * Fox's rows).
 *
 * The entries are MxDt's defaults, Fox's ftFx_SpecialSStart_Enter and ftFx_SpecialAirSStart_Enter
 * (Fox's illusion variables, the speed divided by Fox's attribute +28, state 347 or 350). His
 * own start rows then charge: while B is held the animation runs to frame 120 and there stops
 * (a color flash 0x2D each frame); L, R or Z pressed while charging cancels (the wait on the
 * ground, a special fall in the air). When B is let go (or not held) at frame 15 or later, he
 * dashes: self_vel.x is his facing times 9, 8, 7, 6, 5, 4, 3 or 2 for frames from 120, 105, 90,
 * 75, 60, 45, 30, 15, into 348 or 351. The dash's end, or B, L, R or Z from its frame 2, goes to
 * Fox's air side special end (352) with some speed.
 *
 * Mod behaviour kept: the ground start's collision sends him into the GROUND dash 348 when he
 * leaves the ground (no ground to air change; Fox's 348 collision then makes him airborne), and
 * the air start's landing goes to the ground start 347 with no air to ground change. The ground
 * dash has no physics callback. */
#include "metalsonic.h"

#include <melee/ft/fighter.h>
#include <melee/ft/ft_081B.h>
#include <melee/ft/ft_084E.h>
#include <melee/ft/ft_0892.h>
#include <melee/ft/ftanim.h>
#include <melee/ft/ftcolanim.h>
#include <melee/ft/ftcommon.h>
#include <melee/ft/inlines.h>
#include <melee/ft/kinds/ftCommon/ftCo_FallSpecial.h>
#include <melee/ft/types.h>
#include <sysdolphin/baselib/controller.h>
#include <sysdolphin/baselib/gobj.h>

#define FTNM_PAD_CANCEL (HSD_PAD_L | HSD_PAD_R | HSD_PAD_Z)              /* 0x70 */
#define FTNM_PAD_DASH_END (HSD_PAD_B | HSD_PAD_L | HSD_PAD_R | HSD_PAD_Z) /* 0x270 */

/* +3FE8 to +43B0 (ground) and +4630 to +4A14 (air), the same eight steps: each one re-reads the
 * frame, and each one taken sets self_vel.x (facing times the step's number, in single; the last
 * is facing + facing) and changes to the dash from frame 0. After a change the frame is 0, so on
 * the console only the first step taken changes anything; the steps are kept as they are. */
static void ftNm_SpecialS_Launch(HSD_GObj* gobj, FtMotionId dash)
{
    Fighter* fp = GET_FIGHTER(gobj);

    if (fp->cur_anim_frame >= 120.0f) {
        fp->self_vel.x = fp->facing_dir * 9.0f;
        Fighter_ChangeMotionState(gobj, dash, Ft_MF_None, 0.0f, 1.0f, 0.0f, NULL);
    }
    if (fp->cur_anim_frame >= 105.0f) {
        fp->self_vel.x = fp->facing_dir * 8.0f;
        Fighter_ChangeMotionState(gobj, dash, Ft_MF_None, 0.0f, 1.0f, 0.0f, NULL);
    }
    if (fp->cur_anim_frame >= 90.0f) {
        fp->self_vel.x = fp->facing_dir * 7.0f;
        Fighter_ChangeMotionState(gobj, dash, Ft_MF_None, 0.0f, 1.0f, 0.0f, NULL);
    }
    if (fp->cur_anim_frame >= 75.0f) {
        fp->self_vel.x = fp->facing_dir * 6.0f;
        Fighter_ChangeMotionState(gobj, dash, Ft_MF_None, 0.0f, 1.0f, 0.0f, NULL);
    }
    if (fp->cur_anim_frame >= 60.0f) {
        fp->self_vel.x = fp->facing_dir * 5.0f;
        Fighter_ChangeMotionState(gobj, dash, Ft_MF_None, 0.0f, 1.0f, 0.0f, NULL);
    }
    if (fp->cur_anim_frame >= 45.0f) {
        fp->self_vel.x = fp->facing_dir * 4.0f;
        Fighter_ChangeMotionState(gobj, dash, Ft_MF_None, 0.0f, 1.0f, 0.0f, NULL);
    }
    if (fp->cur_anim_frame >= 30.0f) {
        fp->self_vel.x = fp->facing_dir * 3.0f;
        Fighter_ChangeMotionState(gobj, dash, Ft_MF_None, 0.0f, 1.0f, 0.0f, NULL);
    }
    if (fp->cur_anim_frame >= 15.0f) {
        fp->self_vel.x = fp->facing_dir + fp->facing_dir;
        Fighter_ChangeMotionState(gobj, dash, Ft_MF_None, 0.0f, 1.0f, 0.0f, NULL);
    }
}

/* ---- 347, the ground charge -------------------------------------------------------------------- */

/* +3F70: at the end the retail wait. */
void ftNm_SpecialSStart_Anim(HSD_GObj* gobj)
{
    if (!ftAnim_IsFramesRemaining(gobj)) {
        ft_8008A2BC(gobj);
    }
}

/* +3FBC: with B held: from frame 120 the color flash and the animation rate 0; then L, R or Z
 * pressed goes to the retail wait. B not held (also when it is let go between the two reads):
 * the dash steps into 348. */
void ftNm_SpecialSStart_IASA(HSD_GObj* gobj)
{
    Fighter* fp = GET_FIGHTER(gobj);

    if (!(fp->input.held_buttons[0] & HSD_PAD_B)) {
        ftNm_SpecialS_Launch(gobj, ftNm_MS_SpecialS);
        return;
    }
    /* +40A0 */
    if (fp->cur_anim_frame >= 120.0f) {
        /* +40D8 */
        ftCo_800BFFD0(fp, (FtColAnim) 0x2D, false);
        ftAnim_SetAnimRate(gobj, 0.0f);
        if (!(fp->input.held_buttons[0] & HSD_PAD_B)) {
            ftNm_SpecialS_Launch(gobj, ftNm_MS_SpecialS);
            return;
        }
    }
    /* +40B8 */
    if (fp->input.pressed_buttons & FTNM_PAD_CANCEL) {
        ft_8008A2BC(gobj);
    }
}

/* +43B4: ground friction (ft_80084F3C); up to frame 1.0 self_vel.x is 0. */
void ftNm_SpecialSStart_Phys(HSD_GObj* gobj)
{
    Fighter* fp = GET_FIGHTER(gobj);

    ft_80084F3C(gobj);
    if (fp->cur_anim_frame <= 1.0f) {
        fp->self_vel.x = 0.0f;
    }
}

/* +4404: off the ground (ft_80082708 answers 0): state 348 from the current frame, then the
 * color flash 0x2D. */
void ftNm_SpecialSStart_Coll(HSD_GObj* gobj)
{
    Fighter* fp = GET_FIGHTER(gobj);

    if (ft_80082708(gobj) != 0) {
        return;
    }
    Fighter_ChangeMotionState(gobj, ftNm_MS_SpecialS, Ft_MF_None, fp->cur_anim_frame, 1.0f, 0.0f,
                              NULL);
    ftCo_800BFFD0(fp, (FtColAnim) 0x2D, false);
}

/* ---- 348, the ground dash ---------------------------------------------------------------------- */

/* +4494: at the end: self_vel.y = -0.5 and self_vel.x = facing * 1.5 (in that order), state 352
 * (Fox's air side special end). */
void ftNm_SpecialS_Anim(HSD_GObj* gobj)
{
    Fighter* fp = GET_FIGHTER(gobj);

    if (ftAnim_IsFramesRemaining(gobj)) {
        return;
    }
    fp->self_vel.y = -0.5f;
    fp->self_vel.x = fp->facing_dir * 1.5f;
    Fighter_ChangeMotionState(gobj, ftNm_MS_SpecialAirSEnd, Ft_MF_None, 0.0f, 1.0f, 0.0f, NULL);
}

/* +4530: from frame 2.0, B, L, R or Z pressed: the same as the end. */
void ftNm_SpecialS_IASA(HSD_GObj* gobj)
{
    Fighter* fp = GET_FIGHTER(gobj);

    if (!(fp->cur_anim_frame >= 2.0f)) {
        return;
    }
    if (!(fp->input.pressed_buttons & FTNM_PAD_DASH_END)) {
        return;
    }
    fp->self_vel.y = -0.5f;
    fp->self_vel.x = fp->facing_dir * 1.5f;
    Fighter_ChangeMotionState(gobj, ftNm_MS_SpecialAirSEnd, Ft_MF_None, 0.0f, 1.0f, 0.0f, NULL);
}

/* ---- 350, the air charge ----------------------------------------------------------------------- */

/* +459C: at the end the special fall (1, 1, no interrupt, mobility 0.02, landing 20). */
void ftNm_SpecialAirSStart_Anim(HSD_GObj* gobj)
{
    if (!ftAnim_IsFramesRemaining(gobj)) {
        ftCo_80096900(gobj, 1, 1, false, 0.02f, 20.0f);
    }
}

/* +4604: as +3FBC, into 351; the cancel is the special fall with landing 10. */
void ftNm_SpecialAirSStart_IASA(HSD_GObj* gobj)
{
    Fighter* fp = GET_FIGHTER(gobj);

    if (!(fp->input.held_buttons[0] & HSD_PAD_B)) {
        ftNm_SpecialS_Launch(gobj, ftNm_MS_SpecialAirS);
        return;
    }
    /* +46E8 */
    if (fp->cur_anim_frame >= 120.0f) {
        /* +473C */
        ftCo_800BFFD0(fp, (FtColAnim) 0x2D, false);
        ftAnim_SetAnimRate(gobj, 0.0f);
        if (!(fp->input.held_buttons[0] & HSD_PAD_B)) {
            ftNm_SpecialS_Launch(gobj, ftNm_MS_SpecialAirS);
            return;
        }
    }
    /* +4700 */
    if (fp->input.pressed_buttons & FTNM_PAD_CANCEL) {
        ftCo_80096900(gobj, 1, 1, false, 0.02f, 10.0f);
    }
}

/* +4A18: ftCommon_8007D60C, ftCommon_Fall (0.02, 2.4); up to frame 1.0 self_vel.x is 0. */
void ftNm_SpecialAirSStart_Phys(HSD_GObj* gobj)
{
    Fighter* fp = GET_FIGHTER(gobj);

    ftCommon_8007D60C(fp);
    ftCommon_Fall(fp, 0.02f, 2.4f);
    if (fp->cur_anim_frame <= 1.0f) {
        fp->self_vel.x = 0.0f;
    }
}

/* +4A84: on landing (ft_80081D0C): state 347 from the current frame, then the color flash. */
void ftNm_SpecialAirSStart_Coll(HSD_GObj* gobj)
{
    Fighter* fp = GET_FIGHTER(gobj);

    if (ft_80081D0C(gobj) == 0) {
        return;
    }
    Fighter_ChangeMotionState(gobj, ftNm_MS_SpecialSStart, Ft_MF_None, fp->cur_anim_frame, 1.0f,
                              0.0f, NULL);
    ftCo_800BFFD0(fp, (FtColAnim) 0x2D, false);
}

/* ---- 351, the air dash ------------------------------------------------------------------------- */

/* +4B14: at the end: self_vel.x = facing + facing, state 352. */
void ftNm_SpecialAirS_Anim(HSD_GObj* gobj)
{
    Fighter* fp = GET_FIGHTER(gobj);

    if (ftAnim_IsFramesRemaining(gobj)) {
        return;
    }
    fp->self_vel.x = fp->facing_dir + fp->facing_dir;
    Fighter_ChangeMotionState(gobj, ftNm_MS_SpecialAirSEnd, Ft_MF_None, 0.0f, 1.0f, 0.0f, NULL);
}

/* +4B9C: from frame 2.0, B, L, R or Z pressed: the same as the end. */
void ftNm_SpecialAirS_IASA(HSD_GObj* gobj)
{
    Fighter* fp = GET_FIGHTER(gobj);

    if (!(fp->cur_anim_frame >= 2.0f)) {
        return;
    }
    if (!(fp->input.pressed_buttons & FTNM_PAD_DASH_END)) {
        return;
    }
    fp->self_vel.x = fp->facing_dir + fp->facing_dir;
    Fighter_ChangeMotionState(gobj, ftNm_MS_SpecialAirSEnd, Ft_MF_None, 0.0f, 1.0f, 0.0f, NULL);
}

/* +4BF4: no vertical speed (self_vel.y = 0) every frame. */
void ftNm_SpecialAirS_Phys(HSD_GObj* gobj)
{
    Fighter* fp = GET_FIGHTER(gobj);

    fp->self_vel.y = 0.0f;
}
