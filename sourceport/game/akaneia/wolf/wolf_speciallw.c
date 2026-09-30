/* Akaneia's Wolf: down special, the reflector.
 *
 * Fox's reflector with Wolf's effects and a few differences, each marked "Fox:" below. Start (360
 * ground, 365 air), loop (361, 366), reflect hit (362, 367), end (363, 368) and turn (364, 369).
 * The per-state variables are Fox's (ftWolf_SpecialLwVars, same layout as ftFoxSpecialLw). */
#include "wolf.h"

#include <melee/ft/forward.h>

#include <melee/ef/efsync.h>
#include <melee/ft/fighter.h>
#include <melee/ft/ft_081B.h>
#include <melee/ft/ft_084E.h>
#include <melee/ft/ft_0892.h>
#include <melee/ft/ftanim.h>
#include <melee/ft/ftcoll.h>
#include <melee/ft/ftcommon.h>
#include <melee/ft/ftparts.h>
#include <melee/ft/inlines.h>
#include <melee/ft/kinds/ftCommon/ftCo_Fall.h>
#include <melee/ft/kinds/ftCommon/ftCo_Jump.h>
#include <melee/ft/kinds/ftCommon/ftCo_JumpAerial.h>
#include <melee/ft/kinds/ftCommon/ftCo_Pass.h>
#include <melee/ft/kinds/ftCommon/ftCo_Turn.h>
#include <melee/ft/kinds/ftCommon/types.h>
#include <melee/ft/types.h>
#include <melee/lb/lb_00B0.h>
#include <melee/lb/lb_00F9.h>
#include <sysdolphin/baselib/controller.h>

#define DEG_TO_RAD 0.017453292519943295

static inline ftWolf_SpecialLwVars* ftWf_LwVars(Fighter* fp)
{
    return &ftWf_MV(fp)->SpecialLw;
}

/* Letting go of B ends the reflector once the release lag is over. */
static inline void ftWf_SpecialLw_CheckRelease(Fighter* fp)
{
    if (!(fp->input.held_buttons[0] & HSD_PAD_B)) {
        ftWf_LwVars(fp)->is_release = true;
    }
}

/* ---- effects (accessory4_cb), all on HipN ---- */

/* [SpecialLw_GFX] */
static void ftWf_SpecialLw_StartGFX(HSD_GObj* gobj)
{
    Fighter* fp = GET_FIGHTER(gobj);

    if (!fp->x2219_b0) {
        efSync_Spawn(ftWf_Ef_ReflectorStart, gobj, fp->parts[FtPart_HipN].joint);
        fp->x2219_b0 = true;
    }
    ftWf_EffectDone(fp);
}

/* [SpecialLwLoop_GFX] */
static void ftWf_SpecialLw_LoopGFX(HSD_GObj* gobj)
{
    Fighter* fp = GET_FIGHTER(gobj);

    if (!fp->x2219_b0) {
        efSync_Spawn(ftWf_Ef_ReflectorLoop, gobj, fp->parts[FtPart_HipN].joint);
        fp->x2219_b0 = true;
    }
    ftWf_EffectDone(fp);
}

/* [SpecialLwReflect_GFX] */
static void ftWf_SpecialLw_ReflectGFX(HSD_GObj* gobj)
{
    Fighter* fp = GET_FIGHTER(gobj);

    if (!fp->x2219_b0) {
        efSync_Spawn(ftWf_Ef_ReflectorHit, gobj, fp->parts[FtPart_HipN].joint);
        fp->x2219_b0 = true;
    }
    ftWf_EffectDone(fp);
}

static void ftWf_SpecialLwHit_Enter(HSD_GObj* gobj);

/* The reflect bubble, from the attribute block. */
static void ftWf_SpecialLw_CreateReflectHit(HSD_GObj* gobj)
{
    Fighter* fp = GET_FIGHTER(gobj);

    ftColl_CreateReflectHit(gobj, DISC_REFLECT(&ftWf_Attrs(fp)->xB0_REFLECTOR_REFLECTION),
                            ftWf_SpecialLwHit_Enter);
}

