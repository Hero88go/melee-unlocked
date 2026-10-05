/* ACE's Blastoise: the four special moves (m-ex "ftFunction" of PlBl.dat, all his own code).
 *
 *   neutral  Hydro Pump: one shot of article 0 from bone 34, and he is pushed back at speed 1
 *   side     a dash: ground speed set at frames 10 and 30; in the air Bowser's fall physics
 *   up       a shell spin steered with the stick: frames 6 to 30 fast, 31 to 50 slow
 *   down     Bubble: one shot of article 1 from bone 61 with a small hop; B held from frame 28
 *            restarts the move at frame 5
 *
 * Stick thresholds 0.3 and 0.15 are compared in double on the console (the constants are doubles,
 * +164C), 0.75 in single (+1580); the casts below keep that. */
#include "blastoise.h"

#include <dolphin/os.h>

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
#include <melee/ft/types.h>
#include <melee/lb/lb_00B0.h>
#include <sysdolphin/baselib/controller.h>

#define FTBL_STICK_DEAD 0.3   /* +164C, double */
#define FTBL_STICK_TILT 0.15  /* +165C, double */
#define FTBL_STICK_FULL 0.75f /* +1580 */

static void ftBl_SpecialN_ItemSpawn(HSD_GObj* gobj);
static void ftBl_SpecialLw_ItemSpawn(HSD_GObj* gobj);

/* ---- neutral special: Hydro Pump ---- */

/* +13C8 "SpecialN_Enter": both entries, with the state. */
static void ftBl_SpecialN_EnterState(HSD_GObj* gobj, FtMotionId msid)
{
    Fighter* fp = GET_FIGHTER(gobj);

    fp->throw_flags = 0;
    fp->cmd_vars[0] = 0;
    Fighter_ChangeMotionState(gobj, msid, Ft_MF_None, 0.0f, 1.0f, 0.0f, NULL);
    ftAnim_8006EBA4(gobj);
    fp->accessory4_cb = ftBl_SpecialN_ItemSpawn;
}

/* +01E4 "SpecialN" (slot 4) */
void ftBl_SpecialN_Enter(HSD_GObj* gobj)
{
    ftBl_SpecialN_EnterState(gobj, ftBl_MS_SpecialN);
}

/* +0208 "SpecialAirN" (slot 5) */
void ftBl_SpecialAirN_Enter(HSD_GObj* gobj)
{
    ftBl_SpecialN_EnterState(gobj, ftBl_MS_SpecialAirN);
}

/* +17A0 "SpecialN_Accessory4_Callback": when the script raises the throw flag, he is pushed back
 * (both his air and his ground speed become minus his facing) and the shot leaves from bone 34. */
static void ftBl_SpecialN_ItemSpawn(HSD_GObj* gobj)
{
    Fighter* fp = GET_FIGHTER(gobj);
    ftBl_DatAttrs* sa;
    Vec3 pos;
    Vec3 vel;
    float facing;

    if (!fp->throw_flags_b0) {
        return;
    }
    sa = ftBl_Attrs(fp);
    fp->throw_flags_b0 = false;

    fp->self_vel.x = -fp->facing_dir;
    fp->gr_vel = -fp->facing_dir;

    lb_8000B1CC(fp->parts[FTBL_SPECIALN_BONE].joint, NULL, &pos);
    facing = fp->facing_dir;
    pos.x += facing * sa->specialn_item_offset_x;
    pos.y += sa->specialn_item_offset_y;
    pos.z = 0.0f;
    vel.x = sa->specialn_item_vel_x * facing;
    vel.y = 0.0f;
    vel.z = 0.0f;
    itBl_HydroPump_Spawn(gobj, &pos, &vel, facing);
}

/* +065C "SpecialN_AnimCB" */
void ftBl_SpecialN_Anim(HSD_GObj* gobj)
{
    if (!ftAnim_IsFramesRemaining(gobj)) {
        ft_8008A2BC(gobj);
    }
}

/* +069C "SpecialN_PhysCB" */
void ftBl_SpecialN_Phys(HSD_GObj* gobj)
{
    ft_80084F3C(gobj);
}

/* +06BC "SpecialN_CollCB": off the floor he falls; the move is not carried into the air. */
void ftBl_SpecialN_Coll(HSD_GObj* gobj)
{
    if (!ft_800827A0(gobj)) {
        ftCo_Fall_Enter(gobj);
    }
}

