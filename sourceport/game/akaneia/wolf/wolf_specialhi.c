/* Akaneia's Wolf: up special, Fire Wolf.
 *
 * Fox's Firefox with Wolf's numbers and effects, and a handful of real differences, each marked
 * "Fox:" below. Charge (353 ground, 354 air), travel (355 along the ground, 356 through the air),
 * then landing (357), fall (358) or the bounce off the floor (359). The per-state variables are
 * Fox's (ftWolf_SpecialHiVars, same layout as ftFoxSpecialHi). */
#include "wolf.h"

#include <melee/ft/forward.h>

#include <math.h>

#include <melee/ef/efsync.h>
#include <melee/ef/eflib.h>
#include <melee/ef/types.h>
#include <melee/ft/fighter.h>
#include <melee/ft/ft_081B.h>
#include <melee/ft/ft_084E.h>
#include <melee/ft/ft_0892.h>
#include <melee/ft/ftanim.h>
#include <melee/ft/ftcliffcommon.h>
#include <melee/ft/ftcommon.h>
#include <melee/ft/ftparts.h>
#include <melee/ft/inlines.h>
#include <melee/ft/kinds/ftCommon/ftCo_FallSpecial.h>
#include <melee/ft/kinds/ftCommon/ftCo_Pass.h>
#include <melee/ft/types.h>
#include <melee/lb/lbvector.h>
#include <melee/mp/forward.h>

#define DEG_TO_RAD 0.017453292519943295
#define TWO_PI 6.283185307179586
#define HALF_PI 1.5707963267948966

/* Fox: the stick has to pass fp->dat_attrs x64 for a directed launch; Wolf tests this literal (the
 * attribute holds the same 0.5), and a strict "greater than". */
#define FTWF_FIREWOLF_STICK_MIN 0.5f

static inline ftWolf_SpecialHiVars* ftWf_HiVars(Fighter* fp)
{
    return &ftWf_MV(fp)->SpecialHi;
}

/* Tilt the model along the travel angle (Fox's ftFox_SpecialHi_RotateModel). */
static void ftWf_SpecialHi_RotateModel(Fighter* fp)
{
    ftPartSetRotX(fp, ftParts_GetBoneIndex(fp, FtPart_XRotN),
                  (float) (TWO_PI - ftWf_HiVars(fp)->angle));
}

/* [SpecialHiHold_GFX]: the charge flame, on TransN. */
static void ftWf_SpecialHi_ChargeGFX(HSD_GObj* gobj)
{
    Fighter* fp = GET_FIGHTER(gobj);

    if (!fp->x2219_b0) {
        efSync_Spawn(ftWf_Ef_FireWolfCharge, gobj,
                     fp->parts[ftParts_GetBoneIndex(fp, FtPart_TransN)].joint);
        fp->x2219_b0 = true;
    }
    ftWf_EffectDone(fp);
}

/* [SpecialHi_GFX]: the travel flame, on HipN, turned with the fighter each frame (Fox's 1164 gets
 * the same update through efSync_Spawn's own case). */
static void ftWf_SpecialHi_LaunchGFX(HSD_GObj* gobj)
{
    Fighter* fp = GET_FIGHTER(gobj);

    if (!fp->x2219_b0) {
        EF_Effect* effect = efSync_Spawn(ftWf_Ef_FireWolfLaunch, gobj,
                                         fp->parts[ftParts_GetBoneIndex(fp, FtPart_HipN)].joint);
        fp->x2219_b0 = true;
        if (effect != NULL) {
            effect->update = efLib_Cb_SetRotYZ_FromFighter;
        }
    }
    ftWf_EffectDone(fp);
}

/* [SpecialHi]: the specialhi slot. */
void ftWf_SpecialHi_Enter(HSD_GObj* gobj)
{
    Fighter* fp = GET_FIGHTER(gobj);
    ftWolf_DatAttrs* da = ftWf_Attrs(fp);

    ftWf_HiVars(fp)->gravity_delay = da->x54_FIREWOLF_GRAVITY_DELAY;
    fp->gr_vel /= da->x58_FIREWOLF_START_VEL_DIV;
    Fighter_ChangeMotionState(gobj, ftWf_MS_SpecialHiHold, 0, 0.0f, 1.0f, 0.0f, NULL);
    ftAnim_8006EBA4(gobj);
    fp->accessory4_cb = ftWf_SpecialHi_ChargeGFX;
}