/* Keep reflecting across a ground/air switch (the bubble survives the state change). */
static void ftWf_SpecialLw_KeepReflecting(Fighter* fp)
{
    fp->reflecting = true;
    fp->reflect_hit_cb = ftWf_SpecialLwHit_Enter;
}

/* ---- entering states ---- */

/* [SpecialLw]: the speciallw slot. */
void ftWf_SpecialLw_Enter(HSD_GObj* gobj)
{
    Fighter* fp = GET_FIGHTER(gobj);
    ftWolf_DatAttrs* da = ftWf_Attrs(fp);

    Fighter_ChangeMotionState(gobj, ftWf_MS_SpecialLwStart, 0, 0.0f, 1.0f, 0.0f, NULL);
    ftAnim_8006EBA4(gobj);

    ftWf_LwVars(fp)->release_lag = da->x98_REFLECTOR_RELEASE_LAG;
    ftWf_LwVars(fp)->gravity_delay = da->xA4_REFLECTOR_GRAVITY_DELAY;
    ftWf_LwVars(fp)->is_release = false;
    fp->cmd_vars[1] = 4;
    fp->accessory4_cb = ftWf_SpecialLw_StartGFX;
}

/* [SpecialAirLw]: the specialairlw slot. */
void ftWf_SpecialAirLw_Enter(HSD_GObj* gobj)
{
    Fighter* fp = GET_FIGHTER(gobj);
    ftWolf_DatAttrs* da = ftWf_Attrs(fp);

    fp->self_vel.y = 0.0f;
    fp->self_vel.x /= da->xA8_REFLECTOR_AIR_VEL_DIV;
    Fighter_ChangeMotionState(gobj, ftWf_MS_SpecialAirLwStart, 0, 0.0f, 1.0f, 0.0f, NULL);
    ftAnim_8006EBA4(gobj);

    ftWf_LwVars(fp)->release_lag = da->x98_REFLECTOR_RELEASE_LAG;
    ftWf_LwVars(fp)->is_release = false;
    fp->cmd_vars[1] = 4;
    ftWf_LwVars(fp)->gravity_delay = da->xA4_REFLECTOR_GRAVITY_DELAY;
    fp->accessory4_cb = ftWf_SpecialLw_StartGFX;
}

/* [SpecialLwLoop] */
static void ftWf_SpecialLwLoop_Enter(HSD_GObj* gobj)
{
    Fighter* fp = GET_FIGHTER(gobj);

    Fighter_ChangeMotionState(gobj, ftWf_MS_SpecialLwLoop, 0, 0.0f, 1.0f, 0.0f, NULL);
    ftWf_SpecialLw_CreateReflectHit(gobj);
    fp->accessory4_cb = ftWf_SpecialLw_LoopGFX;
}

/* [SpecialAirLwLoop] */
static void ftWf_SpecialAirLwLoop_Enter(HSD_GObj* gobj)
{
    Fighter* fp = GET_FIGHTER(gobj);

    Fighter_ChangeMotionState(gobj, ftWf_MS_SpecialAirLwLoop, 0, 0.0f, 1.0f, 0.0f, NULL);
    ftWf_SpecialLw_CreateReflectHit(gobj);
    fp->accessory4_cb = ftWf_SpecialLw_LoopGFX;
}

/* [SpecialLwTurn_Spin]: one frame of the turn. Fox's ftFx_SpecialLw_Turn without the facing flip,
 * which Wolf does when the turn starts. */
static void ftWf_SpecialLw_Spin(HSD_GObj* gobj)
{
    Fighter* fp = GET_FIGHTER(gobj);
    ftWolf_DatAttrs* da = ftWf_Attrs(fp);

    ftWf_LwVars(fp)->turn_frames--;
    ftPartSetRotY(fp, 0,
                  -(float) ((180.0f / da->x9C_REFLECTOR_TURN_FRAMES) * DEG_TO_RAD -
                            ftPartGetRotZ(fp, 0)));
}

/* [SpecialLwTurn] / [SpecialAirLwTurn]. Fox: keeps the effects (Ft_MF_KeepGfx), re-arms the loop
 * effect, and flips the facing half way through the turn; Wolf flips it at once. */
