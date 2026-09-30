/* Akaneia's Diddy Kong, Special Hi: Rocketbarrel Boost.
 *
 * Start (the script's cmd_vars[0] lets a hard stick turn Diddy around), then Charge while B is
 * held (at least specialhi_charge_min frames, at most specialhi_charge_max). During the charge
 * the stick tilts the barrels, blending the tilted charge animations in. The launch (AirHiJump)
 * speed and hitbox scale with the charge, its angle with the tilt. In flight Diddy steers: the
 * body (XRotN) turns toward the velocity, the stick drifts. While fast and rising, the barrels
 * trail effects and a second hitbox scales with speed; once slow or falling it is gone for good.
 * Near the end the script (cmd_vars[2]) starts leveling the body out. Hitting a ceiling bounces
 * Diddy off with a quake, 5% damage and a tumble (AirHiDamage). */
#include "ftdiddy.h"

#include <math.h>

#include <melee/cm/camera.h>
#include <melee/ef/efasync.h>
#include <melee/ef/efsync.h>
#include <melee/ft/fighter.h>
#include <melee/ft/ft_081B.h>
#include <melee/ft/ft_084E.h>
#include <melee/ft/ftanim.h>
#include <melee/ft/ftcliffcommon.h>
#include <melee/ft/ftcoll.h>
#include <melee/ft/ftcommon.h>
#include <melee/ft/ftparts.h>
#include <melee/ft/inlines.h>
#include <melee/ft/kinds/ftCommon/ftCo_FallSpecial.h>
#include <melee/ft/kinds/ftCommon/ftCo_Landing.h>
#include <melee/lb/lb_00B0.h>
#include <melee/lb/lbvector.h>
#include <melee/mp/forward.h>
#include <sysdolphin/baselib/controller.h>
#include <sysdolphin/baselib/jobj.h>
#include <sysdolphin/baselib/psstructs.h>

#define DD_PI 3.141592653589793
#define DD_TAU 6.283185307179586

/* The body joint the flight rotates (ftPartSetRotX index 2, XRotN). */
#define ftDd_SpecialHi_BodyPart 2

/* SpecialAirCharge_Damaged: an empty take-damage callback the charge states install, so any
 * callback a previous state left behind does not run. */
static void ftDd_SpecialHi_ChargeDamaged(HSD_GObj* gobj) {}

/* SpecialHi_Init */
static void ftDd_SpecialHi_Init(HSD_GObj* gobj)
{
    Fighter* fp = GET_FIGHTER(gobj);

    ftDd_MV(fp)->specialhi.blend = 0.0F;
    ftDd_MV(fp)->specialhi.tilt = 0.0F;
    ftDd_MV(fp)->specialhi.charge = 0;
    ftDd_MV(fp)->specialhi.leveling = 0;
    fp->cmd_vars[0] = 0;
    fp->cmd_vars[1] = 0;
    fp->self_vel.x = 0.0F;
    fp->self_vel.y = 0.0F;
}

/* SpecialHi_OnLand */
static void ftDd_SpecialHi_OnLand(HSD_GObj* gobj)
{
    Fighter* fp = GET_FIGHTER(gobj);
    ftDd_DatAttrs* da = fp->dat_attrs_backup;

    ftCo_LandingFallSpecial_Enter(gobj, false, da->specialhi_fall_landing_lag);
}

/* SpecialAirJump_ApplyDrift */
static void ftDd_SpecialHi_ApplyDrift(HSD_GObj* gobj, float accel, float max, float friction)
{
    Fighter* fp = GET_FIGHTER(gobj);
    float stick = fp->input.lstick[0].x;

    ftCommon_CalcSelfAccel_AccelToVelClampedFrom(fp, fp->self_vel.x, stick * accel, stick * max,
                                                 friction);
}

/* Blend_ChargeAnimation: ease the tilt toward the stick, then blend the forward or backward
 * tilted charge animation (relative to facing) over the neutral one by |tilt|, the weight
 * itself eased (averaged with last frame's). */
