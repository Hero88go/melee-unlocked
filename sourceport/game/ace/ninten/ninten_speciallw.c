/* PlNt.dat: reflector down special, code+2614..3164 and +3A58..4238.
 * Lucas's magnet transitions, with Ninten's attribute offsets, reflect callback,
 * and the independent Hold counter at fp+234C. The disc's aerial Start stays
 * aerial on landing. End's landing passes a GObj to a Fighter clamp on disc;
 * its visible result is no velocity clamp, retained here. */
#include "ninten.h"

#include <melee/ef/efasync.h>
#include <melee/ef/eflib.h>
#include <melee/ft/fighter.h>
#include <melee/ft/ft_081B.h>
#include <melee/ft/ft_084E.h>
#include <melee/ft/ft_0881.h>
#include <melee/ft/ftanim.h>
#include <melee/ft/ftcoll.h>
#include <melee/ft/ftcommon.h>
#include <melee/ft/ftparts.h>
#include <sysdolphin/baselib/controller.h>

/* +4890: the hit callback restarts grounded Start even when reflected in air. */
static void ftNt_ReflectHit(HSD_GObj* gobj)
{
    Fighter* fp = GET_FIGHTER(gobj);
    Fighter_ChangeMotionState(gobj, ftLc_MS_SpecialLwStart, 0, 0.0f, 1.0f, 0.0f, NULL);
    fp->facing_dir = fp->ReflectAttr.x1A2C_reflectHitDirection;
}

static void ftNt_Reflect(HSD_GObj* gobj)
{
    ftColl_CreateReflectHit(gobj, DISC_REFLECT(&ftNt_Attrs(GET_FIGHTER(gobj))->xC0_LW_REFLECT),
                            ftNt_ReflectHit);
}

/* +3378 */
static void ftNt_SpecialLw_Init(HSD_GObj* gobj)
{
    Fighter* fp = GET_FIGHTER(gobj);
    ftNintenAttributes* attrs = ftNt_Attrs(fp);
    ftNinten_LwMotionVars* mv = ftNt_LwMV(fp);
    fp->cmd_vars[0] = fp->cmd_vars[1] = fp->cmd_vars[2] = fp->cmd_vars[3] = 0;
    ftLc_X233C(fp) = (s32) attrs->xA0_LW_MIN_FRAMES;
    mv->released = 0;
    mv->gravity_delay = 0;
    mv->counter = attrs->xA8_LW_COUNTER;
    mv->sfx_timer = attrs->xB0_LW_SFX_DELAY;
    mv->unused = 0;
}

void ftNt_SpecialLw_Enter(HSD_GObj* gobj)
{
    ftNt_SpecialLw_Init(gobj);
    Fighter_ChangeMotionState(gobj, ftLc_MS_SpecialLwStart, 0, 0.0f, 1.0f, 0.0f, NULL);
    ftAnim_8006EBA4(gobj);
}

void ftNt_SpecialAirLw_Enter(HSD_GObj* gobj)
{
    Fighter* fp = GET_FIGHTER(gobj);
    ftNt_SpecialLw_Init(gobj);
    fp->self_vel.y = 0.0f;
    fp->self_vel.x /= ftNt_Attrs(fp)->xB4_LW_AIR_VEL_DIV;
    Fighter_ChangeMotionState(gobj, ftLc_MS_SpecialAirLwStart, 0, 0.0f, 1.0f, 0.0f, NULL);
    ftAnim_8006EBA4(gobj);
}

static void ftNt_LwCheckRelease(Fighter* fp)
{
    if (!(fp->input.held_buttons[0] & HSD_PAD_B)) {
        ftNt_LwMV(fp)->released = 1;
    }
}

static void ftNt_LwGfx(HSD_GObj* gobj)
{
    Fighter* fp = GET_FIGHTER(gobj);
    if (!fp->x2219_b0) {
        efAsync_Spawn(gobj, &fp->x60C, 0, 0x138A, fp->parts[ftLc_Part_Magnet].joint);
        fp->x2219_b0 = true;
    }
    fp->pre_hitlag_cb = efLib_PauseAll;
    fp->post_hitlag_cb = efLib_ResumeAll;
}

static void ftNt_LwSfx(Fighter* fp)
{
    ftNinten_LwMotionVars* mv = ftNt_LwMV(fp);
    if (--mv->sfx_timer <= 0) {
        ft_80088478(fp, 0x13D9, 0x7F, 0x40);
        mv->sfx_timer = 40;
    }
}

