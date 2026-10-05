/* PlNt.dat up special: native C from the symbol listing.
 * Surface responses retain Lucas's body with Ninten's attributes and no PK Thunder effects.
 * Offsets and entry/hold differences are recorded in the Ninten port notes. */
#include "ninten.h"
#include <math.h>
#include <melee/ef/efsync.h>
#include <melee/ft/fighter.h>
#include <melee/ft/ft_081B.h>
#include <melee/ft/ft_084E.h>
#include <melee/ft/ft_0892.h>
#include <melee/ft/ftanim.h>
#include <melee/ft/ftcliffcommon.h>
#include <melee/ft/ftcommon.h>
#include <melee/ft/ftparts.h>
#include <melee/ft/kinds/ftCommon/ftCo_DownBound.h>
#include <melee/ft/kinds/ftCommon/ftCo_Fall.h>
#include <melee/ft/kinds/ftCommon/ftCo_FallSpecial.h>
#include <melee/ft/kinds/ftCommon/ftCo_Landing.h>
#include <melee/lb/lbvector.h>
#include <sysdolphin/baselib/debug.h>

#define LC_PI 3.14159265358979323846
#define LC_HALF_PI 1.5707963267948966
#define LC_TWO_PI 6.283185307179586
#define LC_DEG_TO_RAD_B 0.0174533
#define ftNt_MV ftLc_MV
#define ftNt_Sign ftLc_Sign
static void ftNt_SpecialHi_EnterFall(HSD_GObj*);


static float ftNt_WrapAngle(float a)
{
    if (a < 0.0f) {
        do {
            a = (float) (a + LC_TWO_PI);
        } while (a < 0.0f);
    }
    while (a > LC_TWO_PI) {
        a = (float) (a - LC_TWO_PI);
    }
    return a;
}

static void ftNt_SpecialHi_SlideOffWall(HSD_GObj* gobj, CollData* coll, float angle)
{
    static Vec3 unit_z = { 0.0f, 0.0f, 1.0f }; /* .data.unit_z.0 */
    Fighter* fp = GET_FIGHTER(gobj);
    ftLucas_MotionVars* mv = ftNt_MV(fp);
    float target = angle;

    mv->specialhi.angle = ftNt_WrapAngle(mv->specialhi.angle);
    if (coll->env_flags & Collide_LeftWallMask) {
        float wall = ftNt_WrapAngle(atan2f(coll->left_facing_wall.normal.y,
                                           coll->left_facing_wall.normal.x));
        float d = ftNt_WrapAngle((float) (mv->specialhi.angle + LC_PI)) - wall;
        target = (float) (d < 0.0f || LC_ISNAN(d) ? wall + LC_HALF_PI : wall - LC_HALF_PI);
    }
    if (coll->env_flags & Collide_RightWallMask) {
        float wall = atan2f(coll->right_facing_wall.normal.y, coll->right_facing_wall.normal.x);
        float d = mv->specialhi.angle - ftNt_WrapAngle((float) (wall + LC_PI));
        target = (float) (d < 0.0f || LC_ISNAN(d) ? wall + LC_HALF_PI : wall - LC_HALF_PI);
    }
    lbVector_RotateAboutUnitAxis(&fp->self_vel, &unit_z, target - mv->specialhi.angle);
    mv->specialhi.angle = atan2f(fp->self_vel.y, fp->self_vel.x);
}