/* +06FC "SpecialAirN_AnimCB" */
void ftBl_SpecialAirN_Anim(HSD_GObj* gobj)
{
    if (!ftAnim_IsFramesRemaining(gobj)) {
        ftCo_Fall_Enter(gobj);
    }
}

/* +073C "SpecialAirN_PhysCB" */
void ftBl_SpecialAirN_Phys(HSD_GObj* gobj)
{
    ft_80084EEC(gobj);
}

/* +075C "SpecialAirN_CollCB": landing ends the move with its own lag. */
void ftBl_SpecialAirN_Coll(HSD_GObj* gobj)
{
    Fighter* fp = GET_FIGHTER(gobj);
    ftBl_DatAttrs* sa = ftBl_Attrs(fp);

    if (ft_80081D0C(gobj)) {
        ftCo_LandingFallSpecial_Enter(gobj, false, sa->specialairn_landing_lag);
    }
}

/* ---- side special: the dash ---- */

/* +143C "SpecialS_Enter": both entries, with the state. */
static void ftBl_SpecialS_EnterState(HSD_GObj* gobj, FtMotionId msid)
{
    Fighter* fp = GET_FIGHTER(gobj);

    fp->throw_flags = 0;
    Fighter_ChangeMotionState(gobj, msid, Ft_MF_None, 0.0f, 1.0f, 0.0f, NULL);
    ftAnim_8006EBA4(gobj);
}

/* +022C "SpecialS" (slot 6) */
void ftBl_SpecialS_Enter(HSD_GObj* gobj)
{
    ftBl_SpecialS_EnterState(gobj, ftBl_MS_SpecialS);
}

/* +0250 "SpecialAirS" (slot 7) */
void ftBl_SpecialAirS_Enter(HSD_GObj* gobj)
{
    ftBl_SpecialS_EnterState(gobj, ftBl_MS_SpecialAirS);
}

/* +07B8 "SpecialS_AnimCB" */
void ftBl_SpecialS_Anim(HSD_GObj* gobj)
{
    if (!ftAnim_IsFramesRemaining(gobj)) {
        ft_8008A2BC(gobj);
    }
}

/* +07F8 "SpecialS_PhysCB": the common ground physics first, then from frame 10 the ground speed
 * is set every frame (the fast value up to frame 30, the slow one after). */
void ftBl_SpecialS_Phys(HSD_GObj* gobj)
{
    Fighter* fp = GET_FIGHTER(gobj);
    ftBl_DatAttrs* sa = ftBl_Attrs(fp);
    float frame;
    float facing;

    ft_80084F3C(gobj);
    frame = fp->cur_anim_frame;
    if (frame >= 10.0f) {
        facing = fp->facing_dir;
        fp->gr_vel = sa->specials_vel_x * facing;
        if (frame >= 30.0f) {
            fp->gr_vel = sa->specials_vel_x_end * facing;
        }
    }
}

/* +0878 "SpecialS_CollCB": off the floor the air state goes on at the same frame. The file
 * changes the state and nothing else (no ftCommon_8007D5D4, no flags kept). */
void ftBl_SpecialS_Coll(HSD_GObj* gobj)
{
    Fighter* fp = GET_FIGHTER(gobj);

    if (!ft_80082708(gobj)) {
        Fighter_ChangeMotionState(gobj, ftBl_MS_SpecialAirS, Ft_MF_None,
                                  fp->cur_anim_frame, 1.0f, 0.0f, NULL);
    }
}

/* +08E8 "SpecialAirS_AnimCB" */
void ftBl_SpecialAirS_Anim(HSD_GObj* gobj)
{
    if (!ftAnim_IsFramesRemaining(gobj)) {
        ftCo_Fall_Enter(gobj);
    }
}

/* +0928 "SpecialAirS_CollCB" */
void ftBl_SpecialAirS_Coll(HSD_GObj* gobj)
{
    Fighter* fp = GET_FIGHTER(gobj);
    ftBl_DatAttrs* sa = ftBl_Attrs(fp);

    if (ft_80081D0C(gobj)) {
        ftCo_LandingFallSpecial_Enter(gobj, false, sa->specialairs_landing_lag);
    }
}

/* ---- up special: the shell spin ---- */

/* +1498 "SpecialHi_Enter": both entries, with the state. */
static void ftBl_SpecialHi_EnterState(HSD_GObj* gobj, FtMotionId msid)
{
    Fighter* fp = GET_FIGHTER(gobj);

    fp->throw_flags = 0;
    Fighter_ChangeMotionState(gobj, msid, Ft_MF_None, 0.0f, 1.0f, 0.0f, NULL);
    ftAnim_8006EBA4(gobj);
}