static void ftDd_SpecialHi_BlendCharge(HSD_GObj* gobj, enum_t neutral, enum_t forward,
                                       enum_t backward)
{
    Fighter* fp = GET_FIGHTER(gobj);
    ftDd_DatAttrs* da = fp->dat_attrs_backup;
    ftDd_MotionVars* mv = ftDd_MV(fp);
    float stick = fp->input.lstick[0].x;
    enum_t anim = neutral;
    float amount;

    if (mv->specialhi.tilt < stick) {
        mv->specialhi.tilt += da->specialhi_tilt_step;
    }
    if (mv->specialhi.tilt > stick) {
        mv->specialhi.tilt -= da->specialhi_tilt_step;
    }
    if (mv->specialhi.tilt > da->specialhi_tilt_max) {
        mv->specialhi.tilt = da->specialhi_tilt_max;
    } else if (mv->specialhi.tilt < -da->specialhi_tilt_max) {
        mv->specialhi.tilt = -da->specialhi_tilt_max;
    }

    if (fp->facing_dir == 1.0F) {
        if (mv->specialhi.tilt > 0.0F) {
            anim = forward;
        }
        if (mv->specialhi.tilt < 0.0F) {
            anim = backward;
        }
    } else if (fp->facing_dir == -1.0F) {
        if (mv->specialhi.tilt < 0.0F) {
            anim = forward;
        }
        if (mv->specialhi.tilt > 0.0F) {
            anim = backward;
        }
    }

    amount = mv->specialhi.tilt < 0.0F ? -mv->specialhi.tilt : mv->specialhi.tilt;
    mv->specialhi.blend = (amount + mv->specialhi.blend) * 0.5F;
    if (mv->specialhi.blend == 0.0F) {
        return;
    }
    ftAnim_8006EDD0(fp, anim, fp->cur_anim_frame, 1.0F);
    HSD_JObjAnimAll(fp->x8AC_animSkeleton);
    if (mv->specialhi.blend == 1.0F) {
        ftAnim_8006FF74(fp, 1);
    } else {
        ftAnim_8006FE9C(fp, 1, mv->specialhi.blend, 1.0F - mv->specialhi.blend);
    }
}

/* ------------------------------------------------------------------------------------------ */
/* Entries                                                                                    */
/* ------------------------------------------------------------------------------------------ */

/* SpecialHi_Enter */
void ftDd_SpecialHi_Enter(HSD_GObj* gobj)
{
    ftDd_SpecialHi_Init(gobj);
    Fighter_ChangeMotionState(gobj, ftDd_MS_SpecialHiStart, Ft_MF_None, 0.0F, 1.0F, 0.0F, NULL);
    ftAnim_8006EBA4(gobj);
}

/* SpecialAirHi_Enter */
void ftDd_SpecialAirHi_Enter(HSD_GObj* gobj)
{
    ftDd_SpecialHi_Init(gobj);
    Fighter_ChangeMotionState(gobj, ftDd_MS_SpecialAirHiStart, Ft_MF_None, 0.0F, 1.0F, 0.0F,
                              NULL);
    ftAnim_8006EBA4(gobj);
}

static void ftDd_SpecialHiCharge_Enter(HSD_GObj* gobj, FtMotionId msid)
{
    Fighter* fp = GET_FIGHTER(gobj);

    Fighter_ChangeMotionState(gobj, msid, Ft_MF_None, 0.0F, 1.0F, 0.0F, NULL);
    fp->take_dmg_cb = ftDd_SpecialHi_ChargeDamaged;
}

