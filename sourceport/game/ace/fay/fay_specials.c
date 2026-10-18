/* ACE's Fay: the side special (345, 346) and the second side move (364 to 369).
 *
 * 345 / 346: the script's command variable 3 asks once for the held article 4 (state 0); the
 * throw flag fires the slash (article 5, 7 frames) at 36 degrees down from her facing and arms
 * a recoil (command variable 2 = 1, read by the physics). At exactly frame 15 with B held she
 * goes on to 364 (ground) or 367 (air), started at frame 14 with an animation blend of 10.
 * 364 / 367 fire article 6 (60 frames) on the throw flag, then hold (365 / 368) while B is held
 * and end (366 / 369) when it is let go; article 6 itself ends the move when it hits. Ground and
 * air versions swap at the same frame (flags 0x5000). */
#include "fay.h"

#include <math.h>

#include <melee/ft/fighter.h>
#include <melee/ft/ft_081B.h>
#include <melee/ft/ft_084E.h>
#include <melee/ft/ft_0892.h>
#include <melee/ft/ftanim.h>
#include <melee/ft/ftcommon.h>
#include <melee/ft/inlines.h>
#include <melee/ft/kinds/ftCommon/ftCo_AttackAir.h>
#include <melee/ft/kinds/ftCommon/ftCo_Fall.h>
#include <melee/ft/types.h>
#include <sysdolphin/baselib/controller.h>

/* +0CD4, +0CDC: 0.62831855 and -0.62831855 (single: 36 degrees); +0C8C: 0.78539819 (pi/4). */
#define FTFY_S_ANGLE 0x1.41b2f8p-1f
#define FTFY_S_RECOIL_ANGLE 0x1.921fb6p-1f

/* +06F0 and +0764 (slots 6 and 7): nothing while fp+22EC is set (never: nothing sets it);
 * command variables 1, 2, 3 and the throw flag cleared, the state, ftAnim_8006EBA4. */
static void ftFy_SpecialS_EnterState(HSD_GObj* gobj, FtMotionId msid)
{
    Fighter* fp = GET_FIGHTER(gobj);

    if (ftFy_FV(fp)->s_lock != 0) {
        return;
    }
    fp->cmd_vars[1] = 0;
    fp->cmd_vars[2] = 0;
    fp->cmd_vars[3] = 0;
    fp->throw_flags_b0 = false;
    Fighter_ChangeMotionState(gobj, msid, Ft_MF_None, 0.0f, 1.0f, 0.0f, NULL);
    ftAnim_8006EBA4(gobj);
}

/* +06F0 (slot 6) */
void ftFy_SpecialS_Enter(HSD_GObj* gobj)
{
    ftFy_SpecialS_EnterState(gobj, ftFy_MS_SpecialS);
}

/* +0764 (slot 7) */
void ftFy_SpecialAirS_Enter(HSD_GObj* gobj)
{
    ftFy_SpecialS_EnterState(gobj, ftFy_MS_SpecialAirS);
}

/* Command variable 3 set by the script: cleared, and the held article 4 in state 0 at her
 * position (+16B8, +18EC, +2FD8, +31CC). */
static void ftFy_SpecialS_BladeOnce(HSD_GObj* gobj, Fighter* fp)
{
    if (fp->cmd_vars[3] == 1) {
        Vec3 pos;

        fp->cmd_vars[3] = 0;
        pos = fp->cur_pos;
        itFy_Blade_Spawn(gobj, &pos, 0, fp->facing_dir);
    }
}

/* +157C: 345 (ground).
 *   the held article (above);
 *   the throw flag: command variable 2 = 1; the slash at x + 10 * facing (fused), y + 0.0, the
 *     z of her position, speed ((4.5 * facing) * cos 36) in double for x and sin -36 * 4.5 for y,
 *     7 frames, tilted 36 degrees;
 *   the animation's end: the common wait. */