static void ftNt_LwEnterHold(HSD_GObj* gobj)
{
    Fighter* fp = GET_FIGHTER(gobj);
    Fighter_ChangeMotionState(gobj, fp->ground_or_air == GA_Ground ? ftLc_MS_SpecialLwHold
                                                               : ftLc_MS_SpecialAirLwHold,
                              Ft_MF_KeepGfx, 0.0f, 1.0f, 0.0f, NULL);
    ftNt_Reflect(gobj);
}

static void ftNt_LwEnterEnd(HSD_GObj* gobj)
{
    Fighter* fp = GET_FIGHTER(gobj);
    ftCommon_8007DB24(gobj);
    Fighter_ChangeMotionState(gobj, fp->ground_or_air == GA_Ground ? ftLc_MS_SpecialLwEnd
                                                               : ftLc_MS_SpecialAirLwEnd,
                              ftLc_MF_MagnetEnd, 0.0f, 1.0f, 0.0f, NULL);
}

static void ftNt_LwToAir(HSD_GObj* gobj, FtMotionId state, MotionFlags flags, bool reflect)
{
    Fighter* fp = GET_FIGHTER(gobj);
    ftCommon_8007D5D4(fp);
    Fighter_ChangeMotionState(gobj, state, flags, fp->cur_anim_frame, 1.0f, 0.0f, NULL);
    if (reflect) {
        ftNt_Reflect(gobj);
    }
}

static void ftNt_LwToGround(HSD_GObj* gobj, FtMotionId state, MotionFlags flags, bool reflect,
                           bool clamp)
{
    Fighter* fp = GET_FIGHTER(gobj);
    ftCommon_8007D7FC(fp);
    Fighter_ChangeMotionState(gobj, state, flags, fp->cur_anim_frame, 1.0f, 0.0f, NULL);
    if (clamp) {
        ftCommon_ClampAirDrift(fp);
    }
    if (reflect) {
        ftNt_Reflect(gobj);
    }
}

void ftNt_SpecialLwStart_Anim(HSD_GObj* gobj)
{
    Fighter* fp = GET_FIGHTER(gobj);
    ftNt_LwCheckRelease(fp);
    if (!ftAnim_IsFramesRemaining(gobj) || fp->cmd_vars[1] == 1) {
        ftNt_LwGfx(gobj);
        ftNt_LwMV(fp)->sfx_timer = 0;
        ftNt_LwEnterHold(gobj);
    }
}

void ftNt_SpecialLwStart_IASA(HSD_GObj* gobj)
{
    Fighter* fp = GET_FIGHTER(gobj);
    if (fp->cmd_vars[0] == 0) {
        float stick = fp->input.lstick[0].x;
        fp->cmd_vars[0] = 1;
        if (stick < 0.0f) {
            stick = -stick;
        }
        if (ftNt_Attrs(fp)->xBC_LW_TURN_STICK < stick) {
            ftCommon_UpdateFacing(fp);
            ftPartSetRotY(fp, 0, fp->facing_dir * 1.5707964f);
        }
    }
}

void ftNt_SpecialLwHold_Anim(HSD_GObj* gobj)
{
    Fighter* fp = GET_FIGHTER(gobj);
    ftNinten_LwMotionVars* mv = ftNt_LwMV(fp);
    ftNintenAttributes* attrs = ftNt_Attrs(fp);
    ftNt_LwCheckRelease(fp);
    if (ftLc_X233C(fp) > 0 && --ftLc_X233C(fp) > 0) {
        /* Keep holding during the minimum duration. */
    } else if (mv->released) {
        ftNt_LwEnterEnd(gobj);
    }
    ftNt_LwSfx(fp);
    if (--mv->counter <= 0) {
        mv->counter = attrs->xA8_LW_COUNTER;
        Fighter_8006CF5C(fp, attrs->xAC_LW_COUNTER_ACTION);
    }
}

void ftNt_SpecialLwHold_Phys(HSD_GObj* gobj)
{
    ft_80084F3C(gobj);
    ftColl_8007AEF8(gobj);
}

void ftNt_SpecialLwHit_Anim(HSD_GObj* gobj)
{
    Fighter* fp = GET_FIGHTER(gobj);
    ftNt_LwCheckRelease(fp);
    if (ftLc_X233C(fp) > 0) {
        ftLc_X233C(fp)--;
    }
    if (!ftAnim_IsFramesRemaining(gobj)) {
        ftNt_LwEnterHold(gobj);
        ftCommon_8007DB24(gobj);
        ftNt_LwGfx(gobj);
    }
    ftNt_LwSfx(fp);
}

