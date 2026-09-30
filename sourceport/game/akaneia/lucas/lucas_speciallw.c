/* Akaneia's Lucas: down special, PSI Magnet. Start, a looping Hold with an absorb bubble, Hit
 * when something is absorbed (heals), and End; ground and air copies switch into each other on
 * landing/leaving the ground. The Turn states (363/368) are only reachable from themselves on the
 * disc; they are carried over as written (see NOTES.md). Mirrors Ness's ftnessspeciallw.c. */
#include "lucas.h"

#include <melee/ef/eflib.h>
#include <melee/ef/efasync.h>
#include <melee/ft/fighter.h>
#include <melee/ft/ft_081B.h>
#include <melee/ft/ft_084E.h>
#include <melee/ft/ft_0881.h>
#include <melee/ft/ftanim.h>
#include <melee/ft/ftcoll.h>
#include <melee/ft/ftcommon.h>
#include <melee/ft/ftparts.h>
#include <melee/pl/player.h>
#include <melee/pl/plbonuslib.h>
#include <sysdolphin/baselib/controller.h>

#define LC_HALF_PI 1.5707963267948966
#define LC_DEG_TO_RAD 0.017453292 /* .rodata.cst8+0x20 */

static void ftLc_Magnet_Absorb(HSD_GObj* gobj)
{
    Fighter* fp = GET_FIGHTER(gobj);
    ftColl_CreateAbsorbHit(gobj, DISC_ABSORB(&ftLc_Attrs(fp)->xB4_MAGNET_ABSORB));
}

/* code+0x3B34 "SpecialLw_Init" (called before the state change). */
static void ftLc_SpecialLw_Init(HSD_GObj* gobj)
{
    Fighter* fp = GET_FIGHTER(gobj);
    ftLucasAttributes* attrs = ftLc_Attrs(fp);
    ftLucas_MotionVars* mv = ftLc_MV(fp);
    fp->cmd_vars[0] = 0;
    fp->cmd_vars[1] = 0;
    fp->cmd_vars[2] = 0;
    fp->cmd_vars[3] = 0;
    ftLc_X233C(fp) = (s32) attrs->x8C_MAGNET_MIN_FRAMES;
    mv->speciallw.released = 0;
    mv->speciallw.gravity_delay = 0;
    mv->speciallw.sfx_timer = attrs->x9C_MAGNET_SFX_DELAY;
    mv->speciallw.x2350 = 0.0f;
}

/* m-ex speciallw, code+0x790 */
void ftLc_SpecialLw_Enter(HSD_GObj* gobj)
{
    ftLc_SpecialLw_Init(gobj);
    Fighter_ChangeMotionState(gobj, ftLc_MS_SpecialLwStart, 0, 0.0f, 1.0f, 0.0f, NULL);
    ftAnim_8006EBA4(gobj);
}

/* m-ex specialairlw, code+0x7EC */
void ftLc_SpecialAirLw_Enter(HSD_GObj* gobj)
{
    Fighter* fp = GET_FIGHTER(gobj);
    ftLc_SpecialLw_Init(gobj);
    fp->self_vel.y = 0.0f;
    fp->self_vel.x = fp->self_vel.x / ftLc_Attrs(fp)->xA0_MAGNET_AIR_VEL_X_DIV;
    Fighter_ChangeMotionState(gobj, ftLc_MS_SpecialAirLwStart, 0, 0.0f, 1.0f, 0.0f, NULL);
    ftAnim_8006EBA4(gobj);
}

/* m-ex onabsorb, code+0x864 (Ness's ftNs_AbsorbThink_DecideAction; the heal is not truncated
 * before the multiply here). */
