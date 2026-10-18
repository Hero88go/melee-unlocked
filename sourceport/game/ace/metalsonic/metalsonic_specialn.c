/* ACE's Metal Sonic, neutral special: a charged shot (states 341 to 346).
 *
 * Both entries (+0640 ground, +06B8 air, the same 30 instructions) go to 344, the air wind up,
 * and arm the long shot (+4FFC) as the effect hook (accessory4_cb). The wind up's animation
 * callback (+3ADC in 344, +3618 in 341):
 *   frames 10 to 14, B let go: the shot state (346 / 343) with the short shot (+53E8) armed;
 *   frames 15 to 141, B held: the wind up again from frame 0, with a color flash (0x2E);
 *   frames 15 to 141, B let go: the console prints "release(:" and goes to the shot state with
 *     the long shot armed;
 *   the animation's end: the "loop" state (345 / 342), which costs him 8 percent at its frame 1.
 * A shot leaves when the animation script sets the throw flag (fp+2210 bit 0x80): it pushes him
 * back by half his facing and makes article 0 at bone 22, at 7 (long, 35 frames) or 5 (short,
 * 8 frames) times his facing.
 *
 * The ground rows (341 to 343) are never entered: nothing goes to 341 but its own animation
 * callback, and both entries go to 344 (on the ground too, where 344's physics is the air's and
 * its collision lands into a special landing). */
#include "metalsonic.h"

#include <dolphin/os.h>

#include <melee/ft/fighter.h>
#include <melee/ft/ft_081B.h>
#include <melee/ft/ft_084E.h>
#include <melee/ft/ft_0892.h>
#include <melee/ft/ftanim.h>
#include <melee/ft/ftcolanim.h>
#include <melee/ft/ftcommon.h>
#include <melee/ft/inlines.h>
#include <melee/ft/kinds/ftCommon/ftCo_Fall.h>
#include <melee/ft/kinds/ftCommon/ftCo_FallSpecial.h>
#include <melee/ft/kinds/ftCommon/ftCo_Landing.h>
#include <melee/ft/types.h>
#include <melee/lb/lb_00B0.h>
#include <sysdolphin/baselib/controller.h>
#include <sysdolphin/baselib/gobj.h>
#include <sysdolphin/baselib/jobj.h>

/* ---- the two shots (effect hooks) ---------------------------------------------------------- */

/* +4FFC: when the throw flag is set: it is cleared, he is pushed back (self_vel.x and gr_vel set
 * to minus half his facing), and the long shot (+54B0, article 0) leaves bone 22's world position
 * at 7 times his facing. The hook stays armed. */
static void ftNm_Shot_Fire(HSD_GObj* gobj)
{
    Fighter* fp = GET_FIGHTER(gobj);
    float back;
    HSD_JObj* jobj;
    Vec3 pos;
    Vec3 vel;

    if (!fp->throw_flags_b0) {
        return;
    }
    back = fp->facing_dir * 0.5f;
    jobj = fp->parts[FTNM_SHOT_BONE].joint;
    fp->throw_flags_b0 = 0;
    fp->self_vel.x = -back;
    fp->gr_vel = -back;
    lb_8000B1CC(jobj, NULL, &pos);
    vel.x = fp->facing_dir * 7.0f;
    vel.y = 0.0f;
    vel.z = 0.0f;
    itNm_Shot_Spawn(gobj, &pos, &vel, fp->facing_dir);
}

/* +53E8: the same with 5 times his facing and the short shot (+567C, 8 frames). */
static void ftNm_ShortShot_Fire(HSD_GObj* gobj)
{
    Fighter* fp = GET_FIGHTER(gobj);
    float back;
    HSD_JObj* jobj;
    Vec3 pos;
    Vec3 vel;

    if (!fp->throw_flags_b0) {
        return;
    }
    back = fp->facing_dir * 0.5f;
    jobj = fp->parts[FTNM_SHOT_BONE].joint;
    fp->throw_flags_b0 = 0;
    fp->self_vel.x = -back;
    fp->gr_vel = -back;
    lb_8000B1CC(jobj, NULL, &pos);
    vel.x = fp->facing_dir * 5.0f;
    vel.y = 0.0f;
    vel.z = 0.0f;
    itNm_ShortShot_Spawn(gobj, &pos, &vel, fp->facing_dir);
}