static void ftNt_SpecialHi_HitSurface(HSD_GObj* gobj, Vec3* normal, bool is_wall)
{
    Fighter* fp = GET_FIGHTER(gobj);
    ftNintenAttributes* attrs = ftNt_Attrs(fp);
    float angle = lbVector_Angle(normal, &fp->self_vel);

    if (angle > (attrs->x7C_HI_WALL_ANGLE + 90.0f) * LC_DEG_TO_RAD_B) {
        Vec3 rot;
        lbVector_Mirror(&fp->self_vel, normal);
        fp->self_vel.x *= 0.5f;
        fp->self_vel.y *= 0.5f;
        ftCommon_ClampSelfVelX(fp, fp->co_attrs.air_drift_max);
        fp->facing_dir = ftNt_Sign(fp->self_vel.x);
        Fighter_ChangeMotionState(gobj, ftLc_MS_SpecialHiBound, Ft_MF_KeepGfx, 0.0f, 1.0f, 0.0f,
                                  NULL);
        ftAnim_8006EBA4(gobj);
        rot.x = atan2f(-normal->x, normal->y);
        rot.y = 0.0f;
        rot.z = 0.0f;
        efSync_Spawn(1030, gobj, &fp->cur_pos, &rot);
    } else if (is_wall) {
        ftNt_SpecialHi_SlideOffWall(gobj, &fp->coll_data, angle);
    }
}

void ftNt_SpecialHi_Anim(HSD_GObj* gobj)
{
    ftNt_MV(GET_FIGHTER(gobj))->specialhi.frame++;
    if (!ftAnim_IsFramesRemaining(gobj)) {
        ftCo_LandingFallSpecial_Enter(gobj, false, 30.0f);
    }
}

void ftNt_SpecialHi_Phys(HSD_GObj* gobj)
{
    Fighter* fp = GET_FIGHTER(gobj);
    ftLucas_MotionVars* mv = ftNt_MV(fp);
    float old_vel = fp->gr_vel;
    float over = fp->facing_dir * ftNt_Attrs(fp)->x74_HI_DECEL - old_vel;

    fp->gr_vel = -over;
    if (fp->facing_dir == 1.0f) {
        if (!(over < 0.0f || LC_ISNAN(over))) {
            fp->gr_vel = old_vel;
        }
    } else if (!(over > 0.0f || LC_ISNAN(over))) {
        fp->gr_vel = old_vel;
    }
    mv->specialhi.saved_vel = fp->self_vel;
    ftCommon_SetSelfMovementFromGroundedMovement(gobj);
    ftPartSetRotX(
        fp, 0,
        (float) (atan2f(fp->self_vel.x, fp->self_vel.y) * fp->facing_dir - LC_HALF_PI));
}

void ftNt_SpecialHi_Coll(HSD_GObj* gobj)
{
    Fighter* fp = GET_FIGHTER(gobj);
    ftNintenAttributes* attrs = ftNt_Attrs(fp);
    ftLucas_MotionVars* mv = ftNt_MV(fp);
    s32 env = fp->coll_data.env_flags;

    if (ft_80082708(gobj)) {
        float offset;
        if (env & 0x6FFF) {
            /* Hit a wall or the ceiling while sliding: knocked down. */
            fp->gr_vel = 0.0f;
            ftPartSetRotX(fp, 0, 0.0f);
            ftCo_80097D40(gobj);
            return;
        }
        /* Follow the floor. */
        if (fp->coll_data.floor.normal.y > 0.0f || LC_ISNAN(fp->coll_data.floor.normal.y)) {
            offset = fp->facing_dir == 1.0f ? (float) -LC_HALF_PI : (float) LC_HALF_PI;
        } else {
            offset = fp->facing_dir == 1.0f ? (float) LC_HALF_PI : (float) -LC_HALF_PI;
        }
        mv->specialhi.angle =
            atan2f(fp->coll_data.floor.normal.y, fp->coll_data.floor.normal.x) + offset;
        return;
    }

    ftCommon_8007D60C(fp);
    if ((env & 0xFFF) == 0) {
        /* Slid off an edge: keep flying. */
        Fighter_ChangeMotionState(gobj, ftLc_MS_SpecialAirHi, ftLc_MF_SwitchGfxHit,
                                  fp->cur_anim_frame, 1.0f, 0.0f, NULL);
        return;
    }
    mv->specialhi.loop1 = attrs->x58_HI_LOOP1;
    mv->specialhi.loop2 = attrs->x5C_HI_LOOP2;
    mv->specialhi.gravity_delay = attrs->x60_HI_GRAVITY_DELAY;
    mv->specialhi.saved_vel.x = 0.0f;
    mv->specialhi.saved_vel.y = 0.0f;
    mv->specialhi.saved_vel.z = 0.0f;
    mv->specialhi.drop.x = 0.0f;
    mv->specialhi.drop.y = 0.0f;
    mv->specialhi.drop.z = 0.0f;
    fp->death2_cb = NULL;
    fp->take_dmg_cb = NULL;
    ftPartSetRotX(fp, 0, 0.0f);
    Fighter_ChangeMotionState(gobj, ftLc_MS_SpecialAirHiEnd, ftLc_MF_Switch, fp->cur_anim_frame,
                              1.0f, 0.0f, NULL);
}