void ftLc_SpecialLw_OnAbsorb(HSD_GObj* gobj)
{
    Fighter* fp = GET_FIGHTER(gobj);
    ftLucasAttributes* attrs = ftLc_Attrs(fp);
    float heal = (float) fp->AbsorbAttr.x1A44_damageTaken * attrs->xAC_MAGNET_HEAL_MUL;
    float percent = fp->dmg.x1830_percent - heal;

    if (percent < 0.0f) {
        heal = heal + percent;
        fp->dmg.x1830_percent = 0.0f;
    } else {
        fp->dmg.x1830_percent = percent;
    }
    Player_SetHPByIndex(fp->player_idx, fp->is_sub_fighter, (s32) fp->dmg.x1830_percent);
    pl_80040B8C(fp->player_idx, fp->is_sub_fighter, (s32) heal);

    fp->facing_dir = fp->AbsorbAttr.x1A40_absorbHitDirection;
    if ((fp->motion_id != ftLc_MS_SpecialLwHit && fp->motion_id != ftLc_MS_SpecialAirLwHit) ||
        !(attrs->x94_MAGNET_HIT_REFRESH_FRAME >= fp->cur_anim_frame))
    {
        Fighter_ChangeMotionState(gobj,
                                  fp->ground_or_air == GA_Ground ? ftLc_MS_SpecialLwHit
                                                                 : ftLc_MS_SpecialAirLwHit,
                                  Ft_MF_KeepGfx, 0.0f, 1.0f, 0.0f, NULL);
        ftLc_Magnet_Absorb(gobj);
    }
}

/* The bubble effect and the hitlag effect pause/resume, as Ness. */
static void ftLc_Magnet_StartGfx(HSD_GObj* gobj)
{
    Fighter* fp = GET_FIGHTER(gobj);
    if (!fp->x2219_b0) {
        efAsync_Spawn(gobj, &fp->x60C, 0, 0x138A, fp->parts[ftLc_Part_Magnet].joint);
        fp->x2219_b0 = true;
    }
    fp->pre_hitlag_cb = efLib_PauseAll;
    fp->post_hitlag_cb = efLib_ResumeAll;
}

/* The looping hum while the magnet is up. */
static void ftLc_Magnet_Sfx(Fighter* fp)
{
    ftLucas_MotionVars* mv = ftLc_MV(fp);
    mv->speciallw.sfx_timer--;
    if (mv->speciallw.sfx_timer <= 0) {
        ft_80088478(fp, 0x13D9, 0x7F, 0x40);
        mv->speciallw.sfx_timer = 40;
    }
}

static void ftLc_Magnet_CheckRelease(Fighter* fp)
{
    if (!(fp->input.held_buttons[0] & HSD_PAD_B)) {
        ftLc_MV(fp)->speciallw.released = 1;
    }
}

/* code+0x4CA4 / 0x4D0C "SpecialLw_Enter{Grounded,Aerial}Loop" (and code+0x4F04, which is the same
 * pair behind a ground check that always returns 1). */
static void ftLc_Magnet_EnterHold(HSD_GObj* gobj)
{
    Fighter* fp = GET_FIGHTER(gobj);
    Fighter_ChangeMotionState(gobj,
                              fp->ground_or_air == GA_Ground ? ftLc_MS_SpecialLwHold
                                                             : ftLc_MS_SpecialAirLwHold,
                              Ft_MF_KeepGfx, 0.0f, 1.0f, 0.0f, NULL);
    ftLc_Magnet_Absorb(gobj);
}

/* code+0x4DDC / 0x4E34 "SpecialLw_Enter_{Grounded,Aerial}_End" */
static void ftLc_Magnet_EnterEnd(HSD_GObj* gobj)
{
    Fighter* fp = GET_FIGHTER(gobj);
    ftCommon_8007DB24(gobj);
    Fighter_ChangeMotionState(gobj,
                              fp->ground_or_air == GA_Ground ? ftLc_MS_SpecialLwEnd
                                                             : ftLc_MS_SpecialAirLwEnd,
                              ftLc_MF_MagnetEnd, 0.0f, 1.0f, 0.0f, NULL);
}