/* [SpecialAirHi]: the specialairhi slot. */
void ftWf_SpecialAirHi_Enter(HSD_GObj* gobj)
{
    Fighter* fp = GET_FIGHTER(gobj);
    ftWolf_DatAttrs* da = ftWf_Attrs(fp);

    fp->self_vel.y = 0.0f;
    fp->self_vel.x /= da->x58_FIREWOLF_START_VEL_DIV;
    ftWf_HiVars(fp)->gravity_delay = da->x54_FIREWOLF_GRAVITY_DELAY;
    Fighter_ChangeMotionState(gobj, ftWf_MS_SpecialHiHoldAir, 0, 0.0f, 1.0f, 0.0f, NULL);
    ftAnim_8006EBA4(gobj);
    fp->accessory4_cb = ftWf_SpecialHi_ChargeGFX;
}

/* [SpecialAirHiLaunch]: launch through the air (356), Fox's ftFx_SpecialAirHi_Enter. */
static void ftWf_SpecialAirHi_Launch(HSD_GObj* gobj)
{
    Fighter* fp = GET_FIGHTER(gobj);
    ftWolf_DatAttrs* da = ftWf_Attrs(fp);
    ftWolf_SpecialHiVars* sh = ftWf_HiVars(fp);
    float stick_x = stickGetDir(fp->input.lstick[0].x, 0.0f);
    float stick_y = stickGetDir(fp->input.lstick[0].y, 0.0f);

    if (stick_y + stick_x > FTWF_FIREWOLF_STICK_MIN) {
        if (stick_x > da->x88_FIREWOLF_FACING_STICK_MIN) {
            ftCommon_UpdateFacing(fp);
        }
        sh->angle = atan2f(fp->input.lstick[0].y, fp->input.lstick[0].x * fp->facing_dir);
    } else {
        sh->angle = (float) HALF_PI;
    }

    Fighter_ChangeMotionState(gobj, ftWf_MS_SpecialAirHi, 0, 0.0f, 1.0f, 0.0f, NULL);
    fp->x2223_b4 = true;
    sh->travel_frames = da->x68_FIREWOLF_DURATION;
    sh->ground_frames = 0;
    sh->launch_frames = 0;
    fp->self_vel.x = cosf(sh->angle) * da->x74_FIREWOLF_SPEED * fp->facing_dir;
    fp->self_vel.y = da->x74_FIREWOLF_SPEED * sinf(sh->angle);
    ftWf_SpecialHi_RotateModel(fp);
    fp->x1968_jumpsUsed = fp->co_attrs.max_jumps;
    /* [SpecialHi_OnSpin] is a copy of ftCommon_8007F76C, which Fox installs here. */
    fp->x21F8 = ftCommon_8007F76C;
    fp->accessory4_cb = ftWf_SpecialHi_LaunchGFX;
}

/* [SpecialHiLaunch]: the ground charge is over. Travel along the floor (355) when the stick points
 * into it and Wolf is not dropping through a platform, otherwise take off (356). Fox's
 * ftFx_SpecialAirHi_AirToGround. */