/* SpecialAirHiJump_Enter: the launch. */
static void ftDd_SpecialAirHiJump_Enter(HSD_GObj* gobj)
{
    Fighter* fp = GET_FIGHTER(gobj);
    ftDd_DatAttrs* da = fp->dat_attrs_backup;
    float t, speed, angle;

    fp->cmd_vars[2] = 0;
    ftDd_MV(fp)->specialhi.gfx_timer = 0;
    ftDd_MV(fp)->specialhi.fly_hit_off = 0;
    Fighter_ChangeMotionState(gobj, ftDd_MS_SpecialAirHiJump, Ft_MF_None, 0.0F, 1.0F, 0.0F,
                              NULL);

    t = (float) ftDd_MV(fp)->specialhi.charge / da->specialhi_charge_max;
    if (t > 1.0F) {
        t = 1.0F;
    } else if (t < 0.0F) {
        t = 0.0F;
    }
    speed = (da->specialhi_speed_max - da->specialhi_speed_min) * t + da->specialhi_speed_min;
    angle = 90.0F - (90.0F - da->specialhi_angle_min) * ftDd_MV(fp)->specialhi.tilt;
    angle = angle * (DD_PI / 180.0);
    fp->self_vel.y = sinf(angle) * speed;
    fp->self_vel.x = cosf(angle) * speed;

    if (fp->x914[0].state != HitCapsule_Disabled) {
        HitCapsule* hit = &fp->x914[0];
        float dmg = (da->specialhi_launch_dmg_max - da->specialhi_launch_dmg_min) * t +
                    da->specialhi_launch_dmg_min;
        float bkb = (da->specialhi_launch_bkb_max - da->specialhi_launch_bkb_min) * t +
                    da->specialhi_launch_bkb_min;
        float kbg = (da->specialhi_launch_kbg_max - da->specialhi_launch_kbg_min) * t +
                    da->specialhi_launch_kbg_min;

        ftColl_8007ABD0(hit, (int) dmg, gobj);
        hit->x2C = (int) bkb;
        hit->x24 = (int) kbg;
    }
    fp->x1968_jumpsUsed = fp->co_attrs.max_jumps;
}

/* SpecialAirHiDamage_Enter */
static void ftDd_SpecialAirHiDamage_Enter(HSD_GObj* gobj)
{
    Fighter* fp = GET_FIGHTER(gobj);

    Fighter_ChangeMotionState(gobj, ftDd_MS_SpecialAirHiDamage, Ft_MF_None, 0.0F, 1.0F, 0.0F,
                              NULL);
    ftAnim_8006EBA4(gobj);
    ftCommon_8007D5D4(fp);
}

/* The ground/air swaps keep the frame (flags 0x4002). */
static void ftDd_SpecialHi_Swap(HSD_GObj* gobj, FtMotionId msid, bool to_air, bool charging)
{
    Fighter* fp = GET_FIGHTER(gobj);

    Fighter_ChangeMotionState(gobj, msid, ftDd_MF_Swap, fp->cur_anim_frame, fp->frame_speed_mul,
                              fp->x8A4_animBlendFrames, NULL);
    if (to_air) {
        ftCommon_8007D5D4(fp);
    } else {
        ftCommon_8007D6A4(fp);
    }
    if (charging) {
        fp->take_dmg_cb = ftDd_SpecialHi_ChargeDamaged;
    }
}

/* ------------------------------------------------------------------------------------------ */
/* Start and charge                                                                           */
/* ------------------------------------------------------------------------------------------ */

void ftDd_SpecialHiStart_Anim(HSD_GObj* gobj)
{
    if (!ftAnim_IsFramesRemaining(gobj)) {
        ftDd_SpecialHiCharge_Enter(gobj, ftDd_MS_SpecialHiCharge);
    }
}

/* On the script's cue, a hard enough stick turns Diddy around. */
void ftDd_SpecialHiStart_IASA(HSD_GObj* gobj)
{
    Fighter* fp = GET_FIGHTER(gobj);
    ftDd_DatAttrs* da = fp->dat_attrs_backup;
    float stick;

    if (fp->cmd_vars[0] == 0) {
        return;
    }
    fp->cmd_vars[0] = 0;
    stick = fp->input.lstick[0].x;
    if (stick < 0.0F) {
        stick = -stick;
    }
    if (!(da->specialhi_turn_stick > stick)) {
        ftCommon_UpdateFacing(fp);
        ftPartSetRotY(fp, 0, fp->facing_dir * (DD_PI / 2));
    }
}

void ftDd_SpecialHiStart_Phys(HSD_GObj* gobj)
{
    ft_80084F3C(gobj);
}

void ftDd_SpecialHiStart_Coll(HSD_GObj* gobj)
{
    if (!ft_80082708(gobj)) {
        ftDd_SpecialHi_Swap(gobj, ftDd_MS_SpecialAirHiStart, true, false);
    }
}