/* Ground <-> air switches keep the frame. `absorb` re-creates the bubble; the landing copies clamp
 * the air drift (`clamp`), except End: code+0x5354 passes the GObj instead of the Fighter to
 * ftCommon_ClampAirDrift, so on console it clamps a float inside the GObj's neighbourhood and
 * never Lucas's velocity. Lucas-visible behavior is "no clamp", which is what is done here. */
static void ftLc_Magnet_ToAir(HSD_GObj* gobj, FtMotionId msid, MotionFlags flags, bool absorb)
{
    Fighter* fp = GET_FIGHTER(gobj);
    ftCommon_8007D5D4(fp);
    Fighter_ChangeMotionState(gobj, msid, flags, fp->cur_anim_frame, 1.0f, 0.0f, NULL);
    if (absorb) {
        ftLc_Magnet_Absorb(gobj);
    }
}

static void ftLc_Magnet_ToGround(HSD_GObj* gobj, FtMotionId msid, MotionFlags flags, bool absorb,
                                 bool clamp)
{
    Fighter* fp = GET_FIGHTER(gobj);
    ftCommon_8007D7FC(fp);
    Fighter_ChangeMotionState(gobj, msid, flags, fp->cur_anim_frame, 1.0f, 0.0f, NULL);
    if (clamp) {
        ftCommon_ClampAirDrift(fp);
    }
    if (absorb) {
        ftLc_Magnet_Absorb(gobj);
    }
}

/* ---------------------------------------------------------------------------------------------
 * Start (shared by 359 and 364).
 * ------------------------------------------------------------------------------------------- */

/* code+0x2938 */
void ftLc_SpecialLwStart_Anim(HSD_GObj* gobj)
{
    Fighter* fp = GET_FIGHTER(gobj);
    ftLc_Magnet_CheckRelease(fp);
    if (!ftAnim_IsFramesRemaining(gobj) || fp->cmd_vars[1] == 1) {
        ftLc_Magnet_StartGfx(gobj);
        ftLc_MV(fp)->speciallw.sfx_timer = 0;
        ftLc_Magnet_EnterHold(gobj);
    }
}

/* code+0x2A20: turn around on the first frame if the stick says so. */
void ftLc_SpecialLwStart_IASA(HSD_GObj* gobj)
{
    Fighter* fp = GET_FIGHTER(gobj);
    if (fp->cmd_vars[0] == 0) {
        float stick = fp->input.lstick[0].x;
        fp->cmd_vars[0] = 1;
        if (stick < 0.0f) {
            stick = -stick;
        }
        if (ftLc_Attrs(fp)->xB0_MAGNET_TURN_STICK < stick) {
            ftCommon_UpdateFacing(fp);
            ftPartSetRotY(fp, 0, fp->facing_dir * (float) LC_HALF_PI);
        }
    }
}

/* code+0x2AB0 */
void ftLc_SpecialLwStart_Phys(HSD_GObj* gobj)
{
    ft_80084F3C(gobj);
}

/* code+0x2AD0 */
void ftLc_SpecialLwStart_Coll(HSD_GObj* gobj)
{
    if (!ft_80082708(gobj)) {
        ftLc_Magnet_ToAir(gobj, ftLc_MS_SpecialAirLwStart, ftLc_MF_SwitchGfx, false);
    }
}

/* ---------------------------------------------------------------------------------------------
 * Hold (360/365), Hit (361/366), End (362/367).
 * ------------------------------------------------------------------------------------------- */

/* code+0x2B10: keep the magnet up for the minimum time, then drop it once B is released. */
void ftLc_SpecialLwHold_Anim(HSD_GObj* gobj)
{
    Fighter* fp = GET_FIGHTER(gobj);
    ftLucas_MotionVars* mv = ftLc_MV(fp);

    ftLc_Magnet_CheckRelease(fp);
    if (ftLc_X233C(fp) > 0 && --ftLc_X233C(fp) > 0) {
        /* still in the minimum time */
    } else if (mv->speciallw.released) {
        ftLc_Magnet_EnterEnd(gobj);
    }
    ftLc_Magnet_Sfx(fp);
}