static void ftWf_SpecialHi_Launch(HSD_GObj* gobj)
{
    Fighter* fp = GET_FIGHTER(gobj);
    ftWolf_DatAttrs* da = ftWf_Attrs(fp);
    ftWolf_SpecialHiVars* sh = ftWf_HiVars(fp);
    CollData* coll = &fp->coll_data;
    float stick_x = fp->input.lstick[0].x;
    float stick_y = fp->input.lstick[0].y;

    if (stickGetDir(stick_y, 0.0f) + stickGetDir(stick_x, 0.0f) > FTWF_FIREWOLF_STICK_MIN) {
        Vec3 stick;
        stick.x = stick_x;
        stick.y = stick_y;
        stick.z = 0.0f;

        if (lbVector_AngleXY(&coll->floor.normal, &stick) >= HALF_PI && !ftCo_8009A134(gobj)) {
            ftCommon_UpdateFacing(fp);
            Fighter_ChangeMotionState(gobj, ftWf_MS_SpecialHi, 0, 0.0f, 1.0f, 0.0f, NULL);
            fp->x2223_b4 = true;
            sh->travel_frames = da->x68_FIREWOLF_DURATION;
            fp->gr_vel = da->x74_FIREWOLF_SPEED * fp->facing_dir;
            sh->ground_frames = 0;
            sh->launch_frames = 0;
            sh->angle = atan2f(-(fp->facing_dir * coll->floor.normal.x), coll->floor.normal.y);
            ftWf_SpecialHi_RotateModel(fp);
            fp->x21F8 = ftCommon_8007F76C;
            fp->accessory4_cb = ftWf_SpecialHi_LaunchGFX;
            return;
        }
    }

    ftCommon_8007D60C(fp);
    ftWf_SpecialAirHi_Launch(gobj);
}

/* [SpecialHiLanding]: the ground travel ran out. Fox: this enters the landing at frame 13 whether
 * or not Wolf is still grounded. */
static void ftWf_SpecialHi_EnterLanding(HSD_GObj* gobj)
{
    Fighter* fp = GET_FIGHTER(gobj);

    ftCommon_8007DB24(gobj);
    Fighter_ChangeMotionState(gobj, ftWf_MS_SpecialHiLanding, 0, 13.0f, 1.0f, 0.0f, NULL);
    fp->x21F8 = ftCommon_8007F76C;
}

/* [SpecialHiFall]: the air travel ran out. */
static void ftWf_SpecialHi_EnterFall(HSD_GObj* gobj)
{
    Fighter_ChangeMotionState(gobj, ftWf_MS_SpecialHiFall, 0, 0.0f, 1.0f, 0.0f, NULL);
    ftAnim_8006EBA4(gobj);
}

/* [SpecialHiLanding_Trans]: land out of the fall or the bounce. */
static void ftWf_SpecialHi_FallToLanding(HSD_GObj* gobj)
{
    Fighter* fp = GET_FIGHTER(gobj);

    Fighter_ChangeMotionState(gobj, ftWf_MS_SpecialHiLanding, Ft_MF_KeepGfx, 14.0f, 1.0f, 0.0f,
                              NULL);
    ftCommon_8007D6A4(fp);
}

/* [SpecialHiBound]: bounce off the floor (359), Fox's ftFx_SpecialHiBound_Enter with the same
 * retail effect (1030). */
static void ftWf_SpecialHi_EnterBound(HSD_GObj* gobj)
{
    Fighter* fp = GET_FIGHTER(gobj);
    ftWolf_DatAttrs* da = ftWf_Attrs(fp);
    float effect_angle;

    Fighter_ChangeMotionState(gobj, ftWf_MS_SpecialHiBound, 0, 0.0f, 1.0f, 0.0f, NULL);
    ftAnim_8006EBA4(gobj);
    fp->x21F8 = ftCommon_8007F76C;
    fp->cmd_vars[0] = 0;
    fp->self_vel.x *= da->x84_FIREWOLF_BOUND_VEL_X;

    if (fp->coll_data.env_flags & Collide_FloorMask) {
        effect_angle = -atan2f(fp->coll_data.floor.normal.x, fp->coll_data.floor.normal.y);
    } else {
        effect_angle = 0.0f;
    }
    efSync_Spawn(1030, gobj, &fp->cur_pos, &effect_angle);
    fp->x2219_b0 = true;
    Fighter_SetEffectHitlagCallbacks(fp);
}