/* ---- entries ----------------------------------------------------------------------------------- */

/* +0640 (slot 4, specialn) and +06B8 (slot 5, specialairn), the same code: the throw flags word
 * and cmd_vars[0] cleared, state 344 from frame 0, ftAnim_8006EBA4, the long shot armed. */
void ftNm_SpecialN_Enter(HSD_GObj* gobj)
{
    Fighter* fp = GET_FIGHTER(gobj);

    fp->throw_flags = 0;
    fp->cmd_vars[0] = 0;
    Fighter_ChangeMotionState(gobj, ftNm_MS_SpecialAirNStart, Ft_MF_None, 0.0f, 1.0f, 0.0f,
                              NULL);
    ftAnim_8006EBA4(gobj);
    fp->accessory4_cb = ftNm_Shot_Fire;
}

void ftNm_SpecialAirN_Enter(HSD_GObj* gobj)
{
    Fighter* fp = GET_FIGHTER(gobj);

    fp->throw_flags = 0;
    fp->cmd_vars[0] = 0;
    Fighter_ChangeMotionState(gobj, ftNm_MS_SpecialAirNStart, Ft_MF_None, 0.0f, 1.0f, 0.0f,
                              NULL);
    ftAnim_8006EBA4(gobj);
    fp->accessory4_cb = ftNm_Shot_Fire;
}

/* ---- the wind up ----------------------------------------------------------------------------- */

/* +3618 (341) and +3ADC (344): the same code with other state numbers (the wind up itself, the
 * held-to-the-end state, the shot state). The frame is read once, and again after the short
 * shot's state change (it is then 0, so the second window is not taken). */
static void ftNm_WindUp_Anim(HSD_GObj* gobj, FtMotionId self, FtMotionId loop, FtMotionId shot)
{
    Fighter* fp = GET_FIGHTER(gobj);
    float frame = fp->cur_anim_frame;

    if (!(frame <= 14.0f)) {
        goto second_window;
    }
    if (!(frame >= 10.0f)) {
        goto check_end;
    }
    /* +3680: frames 10 to 14 */
    if (fp->input.held_buttons[0] & HSD_PAD_B) {
        goto check_end;
    }
    fp->throw_flags = 0;
    fp->cmd_vars[0] = 0;
    Fighter_ChangeMotionState(gobj, shot, Ft_MF_None, 0.0f, 1.0f, 0.0f, NULL);
    ftAnim_8006EBA4(gobj);
    frame = fp->cur_anim_frame;
    fp->accessory4_cb = ftNm_ShortShot_Fire;

second_window:
    /* +36D8: frames 15 to 141 */
    if (!(frame >= 15.0f)) {
        goto check_end;
    }
    if (!(frame <= 141.0f)) {
        goto check_end;
    }
    if (!(fp->input.held_buttons[0] & HSD_PAD_B)) {
        /* +3798 */
        OSReport("release(:\n");
        fp->throw_flags = 0;
        fp->cmd_vars[0] = 0;
        Fighter_ChangeMotionState(gobj, shot, Ft_MF_None, 0.0f, 1.0f, 0.0f, NULL);
        ftAnim_8006EBA4(gobj);
        fp->accessory4_cb = ftNm_Shot_Fire;
        goto check_end;
    }
    Fighter_ChangeMotionState(gobj, self, Ft_MF_None, 0.0f, 1.0f, 0.0f, NULL);
    ftAnim_8006EBA4(gobj);
    ftCo_800BFFD0(fp, (FtColAnim) 0x2E, false);
    if (ftAnim_IsFramesRemaining(gobj)) {
        return;
    }
    goto to_loop;

check_end:
    /* +365C */
    if (ftAnim_IsFramesRemaining(gobj)) {
        return;
    }
to_loop:
    /* +3760 */
    Fighter_ChangeMotionState(gobj, loop, Ft_MF_None, 0.0f, 1.0f, 0.0f, NULL);
}