void ftFy_SpecialS_Anim(HSD_GObj* gobj)
{
    Fighter* fp = GET_FIGHTER(gobj);

    ftFy_SpecialS_BladeOnce(gobj, fp);
    if (fp->throw_flags_b0) {
        /* +15F4 */
        Vec3 pos;
        Vec3 vel;
        float c;
        float s;

        fp->cmd_vars[2] = 1;
        fp->throw_flags_b0 = false;
        c = cosf(FTFY_S_ANGLE);
        vel.x = (float) (((double) fp->facing_dir * 4.5) * (double) c);
        s = sinf(-FTFY_S_ANGLE);
        vel.y = s * 4.5f;
        vel.z = 0.0f;
        pos.x = MU_FMADDS(fp->facing_dir, 10.0f, fp->cur_pos.x);
        pos.y = fp->cur_pos.y + 0.0f;
        pos.z = fp->cur_pos.z;
        itFy_Slash_Spawn(gobj, &pos, &vel, 7, fp->facing_dir, FTFY_S_ANGLE);
    }
    if (!ftAnim_IsFramesRemaining(gobj)) {
        ft_8008A2BC(gobj);
    }
}

/* At exactly frame 15 (the console tests "not before 15", then "equal") with B held: the
 * second side move, started at frame 14, speed 1, blend 10, then ftAnim_8006EBA4. */
static void ftFy_SpecialS_ToSecond(HSD_GObj* gobj, FtMotionId msid)
{
    Fighter* fp = GET_FIGHTER(gobj);

    if (!(fp->cur_anim_frame == 15.0f)) {
        return;
    }
    if (!(fp->input.held_buttons[0] & HSD_PAD_B)) {
        return;
    }
    Fighter_ChangeMotionState(gobj, msid, Ft_MF_None, 14.0f, 1.0f, 10.0f, NULL);
    ftAnim_8006EBA4(gobj);
}

/* +16EC: 345's interrupt */
void ftFy_SpecialS_IASA(HSD_GObj* gobj)
{
    ftFy_SpecialS_ToSecond(gobj, ftFy_MS_SpecialS2);
}

/* +1768: 345's and 365's physics. Command variable 2: 1 sets the horizontal speed to -facing
 * (a recoil) and goes to 2; 2 damps it by 0.95 (in double); otherwise the common ground
 * physics (ft_80084F3C). */
void ftFy_SpecialS_Phys(HSD_GObj* gobj)
{
    Fighter* fp = GET_FIGHTER(gobj);

    if (fp->cmd_vars[2] == 1) {
        /* +17A0 */
        fp->cmd_vars[2] = 2;
        fp->self_vel.x = -fp->facing_dir;
        return;
    }
    if (fp->cmd_vars[2] == 2) {
        fp->self_vel.x = (float) ((double) fp->self_vel.x * 0.95);
        return;
    }
    ft_80084F3C(gobj);
}

/* +17B8: 346 (air). As 345 with the slash at x + 15 * facing (fused), y + 3.0, speed
 * (3.0 * facing) * cos 36 and sin -36 * 3.0, all in single; the common fall at the end. */
void ftFy_SpecialAirS_Anim(HSD_GObj* gobj)
{
    Fighter* fp = GET_FIGHTER(gobj);

    ftFy_SpecialS_BladeOnce(gobj, fp);
    if (fp->throw_flags_b0) {
        /* +1830 */
        Vec3 pos;
        Vec3 vel;
        float c;
        float s;

        fp->cmd_vars[2] = 1;
        fp->throw_flags_b0 = false;
        c = cosf(FTFY_S_ANGLE);
        vel.x = (fp->facing_dir * 3.0f) * c;
        s = sinf(-FTFY_S_ANGLE);
        vel.y = s * 3.0f;
        vel.z = 0.0f;
        pos.x = MU_FMADDS(fp->facing_dir, 15.0f, fp->cur_pos.x);
        pos.y = fp->cur_pos.y + 3.0f;
        pos.z = fp->cur_pos.z;
        itFy_Slash_Spawn(gobj, &pos, &vel, 7, fp->facing_dir, FTFY_S_ANGLE);
    }
    if (!ftAnim_IsFramesRemaining(gobj)) {
        ftCo_Fall_Enter(gobj);
    }
}

/* +1920: 346's interrupt */
void ftFy_SpecialAirS_IASA(HSD_GObj* gobj)
{
    ftFy_SpecialS_ToSecond(gobj, ftFy_MS_SpecialAirS2);
}

/* +199C: 346's physics. The common fall at her gravity and terminal speed; when command
 * variable 2 is 1 (the slash just left), it goes to 0, she is thrown back and up at 45 degrees
 * (-facing * cos, sin, speed 1) and the physics callback becomes the common aerial one
 * (ftCo_AttackAir_Phys, 0x8008D5B4, by absolute address in the file). */