/* Face the way Wolf is moving and tilt the model to match. */
static void ftWf_SpecialAirHi_FaceVelocity(Fighter* fp)
{
    float speed_x;

    /* Fox: zero speed faces right; Wolf's faces left. */
    if (fp->self_vel.x > 0.0f) {
        fp->facing_dir = 1.0f;
        speed_x = fp->self_vel.x;
    } else {
        fp->facing_dir = -1.0f;
        speed_x = -fp->self_vel.x;
    }
    ftWf_HiVars(fp)->angle = atan2f(fp->self_vel.y, speed_x);
    ftWf_SpecialHi_RotateModel(fp);
}

/* ---- 353: ground charge [SpecialHi] ---- */

void ftWf_SpecialHiHold_Anim(HSD_GObj* gobj)
{
    if (!ftAnim_IsFramesRemaining(gobj)) {
        ftWf_SpecialHi_Launch(gobj);
    }
}

void ftWf_SpecialHiHold_IASA(HSD_GObj* gobj) {}

void ftWf_SpecialHiHold_Phys(HSD_GObj* gobj)
{
    ft_80084F3C(gobj);
}

/* [SpecialAirHi_Trans]. Fox also re-arms the charge effect here; Wolf does not. */
void ftWf_SpecialHiHold_Coll(HSD_GObj* gobj)
{
    Fighter* fp = GET_FIGHTER(gobj);

    if (!ft_80082708(gobj)) {
        ftCommon_8007D60C(fp);
        Fighter_ChangeMotionState(gobj, ftWf_MS_SpecialHiHoldAir, Ft_MF_KeepGfx,
                                  fp->cur_anim_frame, 1.0f, 0.0f, NULL);
    }
}

/* ---- 354: air charge [SpecialAirHi] ---- */

void ftWf_SpecialHiHoldAir_Anim(HSD_GObj* gobj)
{
    if (!ftAnim_IsFramesRemaining(gobj)) {
        ftWf_SpecialAirHi_Launch(gobj);
    }
}

void ftWf_SpecialHiHoldAir_IASA(HSD_GObj* gobj) {}

void ftWf_SpecialHiHoldAir_Phys(HSD_GObj* gobj)
{
    Fighter* fp = GET_FIGHTER(gobj);
    ftWolf_DatAttrs* da = ftWf_Attrs(fp);

    if (ftWf_HiVars(fp)->gravity_delay > 0) {
        ftWf_HiVars(fp)->gravity_delay--;
    } else {
        ftCommon_Fall(fp, da->x60_FIREWOLF_FALL_ACCEL, fp->co_attrs.terminal_velocity);
    }
    ftCommon_CalcSelfAccel_Deaccel(fp, da->x5C_FIREWOLF_CHARGE_DECEL_X);
}

/* [SpecialHi_Trans] on landing. */
void ftWf_SpecialHiHoldAir_Coll(HSD_GObj* gobj)
{
    Fighter* fp = GET_FIGHTER(gobj);

    if (ft_CheckGroundAndLedge(gobj, ftGetFacingDirInt(fp))) {
        Fighter_ChangeMotionState(gobj, ftWf_MS_SpecialHiHold, Ft_MF_KeepGfx, fp->cur_anim_frame,
                                  1.0f, 0.0f, NULL);
        ftCommon_8007D6A4(fp);
        return;
    }
    ftCliffCommon_80081298(gobj);
}

/* ---- 355: travel along the ground [SpecialHiLaunch] ---- */

/* Fox: the travel's end checks whether Fox is airborne; Wolf's always lands. */
void ftWf_SpecialHi_Anim(HSD_GObj* gobj)
{
    Fighter* fp = GET_FIGHTER(gobj);

    if (--ftWf_HiVars(fp)->travel_frames <= 0) {
        ftWf_SpecialHi_EnterLanding(gobj);
    }
}

void ftWf_SpecialHi_IASA(HSD_GObj* gobj) {}

void ftWf_SpecialHi_Phys(HSD_GObj* gobj)
{
    Fighter* fp = GET_FIGHTER(gobj);
    ftWolf_DatAttrs* da = ftWf_Attrs(fp);

    ftWf_HiVars(fp)->launch_frames++;
    if (ftWf_HiVars(fp)->launch_frames >= da->x70_FIREWOLF_DECEL_START) {
        ftCommon_CalcGroundAccel_Deaccel(fp, da->x78_FIREWOLF_DECEL);
    }
    ftCommon_SetSelfMovementFromGroundedMovement(gobj);
}