void ftNm_SpecialNStart_Anim(HSD_GObj* gobj)
{
    ftNm_WindUp_Anim(gobj, ftNm_MS_SpecialNStart, ftNm_MS_SpecialNLoop, ftNm_MS_SpecialNEnd);
}

void ftNm_SpecialAirNStart_Anim(HSD_GObj* gobj)
{
    ftNm_WindUp_Anim(gobj, ftNm_MS_SpecialAirNStart, ftNm_MS_SpecialAirNLoop,
                     ftNm_MS_SpecialAirNEnd);
}

/* +3804: the wind up's interrupt (341 and 344): the stick at 0.25 or more sets self_vel.x to 0.2,
 * at -0.25 or less to -0.2; nothing else. */
void ftNm_SpecialNStart_IASA(HSD_GObj* gobj)
{
    Fighter* fp = GET_FIGHTER(gobj);
    float x = fp->input.lstick[0].x;

    if (x >= 0.25f) {
        fp->self_vel.x = 0.2f;
        return;
    }
    if (!(x <= -0.25f)) {
        return;
    }
    fp->self_vel.x = -0.2f;
}

/* +3854: 341's physics. ftCommon_8007D6A4, ft_80084F3C (ground friction), ftCommon_8007D5D4,
 * then ftCommon_Fall by the frame: up to 65 with -0.15 and then -0.05 (both), up to 89 with
 * -0.05, from 90 with -0.025 (0.1 the first argument every time). */
void ftNm_SpecialNStart_Phys(HSD_GObj* gobj)
{
    Fighter* fp = GET_FIGHTER(gobj);

    ftCommon_8007D6A4(fp);
    ft_80084F3C(gobj);
    ftCommon_8007D5D4(fp);
    if (fp->cur_anim_frame <= 65.0f) {
        /* +38E0 */
        ftCommon_Fall(fp, 0.1f, -0.15f);
        if (!(fp->cur_anim_frame <= 89.0f)) {
            goto from_90;
        }
        goto up_to_89;
    }
    if (fp->cur_anim_frame <= 89.0f) {
        goto up_to_89;
    }
from_90:
    /* +38B4 */
    if (fp->cur_anim_frame >= 90.0f) {
        goto fall_025;
    }
    return;

up_to_89:
    /* +3910 */
    ftCommon_Fall(fp, 0.1f, -0.05f);
    if (!(fp->cur_anim_frame >= 90.0f)) {
        return;
    }
fall_025:
    /* +3940 */
    ftCommon_Fall(fp, 0.1f, -0.025f);
}

/* +396C: the ground rows' collision (341 to 343): ft_800827A0, and when it answers 0, a second
 * ft_800827A0 (as in the file). */
void ftNm_SpecialN_Coll(HSD_GObj* gobj)
{
    if (ft_800827A0(gobj)) {
        return;
    }
    ft_800827A0(gobj);
}

/* +39B8: 342's animation callback. At frame 1.0 exactly: 8 percent to himself
 * (Fighter_TakeDamage_8006CC7C); at the end the retail wait. */
void ftNm_SpecialNLoop_Anim(HSD_GObj* gobj)
{
    Fighter* fp = GET_FIGHTER(gobj);

    if (fp->cur_anim_frame == 1.0f) {
        Fighter_TakeDamage_8006CC7C(fp, 8.0f);
    }
    if (!ftAnim_IsFramesRemaining(gobj)) {
        ft_8008A2BC(gobj);
    }
}

/* +3A3C: the physics of 342, 343, 345, 346: ftCommon_8007D5D4, ftCommon_Fall (0.17, 2.4),
 * ftCommon_CalcSelfAccel_Deaccel (0.06). */
void ftNm_SpecialN_Phys(HSD_GObj* gobj)
{
    Fighter* fp = GET_FIGHTER(gobj);

    ftCommon_8007D5D4(fp);
    ftCommon_Fall(fp, 0.17f, 2.4f);
    ftCommon_CalcSelfAccel_Deaccel(fp, 0.06f);
}