void ftFy_SpecialAirS_Phys(HSD_GObj* gobj)
{
    Fighter* fp = GET_FIGHTER(gobj);

    ftCommon_Fall(fp, fp->co_attrs.gravity, fp->co_attrs.terminal_velocity);
    if (fp->cmd_vars[2] == 1) {
        /* +19E0 */
        float back = -fp->facing_dir;
        fp->cmd_vars[2] = 0;
        fp->self_vel.x = back * cosf(FTFY_S_RECOIL_ANGLE);
        fp->self_vel.y = sinf(FTFY_S_RECOIL_ANGLE);
        fp->phys_cb = ftCo_AttackAir_Phys;
    }
}

/* +1A44: 346's collision: the common air collision, and on landing 345 at the same frame
 * (flags 0x5000) with the throw flag and command variable 2 carried over. */
void ftFy_SpecialAirS_Coll(HSD_GObj* gobj)
{
    Fighter* fp;
    bool throw_b0;
    u32 var2;

    if (ft_80081D0C(gobj) == false) {
        return;
    }
    fp = GET_FIGHTER(gobj);
    throw_b0 = fp->throw_flags_b0;
    var2 = fp->cmd_vars[2];
    ftCommon_8007D7FC(fp);
    Fighter_ChangeMotionState(gobj, ftFy_MS_SpecialS, FTFY_MF_SWAP, fp->cur_anim_frame, 1.0f,
                              0.0f, NULL);
    fp->throw_flags_b0 = throw_b0;
    fp->cmd_vars[2] = var2;
}

/* The second move's shot: on the throw flag, article 6 at x + 15 * facing (fused), y + 8.0,
 * her z, 60 frames (+2F88, +318C). */
static void ftFy_SpecialS2_WaveOnFlag(HSD_GObj* gobj, Fighter* fp)
{
    if (fp->throw_flags_b0) {
        Vec3 pos;

        fp->throw_flags_b0 = false;
        pos.x = MU_FMADDS(fp->facing_dir, 15.0f, fp->cur_pos.x);
        pos.y = fp->cur_pos.y + 8.0f;
        pos.z = fp->cur_pos.z;
        itFy_Wave_Spawn(gobj, &pos, 60, fp->facing_dir);
    }
}

/* +2EFC: 364 (ground): the held article, the shot, then 365 at the animation's end. */
void ftFy_SpecialS2_Anim(HSD_GObj* gobj)
{
    Fighter* fp = GET_FIGHTER(gobj);

    ftFy_SpecialS_BladeOnce(gobj, fp);
    ftFy_SpecialS2_WaveOnFlag(gobj, fp);
    if (!ftAnim_IsFramesRemaining(gobj)) {
        Fighter_ChangeMotionState(gobj, ftFy_MS_SpecialS2Hold, Ft_MF_None, 0.0f, 1.0f, 0.0f,
                                  NULL);
    }
}

/* +4394: off the ground in 364 to 366: the air state of the same number at the same frame
 * (flags 0x5000), then ftAnim_8006EBA4 whatever the state. */
static void ftFy_SpecialS2_GroundToAir(HSD_GObj* gobj)
{
    Fighter* fp = GET_FIGHTER(gobj);

    ftCommon_8007D5D4(fp);
    switch (fp->motion_id) {
    case ftFy_MS_SpecialS2Hold:
        Fighter_ChangeMotionState(gobj, ftFy_MS_SpecialAirS2Hold, FTFY_MF_SWAP,
                                  fp->cur_anim_frame, 1.0f, 0.0f, NULL);
        break;
    case ftFy_MS_SpecialS2End:
        Fighter_ChangeMotionState(gobj, ftFy_MS_SpecialAirS2End, FTFY_MF_SWAP,
                                  fp->cur_anim_frame, 1.0f, 0.0f, NULL);
        break;
    case ftFy_MS_SpecialS2:
        Fighter_ChangeMotionState(gobj, ftFy_MS_SpecialAirS2, FTFY_MF_SWAP, fp->cur_anim_frame,
                                  1.0f, 0.0f, NULL);
        break;
    default:
        break;
    }
    ftAnim_8006EBA4(gobj);
}

/* +3014: 364 to 366's collision: the common ground collision (ft_80082708), and off the ground
 * +4394. */