static void ftWf_SpecialLw_EnterTurn(HSD_GObj* gobj, FtMotionId msid)
{
    Fighter* fp = GET_FIGHTER(gobj);
    ftWolf_DatAttrs* da = ftWf_Attrs(fp);

    Fighter_ChangeMotionState(gobj, msid, 0, 0.0f, 1.0f, 0.0f, NULL);
    fp->reflecting = true;
    ftWf_LwVars(fp)->turn_frames = da->x9C_REFLECTOR_TURN_FRAMES;
    fp->reflect_hit_cb = ftWf_SpecialLwHit_Enter;
    fp->cmd_vars[0] = 0;
    fp->facing_dir = -fp->facing_dir;
    ftWf_SpecialLw_Spin(gobj);
}

/* [SpecialAirLwReflect], installed as reflect_hit_cb: something was reflected. */
static void ftWf_SpecialLwHit_Enter(HSD_GObj* gobj)
{
    Fighter* fp = GET_FIGHTER(gobj);
    Vec3 pos;

    fp->facing_dir = fp->ReflectAttr.x1A2C_reflectHitDirection;
    lb_8000B1CC(fp->parts[ftParts_GetBoneIndex(fp, FtPart_HipN)].joint, NULL, &pos);
    lb_800119DC(&pos, 120, 3.0f, 0.1f, 1.0472f);

    Fighter_ChangeMotionState(gobj,
                              fp->ground_or_air == GA_Air ? ftWf_MS_SpecialAirLwHit
                                                          : ftWf_MS_SpecialLwHit,
                              0, 0.0f, 1.0f, 0.0f, NULL);
    ftWf_SpecialLw_KeepReflecting(fp);
    fp->accessory4_cb = ftWf_SpecialLw_ReflectGFX;
}

/* Drop through a platform ([SpecialLw_IASA] and the loop's IASA). */
static bool ftWf_SpecialLw_CheckPass(HSD_GObj* gobj, FtMotionId msid)
{
    if (ftCo_80099F1C(gobj) == true) {
        ftCo_8009A184(gobj, msid, ftWf_MF_SpecialLw_Coll, GET_FIGHTER(gobj)->cur_anim_frame);
        ftWf_SpecialLw_CreateReflectHit(gobj);
        return true;
    }
    return false;
}

/* The ground/air switches. A grounded state leaving the ground goes airborne first
 * (ftCommon_8007D5D4); an air state landing goes to the ground (ftCommon_8007D6A4), before or after
 * the state change as m-ex orders them. */
static void ftWf_SpecialLw_ToAir(HSD_GObj* gobj, FtMotionId msid)
{
    Fighter* fp = GET_FIGHTER(gobj);

    ftCommon_8007D5D4(fp);
    Fighter_ChangeMotionState(gobj, msid, ftWf_MF_SpecialLw_Coll, fp->cur_anim_frame, 1.0f, 0.0f,
                              NULL);
}

static void ftWf_SpecialLw_ToGround(HSD_GObj* gobj, FtMotionId msid)
{
    Fighter* fp = GET_FIGHTER(gobj);

    ftCommon_8007D6A4(fp);
    Fighter_ChangeMotionState(gobj, msid, ftWf_MF_SpecialLw_Coll, fp->cur_anim_frame, 1.0f, 0.0f,
                              NULL);
}

/* ---- phys shared by the states ---- */

/* [SpecialLwLoop_Phys] */
static void ftWf_SpecialLw_GroundPhys(HSD_GObj* gobj)
{
    ft_80084F3C(gobj);
    ftColl_8007AEF8(gobj);
}

/* [SpecialAirLw_Phys] */
static void ftWf_SpecialLw_AirPhys(HSD_GObj* gobj)
{
    Fighter* fp = GET_FIGHTER(gobj);

    if (ftWf_LwVars(fp)->gravity_delay != 0) {
        ftWf_LwVars(fp)->gravity_delay--;
    } else {
        ftCommon_Fall(fp, ftWf_Attrs(fp)->xAC_REFLECTOR_FALL_ACCEL, fp->co_attrs.terminal_velocity);
    }
    ftCommon_CalcSelfAccel_DeaccelQuickAir(fp);
}