void ftDd_SpecialHiCharge_Anim(HSD_GObj* gobj)
{
    Fighter* fp = GET_FIGHTER(gobj);

    if (!ftAnim_IsFramesRemaining(gobj)) {
        ftCommon_8007D5D4(fp);
        ftDd_SpecialAirHiJump_Enter(gobj);
    }
    ftDd_SpecialHi_BlendCharge(gobj, ftDd_SM_SpecialHiCharge, ftDd_SM_SpecialHiChargeF,
                               ftDd_SM_SpecialHiChargeB);
}

/* Charge up to the minimum no matter what, then while B is held up to the maximum. */
static bool ftDd_SpecialHi_KeepCharging(Fighter* fp)
{
    ftDd_DatAttrs* da = fp->dat_attrs_backup;
    float charge = ftDd_MV(fp)->specialhi.charge;

    if (charge < da->specialhi_charge_min ||
        ((fp->input.held_buttons[0] & HSD_PAD_B) && charge < da->specialhi_charge_max))
    {
        ftDd_MV(fp)->specialhi.charge++;
        return true;
    }
    return false;
}

void ftDd_SpecialHiCharge_IASA(HSD_GObj* gobj)
{
    Fighter* fp = GET_FIGHTER(gobj);

    if (!ftDd_SpecialHi_KeepCharging(fp)) {
        ftCommon_8007D5D4(fp);
        ftDd_SpecialAirHiJump_Enter(gobj);
    }
}

void ftDd_SpecialHiCharge_Phys(HSD_GObj* gobj)
{
    ft_80084F3C(gobj);
}

void ftDd_SpecialHiCharge_Coll(HSD_GObj* gobj)
{
    if (!ft_80082708(gobj)) {
        ftDd_SpecialHi_Swap(gobj, ftDd_MS_SpecialAirHiCharge, true, true);
    }
}

void ftDd_SpecialAirHiStart_Anim(HSD_GObj* gobj)
{
    if (!ftAnim_IsFramesRemaining(gobj)) {
        ftDd_SpecialHiCharge_Enter(gobj, ftDd_MS_SpecialAirHiCharge);
    }
}

void ftDd_SpecialAirHiStart_IASA(HSD_GObj* gobj)
{
    ftDd_SpecialHiStart_IASA(gobj);
}

void ftDd_SpecialAirHiStart_Phys(HSD_GObj* gobj)
{
    Fighter* fp = GET_FIGHTER(gobj);
    ftDd_DatAttrs* da = fp->dat_attrs_backup;

    ftCommon_Fall(fp, da->specialhi_air_start_gravity, da->specialhi_air_start_terminal_vel);
}

void ftDd_SpecialAirHiStart_Coll(HSD_GObj* gobj)
{
    if (ft_80081D0C(gobj)) {
        ftDd_SpecialHi_Swap(gobj, ftDd_MS_SpecialHiStart, false, false);
    }
}

void ftDd_SpecialAirHiCharge_Anim(HSD_GObj* gobj)
{
    if (!ftAnim_IsFramesRemaining(gobj)) {
        ftDd_SpecialAirHiJump_Enter(gobj);
    }
    ftDd_SpecialHi_BlendCharge(gobj, ftDd_SM_SpecialAirHiCharge, ftDd_SM_SpecialAirHiChargeF,
                               ftDd_SM_SpecialAirHiChargeB);
}

void ftDd_SpecialAirHiCharge_IASA(HSD_GObj* gobj)
{
    Fighter* fp = GET_FIGHTER(gobj);

    if (!ftDd_SpecialHi_KeepCharging(fp)) {
        ftDd_SpecialAirHiJump_Enter(gobj);
    }
}

void ftDd_SpecialAirHiCharge_Phys(HSD_GObj* gobj)
{
    ftDd_SpecialAirHiStart_Phys(gobj);
}

void ftDd_SpecialAirHiCharge_Coll(HSD_GObj* gobj)
{
    if (ft_80081D0C(gobj)) {
        ftDd_SpecialHi_Swap(gobj, ftDd_MS_SpecialHiCharge, false, true);
    }
}

/* ------------------------------------------------------------------------------------------ */
/* Flight                                                                                     */
/* ------------------------------------------------------------------------------------------ */