/* Follow the slope; leaving the ground continues in the air ([SpecialAirHiLaunch_Trans]). Fox also
 * sets x2223_b4 and the launch effect on that switch; Wolf does not. */
void ftWf_SpecialHi_Coll(HSD_GObj* gobj)
{
    Fighter* fp = GET_FIGHTER(gobj);
    CollData* coll = &fp->coll_data;

    ftWf_HiVars(fp)->ground_frames++;

    if (!ft_80082708(gobj)) {
        ftCommon_8007D60C(fp);
        Fighter_ChangeMotionState(gobj, ftWf_MS_SpecialAirHi, Ft_MF_KeepGfx, fp->cur_anim_frame,
                                  1.0f, 0.0f, NULL);
        return;
    }

    if (coll->env_flags & Collide_FloorMask) {
        ftWf_HiVars(fp)->angle =
            atan2f(-(coll->floor.normal.x * fp->facing_dir), coll->floor.normal.y);
        ftWf_SpecialHi_RotateModel(fp);
    }
}

/* ---- 356: travel through the air [SpecialAirHiLaunch] ---- */

void ftWf_SpecialAirHi_Anim(HSD_GObj* gobj)
{
    Fighter* fp = GET_FIGHTER(gobj);

    if (--ftWf_HiVars(fp)->travel_frames <= 0) {
        ftWf_SpecialHi_EnterFall(gobj);
    }
}

void ftWf_SpecialAirHi_IASA(HSD_GObj* gobj) {}

void ftWf_SpecialAirHi_Phys(HSD_GObj* gobj)
{
    Fighter* fp = GET_FIGHTER(gobj);
    ftWolf_DatAttrs* da = ftWf_Attrs(fp);
    ftWolf_SpecialHiVars* sh = ftWf_HiVars(fp);

    sh->launch_frames++;
    if (sh->launch_frames >= da->x70_FIREWOLF_DECEL_START) {
        fp->self_vel.x += -(cosf(sh->angle) * da->x78_FIREWOLF_DECEL) * fp->facing_dir;
        fp->self_vel.y += -sinf(sh->angle) * da->x78_FIREWOLF_DECEL;
    }
}

/* Hitting the floor bounces, hitting a wall or the ceiling at a glancing angle slides along it.
 * Fox: Fox bounces when the frames spent on the ground reach x6C or when he is not dropping through
 * a platform; Wolf bounces only while that count is under x6C and he is not dropping through. The
 * count only grows during the ground travel. And after sliding along the floor Wolf also runs the
 * ledge and wall checks, where Fox stops. */
void ftWf_SpecialAirHi_Coll(HSD_GObj* gobj)
{
    Fighter* fp = GET_FIGHTER(gobj);
    ftWolf_DatAttrs* da = ftWf_Attrs(fp);
    CollData* coll = &fp->coll_data;
    Vec3* normal;
    s32 env;

    if (ft_CheckGroundAndLedge(gobj, CLIFFCATCH_BOTH) &&
        ftWf_HiVars(fp)->ground_frames < da->x6C_FIREWOLF_BOUND_FRAMES && !ftCo_8009A134(gobj))
    {
        if (!(coll->env_flags & Collide_FloorMask) ||
            lbVector_AngleXY(&coll->floor.normal, &fp->self_vel) >=
                DEG_TO_RAD * (90.0f + da->x94_FIREWOLF_BOUND_ANGLE))
        {
            ftWf_SpecialHi_EnterBound(gobj);
            return;
        }
        ftWf_SpecialAirHi_FaceVelocity(fp);
    }

    if (ftCliffCommon_80081298(gobj) == true) {
        return;
    }

    env = coll->env_flags;
    if (env & Collide_CeilingMask) {
        normal = &coll->ceiling.normal;
    } else if (env & Collide_LeftWallMask) {
        normal = &coll->left_facing_wall.normal;
    } else if (env & Collide_RightWallMask) {
        normal = &coll->right_facing_wall.normal;
    } else {
        return;
    }

    if (lbVector_AngleXY(normal, &fp->self_vel) < DEG_TO_RAD * (90.0f + da->x94_FIREWOLF_BOUND_ANGLE)) {
        ftWf_SpecialAirHi_FaceVelocity(fp);
    }
}