/* [SpecialAirLwLoop_Phys] */
static void ftWf_SpecialLw_AirLoopPhys(HSD_GObj* gobj)
{
    ftWf_SpecialLw_AirPhys(gobj);
    ftColl_8007AEF8(gobj);
}

/* ---- 360 / 365: start [SpecialLw] [SpecialAirLw] ---- */

/* Shared by both starts. Fox also re-arms the loop effect here; Wolf's loop entry does that. */
void ftWf_SpecialLwStart_Anim(HSD_GObj* gobj)
{
    Fighter* fp = GET_FIGHTER(gobj);

    ftWf_SpecialLw_CheckRelease(fp);
    if (!ftAnim_IsFramesRemaining(gobj)) {
        if (fp->ground_or_air != GA_Ground) {
            ftWf_SpecialAirLwLoop_Enter(gobj);
        } else {
            ftWf_SpecialLwLoop_Enter(gobj);
        }
        ftCommon_8007DB24(gobj);
    }
}

void ftWf_SpecialLwStart_IASA(HSD_GObj* gobj)
{
    ftWf_SpecialLw_CheckPass(gobj, ftWf_MS_SpecialAirLwStart);
}

void ftWf_SpecialLwStart_Phys(HSD_GObj* gobj)
{
    ft_80084F3C(gobj);
}

/* [SpecialAirLw_Trans] */
void ftWf_SpecialLwStart_Coll(HSD_GObj* gobj)
{
    if (!ft_80082708(gobj)) {
        ftWf_SpecialLw_ToAir(gobj, ftWf_MS_SpecialAirLwStart);
    }
}

void ftWf_SpecialAirLwStart_Anim(HSD_GObj* gobj)
{
    ftWf_SpecialLwStart_Anim(gobj);
}

void ftWf_SpecialAirLwStart_IASA(HSD_GObj* gobj) {}

void ftWf_SpecialAirLwStart_Phys(HSD_GObj* gobj)
{
    ftWf_SpecialLw_AirPhys(gobj);
}

/* [SpecialLw_Trans]: here the state changes before the landing. */
void ftWf_SpecialAirLwStart_Coll(HSD_GObj* gobj)
{
    Fighter* fp = GET_FIGHTER(gobj);

    if (ft_80081D0C(gobj) == 1) {
        Fighter_ChangeMotionState(gobj, ftWf_MS_SpecialLwStart, ftWf_MF_SpecialLw_Coll,
                                  fp->cur_anim_frame, 1.0f, 0.0f, NULL);
        ftCommon_8007D6A4(fp);
    }
}

/* ---- 361 / 366: loop ---- */

/* Fox: counts the release lag down only while it is positive. The end states are entered plainly
 * (m-ex has no End_Enter of its own). */
static void ftWf_SpecialLw_LoopAnim(HSD_GObj* gobj, FtMotionId end_msid)
{
    Fighter* fp = GET_FIGHTER(gobj);

    ftWf_SpecialLw_CheckRelease(fp);
    if (--ftWf_LwVars(fp)->release_lag > 0) {
        return;
    }
    if (ftWf_LwVars(fp)->is_release == true) {
        Fighter_ChangeMotionState(gobj, end_msid, 0, 0.0f, 1.0f, 0.0f, NULL);
    }
}

void ftWf_SpecialLwLoop_Anim(HSD_GObj* gobj)
{
    ftWf_SpecialLw_LoopAnim(gobj, ftWf_MS_SpecialLwEnd);
}

void ftWf_SpecialLwLoop_IASA(HSD_GObj* gobj)
{
    if (ftCo_800C97A8(gobj) == true) {
        ftWf_SpecialLw_EnterTurn(gobj, ftWf_MS_SpecialLwTurn);
        return;
    }
    if (ftCo_Jump_CheckInput(gobj) == true) {
        return;
    }
    ftWf_SpecialLw_CheckPass(gobj, ftWf_MS_SpecialAirLwLoop);
}