/* The script's cmd_vars[2] starts the leveling out over the rest of the animation. */
void ftDd_SpecialAirHiJump_Anim(HSD_GObj* gobj)
{
    Fighter* fp = GET_FIGHTER(gobj);
    ftDd_DatAttrs* da = fp->dat_attrs_backup;
    ftDd_MotionVars* mv = ftDd_MV(fp);

    if (fp->cmd_vars[2] != 0) {
        fp->cmd_vars[2] = 0;
        mv->specialhi.leveling = 1;
        mv->specialhi.level_frame = 0.0F;
        mv->specialhi.level_frames = ftDd_FV(fp)->hi_jump_frames - fp->cur_anim_frame;
        mv->specialhi.level_start_rot = ftPartGetRotX(fp, ftDd_SpecialHi_BodyPart);
    }
    if (!ftAnim_IsFramesRemaining(gobj)) {
        ftCo_80096900(gobj, 0, 1, false, da->specialhi_fall_mobility,
                      da->specialhi_fall_landing_lag);
    }
}

void ftDd_SpecialAirHiJump_IASA(HSD_GObj* gobj) {}

/* Turn the body toward the velocity, at most specialhi_turn_max a frame. */
static void ftDd_SpecialHi_Steer(Fighter* fp, ftDd_DatAttrs* da)
{
    float target = fp->facing_dir * atan2f(fp->self_vel.x, fp->self_vel.y);
    float cur;
    float delta;
    float rot;

    if (target < 0.0F) {
        if (-target < 0.001) {
            target = 0.0F;
        } else {
            do {
                target = target + DD_TAU;
            } while (target < 0.0F);
        }
    } else if (target < 0.001) {
        target = 0.0F;
    }

    cur = ftPartGetRotX(fp, ftDd_SpecialHi_BodyPart);
    if (cur + DD_PI < target) {
        target = -target;
    }
    delta = target - cur;
    if (delta > 0.0F) {
        if (delta > da->specialhi_turn_max) {
            delta = da->specialhi_turn_max;
        }
    } else if (-da->specialhi_turn_max > delta) {
        delta = -da->specialhi_turn_max;
    }

    rot = delta + cur;
    while (!(rot < DD_TAU)) {
        rot = rot - DD_TAU;
    }
    while (rot < 0.0F) {
        rot = rot + DD_TAU;
    }
    ftPartSetRotX(fp, ftDd_SpecialHi_BodyPart, rot);
}

void ftDd_SpecialAirHiJump_Phys(HSD_GObj* gobj)
{
    Fighter* fp = GET_FIGHTER(gobj);
    ftDd_DatAttrs* da = fp->dat_attrs_backup;
    ftDd_MotionVars* mv = ftDd_MV(fp);
    float speed;
    float slow;

    ftCommon_Fall(fp, fp->co_attrs.gravity, fp->co_attrs.terminal_velocity);
    ftDd_SpecialHi_ApplyDrift(gobj, da->specialhi_drift_accel, da->specialhi_drift_max,
                              da->specialhi_drift_friction);

    if (mv->specialhi.leveling) {
        float target = da->specialhi_level_angle;
        float from = mv->specialhi.level_start_rot;
        float diff;

        mv->specialhi.level_frame += 1.0F;
        if (from < 0.0F && from > -DD_PI) {
            target = -target;
        }
        diff = from - target;
        ftPartSetRotX(fp, ftDd_SpecialHi_BodyPart,
                      diff - mv->specialhi.level_frame / mv->specialhi.level_frames * diff);
        return;
    }

    ftDd_SpecialHi_Steer(fp, da);

    /* Squared in double, as the console's fmul/fadd do, then one rounding. */
    speed = sqrtf((float) ((double) fp->self_vel.x * fp->self_vel.x +
                           (double) fp->self_vel.y * fp->self_vel.y));
    slow = da->specialhi_speed_min * 0.3;
    if (speed > slow && mv->specialhi.fly_hit_off == 0 && fp->self_vel.y > 0.0F) {
        mv->specialhi.gfx_timer++;
        if (mv->specialhi.gfx_timer % da->specialhi_gfx_interval == 0) {
            HSD_JObj* jobj = fp->parts[da->specialhi_gfx_bone].joint;
            Vec3 offset;
            Vec3 pos;

            mv->specialhi.gfx_timer = 0;
            offset = BE_VEC3(da->specialhi_gfx_offset0);
            lb_8000B1CC(jobj, &offset, &pos);
            efSync_Spawn(da->specialhi_gfx_id, gobj, &pos);
            offset = BE_VEC3(da->specialhi_gfx_offset1);
            lb_8000B1CC(jobj, &offset, &pos);
            efSync_Spawn(da->specialhi_gfx_id, gobj, &pos);
        }
        if (fp->x914[1].state != HitCapsule_Disabled) {
            HitCapsule* hit = &fp->x914[1];
            float t = (speed - slow) / (da->specialhi_speed_max - slow);
            float bkb = (da->specialhi_fly_bkb_max - da->specialhi_fly_bkb_min) * t +
                        da->specialhi_fly_bkb_min;
            float dmg = (da->specialhi_fly_dmg_max - da->specialhi_fly_dmg_min) * t +
                        da->specialhi_fly_dmg_min;

            ftColl_8007ABD0(hit, (int) dmg, gobj);
            hit->x2C = (int) bkb;
        }
    } else {
        ftColl_8007AFF8(gobj);
        mv->specialhi.fly_hit_off = 1;
    }
}