void ftNt_SpecialAirHi_Anim(HSD_GObj* gobj)
{
    Fighter* fp = GET_FIGHTER(gobj);
    ftLucas_MotionVars* mv = ftNt_MV(fp);
    ftNt_MV(GET_FIGHTER(gobj))->specialhi.frame++;
    if (!ftAnim_IsFramesRemaining(gobj)) {
        float drop = mv->specialhi.drop.x;
        if (drop < 0.0f) {
            drop = -drop;
        }
        fp->self_vel.y = -drop;
        ftNt_SpecialHi_EnterFall(gobj);
    }
}

void ftNt_SpecialAirHi_Phys(HSD_GObj* gobj)
{
    Fighter* fp = GET_FIGHTER(gobj);
    ftNintenAttributes* attrs = ftNt_Attrs(fp);
    ftLucas_MotionVars* mv = ftNt_MV(fp);
    float speed = sqrtf(fp->self_vel.x * fp->self_vel.x + fp->self_vel.y * fp->self_vel.y);
    float slowed = speed - attrs->x74_HI_DECEL;

    if (!(slowed > 0.0f) && !LC_ISNAN(slowed)) {
        slowed = speed;
    }
    fp->self_vel.x = cosf(mv->specialhi.angle) * slowed;
    fp->self_vel.y = sinf(mv->specialhi.angle) * slowed;
    ftPartSetRotX(
        fp, 0,
        (float) (fp->facing_dir * atan2f(fp->self_vel.x, fp->self_vel.y) - LC_HALF_PI));
    mv->specialhi.saved_vel = fp->self_vel;

    if (fp->cmd_vars[0] == 1) {
        float drop = mv->specialhi.drop.x - attrs->x68_HI_FALL_ACCEL;
        float floor_drop = -attrs->x6C_HI_SPEED;
        mv->specialhi.drop.x = drop;
        if (!(drop < floor_drop)) {
            floor_drop = drop;
        }
        mv->specialhi.drop.x = floor_drop;
        fp->cur_pos.y += floor_drop;
    }
}

void ftNt_SpecialAirHi_Coll(HSD_GObj* gobj)
{
    Fighter* fp = GET_FIGHTER(gobj);
    ftNintenAttributes* attrs = ftNt_Attrs(fp);
    ftLucas_MotionVars* mv = ftNt_MV(fp);

    /* Collide with the velocity from before this frame's collision response. */
    fp->self_vel = mv->specialhi.saved_vel;

    if (ft_CheckGroundAndLedge(gobj, (int) fp->facing_dir)) {
        float angle = lbVector_Angle(&fp->coll_data.floor.normal, &fp->self_vel);
        if (angle > (attrs->x7C_HI_WALL_ANGLE + 90.0f) * LC_DEG_TO_RAD_B || LC_ISNAN(angle)) {
            /* Straight into the ground: knocked down. */
            fp->self_vel.x = 0.0f;
            fp->self_vel.y = 0.0f;
            fp->self_vel.z = 0.0f;
            ftPartSetRotX(fp, 0, 0.0f);
            ftCo_80097D40(gobj);
        } else {
            /* Glancing: keep going along the ground. */
            ftCommon_8007D7FC(fp);
            Fighter_ChangeMotionState(gobj, ftLc_MS_SpecialHi, ftLc_MF_SwitchGfxHit,
                                      fp->cur_anim_frame, 1.0f, 0.0f, NULL);
        }
        return;
    }
    if (ftCliffCommon_80081298(gobj)) {
        ftCliffCommon_80081370(gobj);
        return;
    }
    {
        s32 env = fp->coll_data.env_flags;
        if (env & Collide_CeilingMask) {
            ftNt_SpecialHi_HitSurface(gobj, &fp->coll_data.ceiling.normal, false);
        } else if (env & Collide_LeftWallMask) {
            ftNt_SpecialHi_HitSurface(gobj, &fp->coll_data.left_facing_wall.normal, true);
        } else if (env & Collide_RightWallMask) {
            ftNt_SpecialHi_HitSurface(gobj, &fp->coll_data.right_facing_wall.normal, true);
        }
    }
}