/* +0274 "SpecialHi" (slot 8) */
void ftBl_SpecialHi_Enter(HSD_GObj* gobj)
{
    ftBl_SpecialHi_EnterState(gobj, ftBl_MS_SpecialHi);
}

/* +0298 "SpecialAirHi" (slot 9) */
void ftBl_SpecialAirHi_Enter(HSD_GObj* gobj)
{
    ftBl_SpecialHi_EnterState(gobj, ftBl_MS_SpecialAirHi);
}

/* +0984 "SpecialHi_AnimCB" */
void ftBl_SpecialHi_Anim(HSD_GObj* gobj)
{
    if (!ftAnim_IsFramesRemaining(gobj)) {
        ft_8008A2BC(gobj);
    }
}

/* +09C4 "SpecialHi_IasaCB": empty. */
void ftBl_SpecialHi_IASA(HSD_GObj* gobj)
{
    (void) gobj;
}

/* The state restarted at a frame, as the spin does when the stick turns him (+0AB4 and its
 * three siblings in the ground routine, +0DEC in the air one). */
static void ftBl_SpecialHi_Restart(HSD_GObj* gobj, FtMotionId msid, float frame)
{
    Fighter_ChangeMotionState(gobj, msid, Ft_MF_None, frame, 1.0f, 0.0f, NULL);
}

/* +09C8 "SpecialHi_PhysCB". The common ground physics run once before frame 6, twice from
 * frame 6 when the stick is inside the dead zone or the frame is past 50 (the file calls
 * ft_80084F3C again on those paths), and the ground speed is set from the stick:
 *   frames 6 to 30   stick past 0.15: 0.7 (1.5 past 0.75), signed by the side
 *   frames 31 to 50  stick past 0.15: 0.3 (0.7 past 0.75); each of those also restarts the state
 *                    at the current frame and turns him to the stick. */
void ftBl_SpecialHi_Phys(HSD_GObj* gobj)
{
    Fighter* fp = GET_FIGHTER(gobj);
    ftBl_DatAttrs* sa = ftBl_Attrs(fp);
    float frame;
    float stick;

    ft_80084F3C(gobj);
    frame = fp->cur_anim_frame;
    if (frame < 6.0f) {
        return;
    }
    if (frame > 50.0f) {
        goto phys_again;
    }
    stick = fp->input.lstick[0].x;
    if ((double) stick < FTBL_STICK_DEAD && (double) stick > -FTBL_STICK_DEAD) {
        /* +0A90 */
        ft_80084F3C(gobj);
        frame = fp->cur_anim_frame;
        if (frame < 6.0f) {
            return;
        }
    }

    /* +0A40 */
    if (!(frame > 30.0f)) {
        stick = fp->input.lstick[0].x;
        if ((double) stick > FTBL_STICK_TILT) {
            fp->gr_vel = sa->specialhi_vel_r;
            if (stick > FTBL_STICK_FULL) {
                fp->gr_vel = sa->specialhi_vel_r_full;
            }
        } else if ((double) stick < -FTBL_STICK_TILT) {
            fp->gr_vel = sa->specialhi_vel_l;
            if (stick < -FTBL_STICK_FULL) {
                fp->gr_vel = sa->specialhi_vel_l_full;
            }
        }
        return;
    }

    /* +0BEC */
    if (!(frame >= 31.0f)) {
        return;
    }
    if (frame > 50.0f) {
        goto phys_again;
    }
    if ((double) fp->input.lstick[0].x > FTBL_STICK_TILT) {
        /* +0AB4 */
        fp->gr_vel = sa->specialhi_end_vel_r;
        ftBl_SpecialHi_Restart(gobj, ftBl_MS_SpecialHi, frame);
        ftCommon_UpdateFacing(fp);
        if (fp->input.lstick[0].x > FTBL_STICK_FULL) {
            /* +0B2C */
            fp->gr_vel = sa->specialhi_end_vel_r_full;
            ftBl_SpecialHi_Restart(gobj, ftBl_MS_SpecialHi, fp->cur_anim_frame);
            ftCommon_UpdateFacing(fp);
        }
        /* +0AFC */
        frame = fp->cur_anim_frame;
        if (frame < 31.0f) {
            return;
        }
        if (frame > 50.0f) {
            goto phys_again;
        }
    }
    /* +0C2C */
    if ((double) fp->input.lstick[0].x < -FTBL_STICK_TILT) {
        /* +0B68 */
        fp->gr_vel = sa->specialhi_end_vel_l;
        ftBl_SpecialHi_Restart(gobj, ftBl_MS_SpecialHi, frame);
        ftCommon_UpdateFacing(fp);
        if (fp->input.lstick[0].x < -FTBL_STICK_FULL) {
            fp->gr_vel = sa->specialhi_end_vel_l_full;
            ftBl_SpecialHi_Restart(gobj, ftBl_MS_SpecialHi, fp->cur_anim_frame);
            ftCommon_UpdateFacing(fp);
        }
    }
    /* +0C40 */
    if (fp->cur_anim_frame < 50.0f) {
        return;
    }
phys_again:
    /* +0C58 */
    ft_80084F3C(gobj);
}