void ftNt_SpecialLwTurn_Anim(HSD_GObj* gobj)
{
    Fighter* fp = GET_FIGHTER(gobj);
    ftNinten_LwMotionVars* mv = ftNt_LwMV(fp);
    float frames = ftNt_Attrs(fp)->xA4_LW_TURN_FRAMES;
    ftNt_LwCheckRelease(fp);
    if (ftLc_X233C(fp) > 0) {
        ftLc_X233C(fp)--;
    }
    mv->turn_timer--;
    if (fp->cmd_vars[0] == 0 && (float) mv->turn_timer <= frames) {
        fp->cmd_vars[0] = 1;
        fp->facing_dir = -fp->facing_dir;
    }
    ftPartSetRotY(fp, 0, -(float) ((180.0f / frames) * 0.017453292 - ftPartGetRotZ(fp, 0)));
    if (mv->turn_timer <= 0) {
        if (ftLc_X233C(fp) <= 0 && mv->released) {
            if (fp->ground_or_air == GA_Ground) {
                ftNt_LwToGround(gobj, ftLc_MS_SpecialLwTurn, ftLc_MF_Switch, false, true);
            } else {
                ftNt_LwToAir(gobj, ftLc_MS_SpecialAirLwTurn, ftLc_MF_Switch, false);
            }
        } else {
            ftNt_LwEnterHold(gobj);
        }
    }
}

void ftNt_SpecialAirLwStart_Phys(HSD_GObj* gobj)
{
    Fighter* fp = GET_FIGHTER(gobj);
    ftNinten_LwMotionVars* mv = ftNt_LwMV(fp);
    if (mv->gravity_delay != 0) {
        mv->gravity_delay--;
    } else {
        ftCommon_Fall(fp, ftNt_Attrs(fp)->xB8_LW_FALL_ACCEL, fp->co_attrs.terminal_velocity);
    }
    ftCommon_CalcSelfAccel_DeaccelQuickAir(fp);
}

void ftNt_SpecialAirLwHold_Phys(HSD_GObj* gobj)
{
    ftNt_SpecialAirLwStart_Phys(gobj);
    ftColl_8007AEF8(gobj);
}

void ftNt_SpecialLwStart_Coll(HSD_GObj* gobj)
{
    if (!ft_80082708(gobj)) ftNt_LwToAir(gobj, ftLc_MS_SpecialAirLwStart, ftLc_MF_SwitchGfx, false);
}
void ftNt_SpecialLwHold_Coll(HSD_GObj* gobj)
{
    if (!ft_80082708(gobj)) ftNt_LwToAir(gobj, ftLc_MS_SpecialAirLwHold, ftLc_MF_SwitchGfx, true);
}
void ftNt_SpecialLwHit_Coll(HSD_GObj* gobj)
{
    if (!ft_80082708(gobj)) ftNt_LwToAir(gobj, ftLc_MS_SpecialAirLwHit, ftLc_MF_SwitchGfx, true);
}
void ftNt_SpecialLwEnd_Coll(HSD_GObj* gobj)
{
    if (!ft_80082708(gobj)) ftNt_LwToAir(gobj, ftLc_MS_SpecialAirLwEnd, ftLc_MF_SwitchGfx, false);
}
void ftNt_SpecialLwTurn_Coll(HSD_GObj* gobj)
{
    if (!ft_80082708(gobj)) ftNt_LwToAir(gobj, ftLc_MS_SpecialAirLwTurn, ftLc_MF_Switch, false);
}
void ftNt_SpecialAirLwStart_Coll(HSD_GObj* gobj)
{
    if (ft_80081D0C(gobj)) ftNt_LwToAir(gobj, ftLc_MS_SpecialAirLwStart, ftLc_MF_SwitchGfx, false);
}
void ftNt_SpecialAirLwHold_Coll(HSD_GObj* gobj)
{
    if (ft_80081D0C(gobj)) ftNt_LwToGround(gobj, ftLc_MS_SpecialLwHold, ftLc_MF_SwitchGfx, true, true);
}
void ftNt_SpecialAirLwHit_Coll(HSD_GObj* gobj)
{
    if (ft_80081D0C(gobj)) ftNt_LwToGround(gobj, ftLc_MS_SpecialLwHit, ftLc_MF_SwitchGfx, true, true);
}
void ftNt_SpecialAirLwEnd_Coll(HSD_GObj* gobj)
{
    if (ft_80081D0C(gobj)) ftNt_LwToGround(gobj, ftLc_MS_SpecialLwEnd, ftLc_MF_SwitchGfx, false, false);
}
void ftNt_SpecialAirLwTurn_Coll(HSD_GObj* gobj)
{
    if (ft_80081D0C(gobj)) ftNt_LwToGround(gobj, ftLc_MS_SpecialLwTurn, ftLc_MF_Switch, false, true);
}