void ftNt_SpecialAirHiEnd_Anim(HSD_GObj* gobj)
{
    Fighter* fp = GET_FIGHTER(gobj);
    if (!ftAnim_IsFramesRemaining(gobj)) {
        fp->x1968_jumpsUsed = fp->co_attrs.max_jumps;
        ftNt_SpecialHi_EnterFall(gobj);
    }
}

void ftNt_SpecialHiBound_Anim(HSD_GObj* gobj)
{
    if (!ftAnim_IsFramesRemaining(gobj)) {
        ftNt_SpecialHi_EnterFall(gobj);
    }
}

/* +32AC: no PK Thunder article or effect setup. */
static void ftNt_HiInit(HSD_GObj* gobj)
{
    Fighter* fp = GET_FIGHTER(gobj);
    ftNintenAttributes* attrs = ftNt_Attrs(fp);
    ftLucas_MotionVars* mv = ftNt_MV(fp);
    fp->cmd_vars[0] = fp->cmd_vars[1] = fp->cmd_vars[2] = fp->cmd_vars[3] = 0;
    mv->specialhi.state = 1;
    mv->specialhi.loop1 = attrs->x58_HI_LOOP1;
    mv->specialhi.loop2 = attrs->x5C_HI_LOOP2;
    mv->specialhi.gravity_delay = attrs->x60_HI_GRAVITY_DELAY;
    mv->specialhi.angle = fp->facing_dir == 1.0f ? 0.0f : 3.1415927f;
    mv->specialhi.x236C = 0.0f;
    mv->specialhi.dir_y = 1.0f;
    mv->specialhi.saved_vel = (Vec3) { 0.0f, 0.0f, 0.0f };
    mv->specialhi.frame = 0;
    mv->specialhi.drop = (Vec3) { 0.0f, 0.0f, 0.0f };
    fp->death2_cb = NULL;
    fp->take_dmg_cb = NULL;
    ftPartSetRotX(fp, 0, 0.0f);
}

void ftNt_SpecialHi_Enter(HSD_GObj* gobj)
{
    Fighter_ChangeMotionState(gobj, ftLc_MS_SpecialHiStart, 0, 0.0f, 1.0f, 0.0f, NULL);
    ftNt_HiInit(gobj);
    ftAnim_8006EBA4(gobj);
}
void ftNt_SpecialAirHi_Enter(HSD_GObj* gobj)
{
    Fighter* fp = GET_FIGHTER(gobj);
    Fighter_ChangeMotionState(gobj, ftLc_MS_SpecialAirHiStart, 0, 0.0f, 1.0f, 0.0f, NULL);
    ftNt_HiInit(gobj);
    fp->self_vel.y = 0.0f;
    ftAnim_8006EBA4(gobj);
}
void ftNt_NoCallback(HSD_GObj* gobj) { (void) gobj; }