/* ---- 357: landing ---- */

void ftWf_SpecialHiLanding_Anim(HSD_GObj* gobj)
{
    if (!ftAnim_IsFramesRemaining(gobj)) {
        ft_8008A2BC(gobj);
    }
}

void ftWf_SpecialHiLanding_IASA(HSD_GObj* gobj) {}

void ftWf_SpecialHiLanding_Phys(HSD_GObj* gobj)
{
    Fighter* fp = GET_FIGHTER(gobj);

    ftCommon_CalcGroundAccel_Deaccel(fp, ftWf_Attrs(fp)->x7C_FIREWOLF_LANDING_DECEL);
    ftCommon_SetSelfMovementFromGroundedMovement(gobj);
}

void ftWf_SpecialHiLanding_Coll(HSD_GObj* gobj)
{
    Fighter* fp = GET_FIGHTER(gobj);
    ftWolf_DatAttrs* da = ftWf_Attrs(fp);

    if (!ft_80082708(gobj)) {
        ftCo_80096900(gobj, 1, 0, true, da->x8C_FIREWOLF_FREEFALL_MOBILITY,
                      da->x90_FIREWOLF_LANDING_LAG);
    }
}

/* ---- 358: fall ---- */

/* Fox passes (1, 0, true) to ftCo_80096900; Wolf passes (1, 1, false), i.e. a different second
 * int and allow_interrupt off. What the second int changes in the special fall is not traced. */
void ftWf_SpecialHiFall_Anim(HSD_GObj* gobj)
{
    Fighter* fp = GET_FIGHTER(gobj);
    ftWolf_DatAttrs* da = ftWf_Attrs(fp);

    if (!ftAnim_IsFramesRemaining(gobj)) {
        ftCo_80096900(gobj, 1, 1, false, da->x8C_FIREWOLF_FREEFALL_MOBILITY,
                      da->x90_FIREWOLF_LANDING_LAG);
    }
}

void ftWf_SpecialHiFall_IASA(HSD_GObj* gobj) {}

void ftWf_SpecialHiFall_Phys(HSD_GObj* gobj)
{
    ft_80084DB0(gobj);
}

void ftWf_SpecialHiFall_Coll(HSD_GObj* gobj)
{
    if (ft_CheckGroundAndLedge(gobj, CLIFFCATCH_BOTH)) {
        ftWf_SpecialHi_FallToLanding(gobj);
        return;
    }
    ftCliffCommon_80081298(gobj);
}

/* ---- 359: bounce ---- */

/* Fox's bounce can end early on cmd_vars[0] and lands into Wait; Wolf's is the fall's. */
void ftWf_SpecialHiBound_Anim(HSD_GObj* gobj)
{
    ftWf_SpecialHiFall_Anim(gobj);
}

void ftWf_SpecialHiBound_IASA(HSD_GObj* gobj) {}

/* Fox: only while airborne; Wolf's bounce is an air state throughout. */
void ftWf_SpecialHiBound_Phys(HSD_GObj* gobj)
{
    Fighter* fp = GET_FIGHTER(gobj);

    ft_800851C0(gobj);
    ftCommon_CalcSelfAccel_DeaccelQuickAir(fp);
}

void ftWf_SpecialHiBound_Coll(HSD_GObj* gobj)
{
    Fighter* fp = GET_FIGHTER(gobj);

    if (ft_CheckGroundAndLedge(gobj, ftGetFacingDirInt(fp))) {
        ftWf_SpecialHi_FallToLanding(gobj);
        return;
    }
    ftCliffCommon_80081298(gobj);
}