/* +3A90: 343's animation callback: at the end the retail wait. */
void ftNm_SpecialNEnd_Anim(HSD_GObj* gobj)
{
    if (!ftAnim_IsFramesRemaining(gobj)) {
        ft_8008A2BC(gobj);
    }
}

/* +3CC8: 344's physics. ftCommon_8007D5D4, ftCommon_CalcSelfAccel_Deaccel (0.06), then
 * ftCommon_Fall by the frame: up to 8 with -0.2; nothing more under 9; from 9 up to 65 with
 * -0.15 and then -0.05; up to 89 with -0.05; from 90 with -0.05 (0.1 the first argument every
 * time; the file's -0.05 twice, kept). */
void ftNm_SpecialAirNStart_Phys(HSD_GObj* gobj)
{
    Fighter* fp = GET_FIGHTER(gobj);

    ftCommon_8007D5D4(fp);
    ftCommon_CalcSelfAccel_Deaccel(fp, 0.06f);
    if (fp->cur_anim_frame <= 8.0f) {
        /* +3D70 */
        ftCommon_Fall(fp, 0.1f, -0.2f);
    }
    /* +3D0C */
    if (!(fp->cur_anim_frame >= 9.0f)) {
        return;
    }
    if (fp->cur_anim_frame <= 65.0f) {
        goto up_to_65;
    }
    if (fp->cur_anim_frame <= 89.0f) {
        goto up_to_89;
    }
from_90:
    /* +3D48 */
    if (fp->cur_anim_frame >= 90.0f) {
        goto fall_last;
    }
    return;

up_to_65:
    /* +3DB8 */
    ftCommon_Fall(fp, 0.1f, -0.15f);
    if (!(fp->cur_anim_frame <= 89.0f)) {
        goto from_90;
    }
up_to_89:
    /* +3DE8 */
    ftCommon_Fall(fp, 0.1f, -0.05f);
    if (!(fp->cur_anim_frame >= 90.0f)) {
        return;
    }
fall_last:
    /* +3D90 */
    ftCommon_Fall(fp, 0.1f, -0.05f);
}

/* +3E1C: the collision of 344 and 346: on landing (ft_80081D0C), the special landing with 15
 * frames, no interrupt. */
void ftNm_SpecialAirN_Coll(HSD_GObj* gobj)
{
    if (ft_80081D0C(gobj) != 0) {
        ftCo_LandingFallSpecial_Enter(gobj, false, 15.0f);
    }
}

/* +3E74: 345's animation callback. At frame 1.0 exactly: 8 percent to himself; at the end the
 * special fall (ftCo_80096900: 1, 1, no interrupt, mobility 0.02, landing 20). */
void ftNm_SpecialAirNLoop_Anim(HSD_GObj* gobj)
{
    Fighter* fp = GET_FIGHTER(gobj);

    if (fp->cur_anim_frame == 1.0f) {
        Fighter_TakeDamage_8006CC7C(fp, 8.0f);
    }
    if (!ftAnim_IsFramesRemaining(gobj)) {
        ftCo_80096900(gobj, 1, 1, false, 0.02f, 20.0f);
    }
}

/* +54A0: 345's landing callback: the special landing with 15 frames, no interrupt. */
static void ftNm_SpecialAirNLoop_Land(HSD_GObj* gobj)
{
    ftCo_LandingFallSpecial_Enter(gobj, false, 15.0f);
}

/* +3F14: 345's collision: ft_800831CC with no ledge test and +54A0 on landing. */
void ftNm_SpecialAirNLoop_Coll(HSD_GObj* gobj)
{
    ft_800831CC(gobj, NULL, ftNm_SpecialAirNLoop_Land);
}

/* +3F24: 346's animation callback: at the end the retail fall. */
void ftNm_SpecialAirNEnd_Anim(HSD_GObj* gobj)
{
    if (!ftAnim_IsFramesRemaining(gobj)) {
        ftCo_Fall_Enter(gobj);
    }
}