void ftLc_SpecialLw_NoIASA(HSD_GObj* gobj) {}

/* code+0x2C00 */
void ftLc_SpecialLwHold_Phys(HSD_GObj* gobj)
{
    ft_80084F3C(gobj);
    ftColl_8007AF10(gobj);
}

/* code+0x2C34 */
void ftLc_SpecialLwHold_Coll(HSD_GObj* gobj)
{
    if (!ft_80082708(gobj)) {
        ftLc_Magnet_ToAir(gobj, ftLc_MS_SpecialAirLwHold, ftLc_MF_SwitchGfx, true);
    }
}

/* code+0x2C74 / 0x31C8 (identical) */
void ftLc_SpecialLwHit_Anim(HSD_GObj* gobj)
{
    Fighter* fp = GET_FIGHTER(gobj);
    ftLc_Magnet_CheckRelease(fp);
    if (ftLc_X233C(fp) > 0) {
        ftLc_X233C(fp)--;
    }
    if (!ftAnim_IsFramesRemaining(gobj)) {
        ftLc_Magnet_EnterHold(gobj);
        ftCommon_8007DB24(gobj);
        ftLc_Magnet_StartGfx(gobj);
    }
    ftLc_Magnet_Sfx(fp);
}

/* code+0x2DA8 */
void ftLc_SpecialLwHit_Coll(HSD_GObj* gobj)
{
    if (!ft_80082708(gobj)) {
        ftLc_Magnet_ToAir(gobj, ftLc_MS_SpecialAirLwHit, ftLc_MF_SwitchGfx, true);
    }
}

/* code+0x2DE8 */
void ftLc_SpecialLwEnd_Anim(HSD_GObj* gobj)
{
    if (!ftAnim_IsFramesRemaining(gobj)) {
        ftCommon_8007DB24(gobj);
        ftCommon_8007D92C(gobj);
    }
}

/* code+0x2E34 */
void ftLc_SpecialLwEnd_Phys(HSD_GObj* gobj)
{
    ft_80084F3C(gobj);
}

/* code+0x2E54 */
void ftLc_SpecialLwEnd_Coll(HSD_GObj* gobj)
{
    if (!ft_80082708(gobj)) {
        ftLc_Magnet_ToAir(gobj, ftLc_MS_SpecialAirLwEnd, ftLc_MF_SwitchGfx, false);
    }
}

/* ---------------------------------------------------------------------------------------------
 * Turn (363/368). Nothing enters these but their own transitions; kept for completeness.
 * ------------------------------------------------------------------------------------------- */

/* code+0x5090 "SpecialLw_Hold2_CheckChangeState" */
static void ftLc_SpecialLwTurn_CheckEnd(HSD_GObj* gobj)
{
    Fighter* fp = GET_FIGHTER(gobj);
    if (ftLc_X233C(fp) <= 0 && ftLc_MV(fp)->speciallw.released) {
        if (fp->ground_or_air == GA_Ground) {
            ftLc_Magnet_ToGround(gobj, ftLc_MS_SpecialLwTurn, ftLc_MF_Switch, false, true);
        } else {
            ftLc_Magnet_ToAir(gobj, ftLc_MS_SpecialAirLwTurn, ftLc_MF_Switch, false);
        }
        return;
    }
    ftLc_Magnet_EnterHold(gobj);
}

/* code+0x2E94 */
void ftLc_SpecialLwTurn_Anim(HSD_GObj* gobj)
{
    Fighter* fp = GET_FIGHTER(gobj);
    ftLucasAttributes* attrs = ftLc_Attrs(fp);
    ftLucas_MotionVars* mv = ftLc_MV(fp);

    ftLc_Magnet_CheckRelease(fp);
    if (ftLc_X233C(fp) > 0) {
        ftLc_X233C(fp)--;
    }
    mv->speciallw.turn_timer--;
    if (fp->cmd_vars[0] == 0 && (float) mv->speciallw.turn_timer < attrs->x90_MAGNET_TURN_FRAMES) {
        fp->cmd_vars[0] = 1;
        fp->facing_dir = -fp->facing_dir;
    }
    /* The disc reads the Z rotation and writes the Y rotation. */
    ftPartSetRotY(fp, 0,
                  -(float) ((180.0f / attrs->x90_MAGNET_TURN_FRAMES) * LC_DEG_TO_RAD -
                            ftPartGetRotZ(fp, 0)));
    if (mv->speciallw.turn_timer <= 0) {
        ftLc_SpecialLwTurn_CheckEnd(gobj);
    }
}