/* +0CB0 "SpecialHi_CollCB": off the floor the air spin goes on at the same frame. */
void ftBl_SpecialHi_Coll(HSD_GObj* gobj)
{
    Fighter* fp = GET_FIGHTER(gobj);

    if (!ft_80082708(gobj)) {
        Fighter_ChangeMotionState(gobj, ftBl_MS_SpecialAirHi, Ft_MF_None,
                                  fp->cur_anim_frame, 1.0f, 0.0f, NULL);
    }
}

/* +0D20 "SpecialAirHi_AnimCB": from frame 50 he drops into the special fall (no interrupt,
 * mobility 0.1, his landing lag). */
void ftBl_SpecialAirHi_Anim(HSD_GObj* gobj)
{
    Fighter* fp = GET_FIGHTER(gobj);

    if (fp->cur_anim_frame >= 50.0f) {
        ftCo_80096900(gobj, 0, 1, false, 0.1f,
                      ftBl_Attrs(fp)->specialairhi_landing_lag);
    }
}

/* +0EA0: while he is coming down, a tilted stick turns him. */
static void ftBl_SpecialAirHi_TurnFalling(Fighter* fp)
{
    float stick;

    if (fp->self_vel.y < 0.0f) {
        stick = fp->input.lstick[0].x;
        if ((double) stick > FTBL_STICK_TILT || (double) stick < -FTBL_STICK_TILT) {
            ftCommon_UpdateFacing(fp);
        }
    }
}

/* +0D78 "SpecialAirHi_PhysCB". The common air physics (ft_80084EEC), then:
 *   frames up to 3   the rise: vertical speed set to +24 every frame; a tilted stick turns him
 *                    and restarts the state at the current frame, a neutral stick ends the call
 *   frames 6 to 30   horizontal speed from the stick: 0.7 (1.5 past 0.75) by side, 0 times his
 *                    facing when the stick is inside 0.15
 *   frames 31 to 50  0.3 (0.7 past 0.75) by side
 * and on every path that reaches the end, the turn while falling. The file's branch order is
 * kept (labels name its offsets): the left side is tested before the right in the late window,
 * and a frame that leaves a window after a turn skips the rest. */