void ftWf_SpecialLwLoop_Phys(HSD_GObj* gobj)
{
    ftWf_SpecialLw_GroundPhys(gobj);
}

/* [SpecialAirLwLoop_Trans] */
void ftWf_SpecialLwLoop_Coll(HSD_GObj* gobj)
{
    if (!ft_80082708(gobj)) {
        ftWf_SpecialLw_ToAir(gobj, ftWf_MS_SpecialAirLwLoop);
        ftWf_SpecialLw_KeepReflecting(GET_FIGHTER(gobj));
    }
}

void ftWf_SpecialAirLwLoop_Anim(HSD_GObj* gobj)
{
    ftWf_SpecialLw_LoopAnim(gobj, ftWf_MS_SpecialAirLwEnd);
}

void ftWf_SpecialAirLwLoop_IASA(HSD_GObj* gobj)
{
    if (ftCo_800C97A8(gobj) == true) {
        ftWf_SpecialLw_EnterTurn(gobj, ftWf_MS_SpecialAirLwTurn);
        return;
    }
    ftCo_800CB870(gobj);
}

void ftWf_SpecialAirLwLoop_Phys(HSD_GObj* gobj)
{
    ftWf_SpecialLw_AirLoopPhys(gobj);
}

/* [SpecialLwLoop_Trans] */
void ftWf_SpecialAirLwLoop_Coll(HSD_GObj* gobj)
{
    if (ft_80081D0C(gobj) == 1) {
        ftWf_SpecialLw_ToGround(gobj, ftWf_MS_SpecialLwLoop);
        ftWf_SpecialLw_KeepReflecting(GET_FIGHTER(gobj));
    }
}

/* ---- 362 / 367: reflect hit [SpecialLwReflect] [SpecialAirLwReflect] ---- */

void ftWf_SpecialLwHit_Anim(HSD_GObj* gobj)
{
    ftWf_SpecialLw_CheckRelease(GET_FIGHTER(gobj));
    if (!ftAnim_IsFramesRemaining(gobj)) {
        ftWf_SpecialLwLoop_Enter(gobj);
    }
}

void ftWf_SpecialLwHit_IASA(HSD_GObj* gobj) {}

void ftWf_SpecialLwHit_Phys(HSD_GObj* gobj)
{
    ftWf_SpecialLw_GroundPhys(gobj);
}

/* [SpecialAirLwReflect_Trans]: restarts the animation (frame 0) rather than carrying it over. */
void ftWf_SpecialLwHit_Coll(HSD_GObj* gobj)
{
    Fighter* fp = GET_FIGHTER(gobj);

    if (!ft_80082708(gobj)) {
        ftCommon_8007D5D4(fp);
        Fighter_ChangeMotionState(gobj, ftWf_MS_SpecialAirLwHit, 0, 0.0f, 1.0f, 0.0f, NULL);
        ftWf_SpecialLw_KeepReflecting(fp);
        fp->accessory4_cb = ftWf_SpecialLw_ReflectGFX;
    }
}

void ftWf_SpecialAirLwHit_Anim(HSD_GObj* gobj)
{
    ftWf_SpecialLw_CheckRelease(GET_FIGHTER(gobj));
    if (!ftAnim_IsFramesRemaining(gobj)) {
        ftWf_SpecialAirLwLoop_Enter(gobj);
    }
}

void ftWf_SpecialAirLwHit_IASA(HSD_GObj* gobj) {}

void ftWf_SpecialAirLwHit_Phys(HSD_GObj* gobj)
{
    ftWf_SpecialLw_AirLoopPhys(gobj);
}

/* [SpecialLwReflect_Trans]: also from frame 0. */
void ftWf_SpecialAirLwHit_Coll(HSD_GObj* gobj)
{
    Fighter* fp = GET_FIGHTER(gobj);

    if (ft_80081D0C(gobj) == 1) {
        ftCommon_8007D6A4(fp);
        Fighter_ChangeMotionState(gobj, ftWf_MS_SpecialLwHit, ftWf_MF_SpecialLw_Coll, 0.0f, 1.0f,
                                  0.0f, NULL);
        ftWf_SpecialLw_KeepReflecting(fp);
        fp->accessory4_cb = ftWf_SpecialLw_ReflectGFX;
    }
}