/* code+0x2FD8 */
void ftLc_SpecialLwTurn_Coll(HSD_GObj* gobj)
{
    if (!ft_80082708(gobj)) {
        ftLc_Magnet_ToAir(gobj, ftLc_MS_SpecialAirLwTurn, ftLc_MF_Switch, false);
    }
}

/* ---------------------------------------------------------------------------------------------
 * Aerial physics and landing.
 * ------------------------------------------------------------------------------------------- */

static void ftLc_Magnet_AirFall(Fighter* fp)
{
    ftLucas_MotionVars* mv = ftLc_MV(fp);
    if (mv->speciallw.gravity_delay != 0) {
        mv->speciallw.gravity_delay--;
    } else {
        ftCommon_Fall(fp, ftLc_Attrs(fp)->xA4_MAGNET_FALL_ACCEL, fp->co_attrs.terminal_velocity);
    }
    ftCommon_CalcSelfAccel_DeaccelQuickAir(fp);
}

/* code+0x3058 (Start) and code+0x3360 (End) */
void ftLc_SpecialAirLwStart_Phys(HSD_GObj* gobj)
{
    ftLc_Magnet_AirFall(GET_FIGHTER(gobj));
}

void ftLc_SpecialAirLwEnd_Phys(HSD_GObj* gobj)
{
    ftLc_Magnet_AirFall(GET_FIGHTER(gobj));
}

/* code+0x3118 (Hold, Hit, Turn) */
void ftLc_SpecialAirLwHold_Phys(HSD_GObj* gobj)
{
    ftLc_Magnet_AirFall(GET_FIGHTER(gobj));
    ftColl_8007AF10(gobj);
}

/* code+0x30B4. On landing the disc re-enters the *aerial* start (code+0x51EC does
 * ftCommon_8007D5D4 and state 364), so he stays airborne until the start animation ends. */
void ftLc_SpecialAirLwStart_Coll(HSD_GObj* gobj)
{
    if (ft_80081D0C(gobj)) {
        ftLc_Magnet_ToAir(gobj, ftLc_MS_SpecialAirLwStart, ftLc_MF_SwitchGfx, false);
    }
}

/* code+0x3188 */
void ftLc_SpecialAirLwHold_Coll(HSD_GObj* gobj)
{
    if (ft_80081D0C(gobj)) {
        ftLc_Magnet_ToGround(gobj, ftLc_MS_SpecialLwHold, ftLc_MF_SwitchGfx, true, true);
    }
}

/* code+0x32FC */
void ftLc_SpecialAirLwHit_Coll(HSD_GObj* gobj)
{
    if (ft_80081D0C(gobj)) {
        ftLc_Magnet_ToGround(gobj, ftLc_MS_SpecialLwHit, ftLc_MF_SwitchGfx, true, true);
    }
}

/* code+0x33BC */
void ftLc_SpecialAirLwEnd_Coll(HSD_GObj* gobj)
{
    if (ft_80081D0C(gobj)) {
        ftLc_Magnet_ToGround(gobj, ftLc_MS_SpecialLwEnd, ftLc_MF_SwitchGfx, false, false);
    }
}

/* code+0x3440 */
void ftLc_SpecialAirLwTurn_Coll(HSD_GObj* gobj)
{
    if (ft_80081D0C(gobj)) {
        ftLc_Magnet_ToGround(gobj, ftLc_MS_SpecialLwTurn, ftLc_MF_Switch, false, true);
    }
}