void ftBl_SpecialAirHi_Phys(HSD_GObj* gobj)
{
    Fighter* fp = GET_FIGHTER(gobj);
    ftBl_DatAttrs* sa = ftBl_Attrs(fp);
    float frame;
    float stick;

    ft_80084EEC(gobj);

    if (!(fp->cur_anim_frame > 3.0f)) {
        fp->self_vel.y = sa->specialairhi_vel_y;
        stick = fp->input.lstick[0].x;
        if (!((double) stick > FTBL_STICK_TILT) && (double) stick >= -FTBL_STICK_TILT) {
            return;
        }
        /* +0DE4 */
        ftCommon_UpdateFacing(fp);
        ftBl_SpecialHi_Restart(gobj, ftBl_MS_SpecialAirHi, fp->cur_anim_frame);
    }

    /* +0E14 */
    frame = fp->cur_anim_frame;
    if (frame < 6.0f || frame > 50.0f) {
        goto falling;
    }
    stick = fp->input.lstick[0].x;
    if ((double) stick >= FTBL_STICK_TILT) {
        /* +1058 */
        if (!(frame <= 30.0f)) {
            goto late_check;
        }
        if ((double) stick > FTBL_STICK_TILT) {
            fp->self_vel.x = sa->specialhi_vel_r;
            ftCommon_UpdateFacing(fp);
            if (fp->input.lstick[0].x > FTBL_STICK_FULL) {
                /* +0ED0 */
                fp->self_vel.x = sa->specialhi_vel_r_full;
                ftCommon_UpdateFacing(fp);
            }
            /* +10A0 */
            frame = fp->cur_anim_frame;
            if (frame < 6.0f) {
                goto falling;
            }
            if (frame > 30.0f) {
                goto late_window;
            }
        }
    } else {
        if ((double) stick > -FTBL_STICK_TILT) {
            /* +0E64 */
            fp->self_vel.x = fp->facing_dir * 0.0f;
        }
        /* +0E78 */
        if (!(frame <= 30.0f)) {
            goto late_check;
        }
    }

    /* +0E8C */
    if (!((double) fp->input.lstick[0].x < -FTBL_STICK_TILT)) {
        goto falling;
    }
    /* +0EE4 */
    fp->self_vel.x = sa->specialhi_vel_l;
    ftCommon_UpdateFacing(fp);
    if (fp->input.lstick[0].x < -FTBL_STICK_FULL) {
        /* +0F78 */
        fp->self_vel.x = sa->specialhi_vel_l_full;
        ftCommon_UpdateFacing(fp);
    }

late_window:
    /* +0F08 */
    frame = fp->cur_anim_frame;
    if (frame < 31.0f || frame > 50.0f) {
        goto falling;
    }
    goto late;

late_check:
    /* +10D0 */
    if (!(fp->cur_anim_frame >= 31.0f)) {
        goto falling;
    }

late:
    /* +0F34 */
    if ((double) fp->input.lstick[0].x < -FTBL_STICK_TILT) {
        /* +0F8C */
        fp->self_vel.x = sa->specialhi_end_vel_l;
        ftCommon_UpdateFacing(fp);
        if (fp->input.lstick[0].x < -FTBL_STICK_FULL) {
            fp->self_vel.x = sa->specialhi_end_vel_l_full;
            ftCommon_UpdateFacing(fp);
        }
        /* +0FC0 */
        frame = fp->cur_anim_frame;
        if (frame < 31.0f || frame > 50.0f) {
            goto falling;
        }
    }
    /* +0F48 */
    if ((double) fp->input.lstick[0].x > FTBL_STICK_TILT) {
        /* +0FF0 */
        fp->self_vel.x = sa->specialhi_end_vel_r;
        ftCommon_UpdateFacing(fp);
        if (fp->input.lstick[0].x > FTBL_STICK_FULL) {
            fp->self_vel.x = sa->specialhi_end_vel_r_full;
            ftCommon_UpdateFacing(fp);
        }
    }

falling:
    ftBl_SpecialAirHi_TurnFalling(fp);
}

/* +1674 "SpecialHiEnd_OnLand" */
static void ftBl_SpecialAirHi_Land(HSD_GObj* gobj)
{
    Fighter* fp = GET_FIGHTER(gobj);

    ftCo_LandingFallSpecial_Enter(gobj, false,
                                  ftBl_Attrs(fp)->specialairhi_landing_lag);
}

/* +10EC "SpecialAirHi_CollCB": the air collision with ledge grab, landing into the lag above. */
void ftBl_SpecialAirHi_Coll(HSD_GObj* gobj)
{
    ft_80083A48(gobj, ftBl_SpecialAirHi_Land);
}

/* ---- down special: Bubble ---- */

/* +14F4 "SpecialLw_Enter": both entries, with the state. */
static void ftBl_SpecialLw_EnterState(HSD_GObj* gobj, FtMotionId msid)
{
    Fighter* fp = GET_FIGHTER(gobj);

    fp->throw_flags = 0;
    fp->cmd_vars[0] = 0;
    Fighter_ChangeMotionState(gobj, msid, Ft_MF_None, 0.0f, 1.0f, 0.0f, NULL);
    ftAnim_8006EBA4(gobj);
    fp->accessory4_cb = ftBl_SpecialLw_ItemSpawn;
}

/* +02BC "SpecialLw" (slot 10) */
void ftBl_SpecialLw_Enter(HSD_GObj* gobj)
{
    ftBl_SpecialLw_EnterState(gobj, ftBl_MS_SpecialLw);
}

/* +02E0 "SpecialAirLw" (slot 11) */
void ftBl_SpecialAirLw_Enter(HSD_GObj* gobj)
{
    ftBl_SpecialLw_EnterState(gobj, ftBl_MS_SpecialAirLw);
}