void ftFy_SpecialS2_Coll(HSD_GObj* gobj)
{
    if (ft_80082708(gobj) == false) {
        ftFy_SpecialS2_GroundToAir(gobj);
    }
}

/* +3060: 365 (held): B let go goes to 366 (no ftAnim_8006EBA4). */
void ftFy_SpecialS2Hold_Anim(HSD_GObj* gobj)
{
    if (GET_FIGHTER(gobj)->input.held_buttons[0] & HSD_PAD_B) {
        return;
    }
    Fighter_ChangeMotionState(gobj, ftFy_MS_SpecialS2End, Ft_MF_None, 0.0f, 1.0f, 0.0f, NULL);
}

/* +3094: 366: the common wait at the animation's end. */
void ftFy_SpecialS2End_Anim(HSD_GObj* gobj)
{
    if (!ftAnim_IsFramesRemaining(gobj)) {
        ft_8008A2BC(gobj);
    }
}

/* +30E0: 367 (air): as 364, then 368 at the animation's end. The file reads her position once
 * at the start; nothing moves her before the shot, so the values are the same. */
void ftFy_SpecialAirS2_Anim(HSD_GObj* gobj)
{
    Fighter* fp = GET_FIGHTER(gobj);

    ftFy_SpecialS_BladeOnce(gobj, fp);
    ftFy_SpecialS2_WaveOnFlag(gobj, fp);
    if (!ftAnim_IsFramesRemaining(gobj)) {
        Fighter_ChangeMotionState(gobj, ftFy_MS_SpecialAirS2Hold, Ft_MF_None, 0.0f, 1.0f, 0.0f,
                                  NULL);
    }
}

/* +3200: 367 to 369's physics: the common fall at her gravity and terminal speed, no drift. */
void ftFy_SpecialAirS2_Phys(HSD_GObj* gobj)
{
    Fighter* fp = GET_FIGHTER(gobj);

    ftCommon_Fall(fp, fp->co_attrs.gravity, fp->co_attrs.terminal_velocity);
}

/* +44A0: landing in 367 to 369: the ground state of the same number at the same frame (flags
 * 0x5000), then ftAnim_8006EBA4. */
static void ftFy_SpecialAirS2_AirToGround(HSD_GObj* gobj)
{
    Fighter* fp = GET_FIGHTER(gobj);

    ftCommon_8007D7FC(fp);
    switch (fp->motion_id) {
    case ftFy_MS_SpecialAirS2Hold:
        Fighter_ChangeMotionState(gobj, ftFy_MS_SpecialS2Hold, FTFY_MF_SWAP, fp->cur_anim_frame,
                                  1.0f, 0.0f, NULL);
        break;
    case ftFy_MS_SpecialAirS2End:
        Fighter_ChangeMotionState(gobj, ftFy_MS_SpecialS2End, FTFY_MF_SWAP, fp->cur_anim_frame,
                                  1.0f, 0.0f, NULL);
        break;
    case ftFy_MS_SpecialAirS2:
        Fighter_ChangeMotionState(gobj, ftFy_MS_SpecialS2, FTFY_MF_SWAP, fp->cur_anim_frame,
                                  1.0f, 0.0f, NULL);
        break;
    default:
        break;
    }
    ftAnim_8006EBA4(gobj);
}

/* +3210: 367 to 369's collision: the common air collision, and on landing +44A0. */
void ftFy_SpecialAirS2_Coll(HSD_GObj* gobj)
{
    if (ft_80081D0C(gobj) != false) {
        ftFy_SpecialAirS2_AirToGround(gobj);
    }
}

/* +325C: 368 (held): B let go goes to 369. */
void ftFy_SpecialAirS2Hold_Anim(HSD_GObj* gobj)
{
    if (GET_FIGHTER(gobj)->input.held_buttons[0] & HSD_PAD_B) {
        return;
    }
    Fighter_ChangeMotionState(gobj, ftFy_MS_SpecialAirS2End, Ft_MF_None, 0.0f, 1.0f, 0.0f,
                              NULL);
}

/* +3290: 369: the common fall at the animation's end. */
void ftFy_SpecialAirS2End_Anim(HSD_GObj* gobj)
{
    if (!ftAnim_IsFramesRemaining(gobj)) {
        ftCo_Fall_Enter(gobj);
    }
}