/* Landing ends it; a ceiling bounces Diddy off it, 20% slower, into the tumble. */
void ftDd_SpecialAirHiJump_Coll(HSD_GObj* gobj)
{
    Fighter* fp = GET_FIGHTER(gobj);
    CollData* cd = &fp->coll_data;
    Vec3 normal;
    Vec3 pos;
    float angle;
    HSD_Generator* gen;

    if (ft_CheckGroundAndLedge(gobj, 0)) {
        ftDd_SpecialHi_OnLand(gobj);
        return;
    }
    if (ftCliffCommon_80081298(gobj) || !(cd->env_flags & Collide_CeilingMask)) {
        return;
    }

    normal = cd->ceiling.normal;
    lbVector_Mirror(&fp->self_vel, &normal);
    fp->self_vel.x = fp->self_vel.x * 0.8;
    fp->self_vel.y = fp->self_vel.y * 0.8;

    /* The console takes the height from coll_data+0xA0 (fp+790). */
    pos.x = fp->cur_pos.x;
    pos.y = fp->cur_pos.y + cd->desired_ecb.left.y;
    pos.z = 0.0F;

    angle = atan2f(-cd->ceiling.normal.x, cd->ceiling.normal.y);
    efAsync_Spawn(gobj, &fp->x60C, 5, 0x406, NULL, &pos, &angle);
    angle = atan2f(cd->ceiling.normal.x, cd->ceiling.normal.y);
    gen = efSync_Spawn(0xEA, gobj, &pos);
    if (gen != NULL) {
        gen->angle = angle;
    }
    Camera_RequestQuake(QuakeKind_Small, &pos);
    ftCommon_8007EBAC(fp, 7, 0);
    Fighter_TakeDamage_8006CC7C(fp, 5.0F);
    ftDd_SpecialAirHiDamage_Enter(gobj);
}

void ftDd_SpecialAirHiDamage_Anim(HSD_GObj* gobj)
{
    Fighter* fp = GET_FIGHTER(gobj);
    ftDd_DatAttrs* da = fp->dat_attrs_backup;

    if (!ftAnim_IsFramesRemaining(gobj)) {
        ftCo_80096900(gobj, 0, 1, false, da->specialhi_fall_mobility,
                      da->specialhi_fall_landing_lag);
    }
}

void ftDd_SpecialAirHiDamage_IASA(HSD_GObj* gobj) {}

void ftDd_SpecialAirHiDamage_Phys(HSD_GObj* gobj)
{
    Fighter* fp = GET_FIGHTER(gobj);

    ftCommon_CalcSelfAccel_Deaccel(fp, fp->co_attrs.aerial_friction);
    ftCommon_Fall(fp, fp->co_attrs.gravity, fp->co_attrs.terminal_velocity);
}

void ftDd_SpecialAirHiDamage_Coll(HSD_GObj* gobj)
{
    ft_800831CC(gobj, ftCo_80096CC8, ftDd_SpecialHi_OnLand);
}