/* +16A4 "SpecialLw_Accessory4_Callback": when the script raises the throw flag he hops (0.5 up,
 * 0.2 back; the product is taken in double and rounded, fmul and frsp) and the bubble leaves from
 * bone 61, sinking at 0.15. The hop is written on the ground too, where the ground physics do
 * not use it. */
static void ftBl_SpecialLw_ItemSpawn(HSD_GObj* gobj)
{
    Fighter* fp = GET_FIGHTER(gobj);
    ftBl_DatAttrs* sa;
    Vec3 pos;
    Vec3 vel;
    float facing;

    if (!fp->throw_flags_b0) {
        return;
    }
    sa = ftBl_Attrs(fp);
    fp->throw_flags_b0 = false;

    fp->self_vel.y = 0.5f;
    fp->self_vel.x = -(float) ((double) fp->facing_dir * 0.2);

    lb_8000B1CC(fp->parts[FTBL_SPECIALLW_BONE].joint, NULL, &pos);
    facing = fp->facing_dir;
    pos.x += facing * sa->speciallw_item_offset_x;
    pos.y += sa->speciallw_item_offset_y;
    pos.z = 0.0f;
    vel.x = sa->speciallw_item_vel_x * facing;
    vel.y = -0.15f;
    vel.z = 0.0f;
    itBl_Bubble_Spawn(gobj, &pos, &vel, facing);
}

/* +1154 "SpecialLw_IASACB" and +128C "SpecialAirLw_IASACB": from frame 28, B held starts the
 * same state again at frame 5 (another bubble). */
static void ftBl_SpecialLw_Repeat(HSD_GObj* gobj, FtMotionId msid)
{
    Fighter* fp = GET_FIGHTER(gobj);

    if (fp->cur_anim_frame >= 28.0f && (fp->input.held_buttons[0] & HSD_PAD_B)) {
        Fighter_ChangeMotionState(gobj, msid, Ft_MF_None, 5.0f, 1.0f, 0.0f, NULL);
        ftAnim_8006EBA4(gobj);
        fp->accessory4_cb = ftBl_SpecialLw_ItemSpawn;
    }
}

/* +1114 "SpecialLw_AnimCB" */
void ftBl_SpecialLw_Anim(HSD_GObj* gobj)
{
    if (!ftAnim_IsFramesRemaining(gobj)) {
        ft_8008A2BC(gobj);
    }
}

/* +1154 "SpecialLw_IASACB" */
void ftBl_SpecialLw_IASA(HSD_GObj* gobj)
{
    ftBl_SpecialLw_Repeat(gobj, ftBl_MS_SpecialLw);
}

/* +11EC "SpecialLw_PhysCB" */
void ftBl_SpecialLw_Phys(HSD_GObj* gobj)
{
    ft_80084F3C(gobj);
}

/* +120C "SpecialLw_CollCB" */
void ftBl_SpecialLw_Coll(HSD_GObj* gobj)
{
    if (!ft_800827A0(gobj)) {
        ftCo_Fall_Enter(gobj);
    }
}

/* +124C "SpecialAirLw_AnimCB" */
void ftBl_SpecialAirLw_Anim(HSD_GObj* gobj)
{
    if (!ftAnim_IsFramesRemaining(gobj)) {
        ftCo_Fall_Enter(gobj);
    }
}

/* +128C "SpecialAirLw_IASACB" */
void ftBl_SpecialAirLw_IASA(HSD_GObj* gobj)
{
    ftBl_SpecialLw_Repeat(gobj, ftBl_MS_SpecialAirLw);
}

/* +1324 "SpecialAirLw_PhysCB" */
void ftBl_SpecialAirLw_Phys(HSD_GObj* gobj)
{
    ft_80084EEC(gobj);
}

/* +1344 "SpecialAirLw_CollCB": landing goes on in the ground state at the same frame, with the
 * shot callback set again (the state change clears it). */
void ftBl_SpecialAirLw_Coll(HSD_GObj* gobj)
{
    Fighter* fp = GET_FIGHTER(gobj);

    if (ft_80081D0C(gobj)) {
        Fighter_ChangeMotionState(gobj, ftBl_MS_SpecialLw, Ft_MF_None,
                                  fp->cur_anim_frame, 1.0f, 0.0f, NULL);
        ftAnim_8006EBA4(gobj);
        fp->accessory4_cb = ftBl_SpecialLw_ItemSpawn;
    }
}