/* ---- 363 / 368: end ---- */

/* Fox: ends with ftCommon_8007DB24 and ftCommon_8007D92C; Wolf goes to Wait or Fall. */
void ftWf_SpecialLwEnd_Anim(HSD_GObj* gobj)
{
    if (!ftAnim_IsFramesRemaining(gobj)) {
        ft_8008A2BC(gobj);
    }
}

void ftWf_SpecialLwEnd_IASA(HSD_GObj* gobj) {}

void ftWf_SpecialLwEnd_Phys(HSD_GObj* gobj)
{
    ft_80084F3C(gobj);
}

/* [SpecialAirLwEnd_Trans] */
void ftWf_SpecialLwEnd_Coll(HSD_GObj* gobj)
{
    if (!ft_80082708(gobj)) {
        ftWf_SpecialLw_ToAir(gobj, ftWf_MS_SpecialAirLwEnd);
    }
}

void ftWf_SpecialAirLwEnd_Anim(HSD_GObj* gobj)
{
    if (!ftAnim_IsFramesRemaining(gobj)) {
        ftCo_Fall_Enter(gobj);
    }
}

void ftWf_SpecialAirLwEnd_IASA(HSD_GObj* gobj) {}

void ftWf_SpecialAirLwEnd_Phys(HSD_GObj* gobj)
{
    ftWf_SpecialLw_AirPhys(gobj);
}

/* [SpecialLwEnd_Trans] */
void ftWf_SpecialAirLwEnd_Coll(HSD_GObj* gobj)
{
    if (ft_80081D0C(gobj) == 1) {
        ftWf_SpecialLw_ToGround(gobj, ftWf_MS_SpecialLwEnd);
    }
}

/* ---- 364 / 369: turn ---- */

/* The release lag keeps counting (without Fox's "only while positive") during the turn. */
static void ftWf_SpecialLw_TurnAnim(HSD_GObj* gobj, bool air)
{
    Fighter* fp = GET_FIGHTER(gobj);

    ftWf_SpecialLw_CheckRelease(fp);
    ftWf_LwVars(fp)->release_lag--;
    ftWf_SpecialLw_Spin(gobj);
    if (ftWf_LwVars(fp)->turn_frames <= 0) {
        if (air) {
            ftWf_SpecialAirLwLoop_Enter(gobj);
        } else {
            ftWf_SpecialLwLoop_Enter(gobj);
        }
    }
}

void ftWf_SpecialLwTurn_Anim(HSD_GObj* gobj)
{
    ftWf_SpecialLw_TurnAnim(gobj, false);
}

void ftWf_SpecialLwTurn_IASA(HSD_GObj* gobj) {}

void ftWf_SpecialLwTurn_Phys(HSD_GObj* gobj)
{
    ftWf_SpecialLw_GroundPhys(gobj);
}

/* [SpecialLwLwTurn_Coll] to [SpecialAirLwTurn_Trans] */
void ftWf_SpecialLwTurn_Coll(HSD_GObj* gobj)
{
    if (!ft_80082708(gobj)) {
        ftWf_SpecialLw_ToAir(gobj, ftWf_MS_SpecialAirLwTurn);
    }
}

void ftWf_SpecialAirLwTurn_Anim(HSD_GObj* gobj)
{
    ftWf_SpecialLw_TurnAnim(gobj, true);
}

void ftWf_SpecialAirLwTurn_IASA(HSD_GObj* gobj) {}

void ftWf_SpecialAirLwTurn_Phys(HSD_GObj* gobj)
{
    ftWf_SpecialLw_AirLoopPhys(gobj);
}

/* [SpecialLwTurn_Trans] */
void ftWf_SpecialAirLwTurn_Coll(HSD_GObj* gobj)
{
    if (ft_80081D0C(gobj) == 1) {
        ftWf_SpecialLw_ToGround(gobj, ftWf_MS_SpecialLwTurn);
    }
}