/* +37F0: only change the state when Start's animation ends. */
static void ftNt_HiStartAnim(HSD_GObj* gobj, FtMotionId state)
{
    if (!ftAnim_IsFramesRemaining(gobj)) {
        Fighter_ChangeMotionState(gobj, state, 0, 0.0f, 1.0f, 0.0f, NULL);
    }
}
void ftNt_SpecialHiStart_Anim(HSD_GObj* gobj) { ftNt_HiStartAnim(gobj, ftLc_MS_SpecialHiHold); }
void ftNt_SpecialAirHiStart_Anim(HSD_GObj* gobj) { ftNt_HiStartAnim(gobj, ftLc_MS_SpecialAirHiHold); }

/* +385C and the two Hold animation callbacks. */
static void ftNt_HiHoldAnim(HSD_GObj* gobj, FtMotionId state)
{
    ftLucas_MotionVars* mv = ftNt_MV(GET_FIGHTER(gobj));
    if (mv->specialhi.loop1 > 0) mv->specialhi.loop1--;
    if (mv->specialhi.loop2 > 0) mv->specialhi.loop2--;
    if (mv->specialhi.loop1 <= 0 && mv->specialhi.loop2 <= 0) {
        Fighter_ChangeMotionState(gobj, state, 0, 0.0f, 1.0f, 0.0f, NULL);
    }
}
void ftNt_SpecialHiHold_Anim(HSD_GObj* gobj) { ftNt_HiHoldAnim(gobj, ftLc_MS_SpecialHiEnd); }
void ftNt_SpecialAirHiHold_Anim(HSD_GObj* gobj) { ftNt_HiHoldAnim(gobj, ftLc_MS_SpecialAirHiEnd); }

/* +1630 and +1D88. Ground tests stick against +/-0.7; air tests +/-0.3.
 * Those are doubles in .rodata.cst8, not the first float word printed by da_data. */
static void ftNt_HiHoldIASA(HSD_GObj* gobj, double threshold)
{
    Fighter* fp = GET_FIGHTER(gobj);
    float stick = fp->input.lstick[0].x;
    if (fp->cmd_vars[0] == 0) {
        float magnitude = stick < 0.0f ? -stick : stick;
        fp->cmd_vars[0] = 1;
        if (ftNt_Attrs(fp)->x9C_HI_TURN_STICK < magnitude) {
            ftCommon_UpdateFacing(fp);
            ftPartSetRotY(fp, 0, fp->facing_dir * 1.5707964f);
        }
    }
    if (fp->cur_anim_frame >= 4.0f) {
        if (fp->facing_dir == -1.0f && stick < -threshold) fp->self_vel.x = -0.7f;
        else if (fp->facing_dir == 1.0f && stick > threshold) fp->self_vel.x = 0.7f;
    }
}
void ftNt_SpecialHiHold_IASA(HSD_GObj* gobj) { ftNt_HiHoldIASA(gobj, 0.7); }
void ftNt_SpecialAirHiHold_IASA(HSD_GObj* gobj) { ftNt_HiHoldIASA(gobj, 0.3); }

static void ftNt_HiGroundToAir(HSD_GObj* gobj, FtMotionId state)
{
    Fighter* fp = GET_FIGHTER(gobj);
    if (!ft_80082708(gobj)) {
        ftCommon_8007D60C(fp);
        Fighter_ChangeMotionState(gobj, state, ftLc_MF_Switch, fp->cur_anim_frame, 1.0f, 0.0f, NULL);
    }
}
void ftNt_SpecialHiStart_Coll(HSD_GObj* gobj) { ftNt_HiGroundToAir(gobj, ftLc_MS_SpecialAirHiStart); }
void ftNt_SpecialHiEnd_Coll(HSD_GObj* gobj) { ftNt_HiGroundToAir(gobj, ftLc_MS_SpecialAirHiEnd); }

/* +1744: the ground Hold changes to air even while its floor check still succeeds. */
void ftNt_SpecialHiHold_Coll(HSD_GObj* gobj)
{
    Fighter* fp = GET_FIGHTER(gobj);
    if (fp->ground_or_air != GA_Air) {
        ftCommon_8007D60C(fp);
        fp->self_vel.y = 0.5f;
        Fighter_ChangeMotionState(gobj, ftLc_MS_SpecialAirHiHold, ftLc_MF_Switch, 0.0f, 1.0f, 0.0f, NULL);
    }
    if (!ft_80082708(gobj)) {
        ftCommon_8007D60C(fp);
        Fighter_ChangeMotionState(gobj, ftLc_MS_SpecialAirHiHold, ftLc_MF_Switch, 0.0f, 1.0f, 0.0f, NULL);
    }
}

static void ftNt_HiAirToGround(HSD_GObj* gobj, FtMotionId state)
{
    Fighter* fp = GET_FIGHTER(gobj);
    if (ft_80081D0C(gobj)) {
        ftCommon_8007D7FC(fp);
        Fighter_ChangeMotionState(gobj, state, ftLc_MF_Switch, fp->cur_anim_frame, 1.0f, 0.0f, NULL);
    }
}
void ftNt_SpecialAirHiStart_Coll(HSD_GObj* gobj) { ftNt_HiAirToGround(gobj, ftLc_MS_SpecialHiStart); }
void ftNt_SpecialAirHiEnd_Coll(HSD_GObj* gobj) { ftNt_HiAirToGround(gobj, ftLc_MS_SpecialHiEnd); }
void ftNt_SpecialAirHiHold_Coll(HSD_GObj* gobj)
{
    Fighter* fp = GET_FIGHTER(gobj);
    ftCollisionBox box = { 12.0f, 0.0f, { -2.0f, 2.0f }, { 2.0f, 2.0f } };
    if (ft_800824A0(gobj, &box)) {
        ftCommon_8007D7FC(fp);
        Fighter_ChangeMotionState(gobj, ftLc_MS_SpecialHiHold, ftLc_MF_Switch, fp->cur_anim_frame, 1.0f, 0.0f, NULL);
    }
}

void ftNt_SpecialHiEnd_Phys(HSD_GObj* gobj)
{
    Fighter* fp = GET_FIGHTER(gobj);
    ft_80084F3C(gobj);
    fp->self_vel.y = 0.0f;
}
void ftNt_SpecialAirHiEnd_Phys(HSD_GObj* gobj)
{
    Fighter* fp = GET_FIGHTER(gobj);
    ftLc_SpecialAirHiStart_Phys(gobj);
    fp->self_vel.y = 1.0f;
}
static void ftNt_SpecialHi_EnterFall(HSD_GObj* gobj)
{
    Fighter* fp = GET_FIGHTER(gobj);
    ftNintenAttributes* attrs = ftNt_Attrs(fp);
    ftCommon_8007D60C(fp);
    if (attrs->x88_HI_LANDING_LAG == 0.0f) ftCo_Fall_Enter(gobj);
    else ftCo_80096900(gobj, 1, 0, false, attrs->x6C_HI_SPEED, attrs->x88_HI_LANDING_LAG);
}

/* +254C: retain the disc's extra floor check after knockdown or ledge catch.
 * Its final call at +260C passes a Fighter to a GObj entry. If reached, report
 * the same invalid-object path safely instead of dereferencing a host address. */
void ftNt_SpecialHiBound_Coll(HSD_GObj* gobj)
{
    Fighter* fp = GET_FIGHTER(gobj);
    if (ft_CheckGroundAndLedge(gobj, 0) == 1) {
        fp->self_vel = (Vec3) { 0.0f, 0.0f, 0.0f };
        ftPartSetRotX(fp, 0, 0.0f);
        ftCo_80097D40(gobj);
    } else if (ftCliffCommon_80081298(gobj)) {
        ftCliffCommon_80081370(gobj);
    }
    if (ft_80081D0C(gobj)) {
        ftCommon_8007D7FC(fp);
        __assert("PlNt.dat", 0x260C, "invalid landing object in disc callback");
    }
}
