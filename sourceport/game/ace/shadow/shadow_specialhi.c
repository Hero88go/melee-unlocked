/* PlSh.dat ftFunction +0614/+0698, +150C..+1F20 and +4314..+4890.
 * The warp is its own move; its entries, collision order and stored velocity are literal. */
#include "shadow.h"

#include <math.h>

#include <melee/ft/fighter.h>
#include <melee/ft/ft_081B.h>
#include <melee/ft/ft_0892.h>
#include <melee/ft/ftanim.h>
#include <melee/ft/ftcliffcommon.h>
#include <melee/ft/ftcoll.h>
#include <melee/ft/ftcommon.h>
#include <melee/ft/inlines.h>
#include <melee/ft/kinds/ftCommon/ftCo_FallSpecial.h>
#include <melee/ft/kinds/ftCommon/ftCo_Landing.h>
#include <melee/ft/kinds/ftCommon/ftCo_Pass.h>
#include <melee/lb/lbvector.h>
#include <melee/lb/types.h>

static void ftSh_SpecialHi_WarpEnterAir(HSD_GObj* gobj);
static void ftSh_SpecialHi_EndEnter(HSD_GObj* gobj, bool air);

void ftSh_SpecialHi_Enter(HSD_GObj* gobj)
{
    Fighter* fp = GET_FIGHTER(gobj);
    fp->gr_vel = fp->self_vel.y = fp->self_vel.x = 0.0f;
    Fighter_ChangeMotionState(gobj, ftSh_MS_SpecialHiStart, 0, 0.0f, 1.0f, 0.0f, NULL);
    ftAnim_8006EBA4(gobj);
    fp->cmd_vars[0] = 0;
    fp->accessory4_cb = ftSh_GFXWarp;
    ftSh_MV(fp)->specialhi.coll_frames = 0;
}

void ftSh_SpecialAirHi_Enter(HSD_GObj* gobj)
{
    Fighter* fp = GET_FIGHTER(gobj);
    ftSh_DatAttrs* da = ftSh_DA(fp);
    fp->self_vel.x /= da->specialhi_air_div_x;
    fp->self_vel.y /= da->specialhi_air_div_y;
    Fighter_ChangeMotionState(gobj, ftSh_MS_SpecialAirHiStart, 0, 0.0f, 1.0f, 0.0f, NULL);
    ftAnim_8006EBA4(gobj);
    fp->cmd_vars[0] = 0;
    fp->accessory4_cb = ftSh_GFXWarp;
    ftSh_MV(fp)->specialhi.coll_frames = 0;
}

static void ftSh_SpecialHi_MakeWarp(HSD_GObj* gobj, bool air)
{
    Fighter* fp = GET_FIGHTER(gobj);
    Fighter_ChangeMotionState(gobj, air ? ftSh_MS_SpecialAirHiWarp : ftSh_MS_SpecialHiWarp,
                              0, 3.0f, 1.0f, 0.0f, NULL);
    ftAnim_8006EBA4(gobj);
    ftAnim_SetAnimRate(gobj, 0.0f);
    fp->x2223_b3 = true;
    ftSh_MV(fp)->specialhi.warp_frames = ftSh_DA(fp)->specialhi_warp_frames;
    fp->x1968_jumpsUsed = (u8) fp->co_attrs.max_jumps;
    ftColl_8007B62C(gobj, 2);
    fp->invisible = true;
}

/* +46A0: the small-stick branch uses a fixed up angle and magnitude one. */
static void ftSh_SpecialHi_WarpEnterAir(HSD_GObj* gobj)
{
    Fighter* fp = GET_FIGHTER(gobj);
    ftSh_DatAttrs* da = ftSh_DA(fp);
    ftSh_SpecialHiVars* mv = &ftSh_MV(fp)->specialhi;
    float sx = fp->input.lstick[0].x;
    float sy = fp->input.lstick[0].y;
    float magnitude = lbVector_sqrtf_accurate(MU_FMADDS(sx, sx, sy * sy));
    float angle;
    float speed;
    if (magnitude > 1.0f) {
        magnitude = 1.0f;
    }
    if (da->specialhi_stick_min < magnitude) {
        if (sx > 0.001f) {
            ftCommon_UpdateFacing(fp);
            sy = fp->input.lstick[0].y;
            sx = fp->input.lstick[0].x;
        }
        angle = atan2f(sy, sx * fp->facing_dir);
        mv->stick_x = fp->input.lstick[0].x;
        mv->stick_y = fp->input.lstick[0].y;
    } else {
        ftCommon_8007DA24(fp);
        magnitude = 1.0f;
        angle = 0x1.921fb0p+0f;
        mv->stick_x = 0.0f;
        mv->stick_y = 1.0f;
    }
    speed = MU_FMADDS(da->specialhi_speed_mul, magnitude, da->specialhi_speed_add);
    fp->self_vel.x = (speed * cosf(angle)) * fp->facing_dir;
    /* +4778 recomputes the fused speed before sine. */
    speed = MU_FMADDS(da->specialhi_speed_mul, magnitude, da->specialhi_speed_add);
    fp->self_vel.y = speed * sinf(angle);
    ftSh_SpecialHi_MakeWarp(gobj, true);
}

/* +4314: a stick into the floor or a platform pass selects the air warp. */
static void ftSh_SpecialHi_WarpEnterGround(HSD_GObj* gobj)
{
    Fighter* fp = GET_FIGHTER(gobj);
    ftSh_DatAttrs* da = ftSh_DA(fp);
    float sx = fp->input.lstick[0].x;
    float sy = fp->input.lstick[0].y;
    float magnitude = lbVector_sqrtf_accurate(MU_FMADDS(sx, sx, sy * sy));
    Vec3 stick = { sx, sy, 0.0f };
    if (magnitude > 1.0f) {
        magnitude = 1.0f;
    }
    if (!(da->specialhi_stick_min > magnitude)) {
        float angle = lbVector_AngleXY(&fp->coll_data.floor.normal, &stick);
        if (!(angle < 0x1.921fb0p+0f) && !ftCo_8009A134(gobj)) {
            float speed;
            ftCommon_UpdateFacing(fp);
            angle = atan2f(fp->input.lstick[0].y, fp->input.lstick[0].x * fp->facing_dir);
            ftSh_MV(fp)->specialhi.stick_x = stick.x;
            ftSh_MV(fp)->specialhi.stick_y = stick.y;
            speed = MU_FMADDS(magnitude, da->specialhi_speed_mul, da->specialhi_speed_add);
            fp->gr_vel = (speed * cosf(angle)) * fp->facing_dir;
            ftSh_SpecialHi_MakeWarp(gobj, false);
            return;
        }
    }
    ftCommon_8007D60C(fp);
    ftSh_SpecialHi_WarpEnterAir(gobj);
}

static void ftSh_SpecialHi_StartAnim(HSD_GObj* gobj, bool air)
{
    Fighter* fp = GET_FIGHTER(gobj);
    ftSh_DatAttrs* da = ftSh_DA(fp);
    if (fp->cur_anim_frame < (float) da->specialhi_turn_frames) {
        float stick = fp->input.lstick[0].x;
        bool turn = (fp->facing_dir > 0.0f && stick < -da->specialhi_turn_stick) ||
                    (fp->facing_dir < 0.0f && stick > da->specialhi_turn_stick);
        if (turn) {
            ftCommon_UpdateFacing(fp);
            Fighter_ChangeMotionState(gobj, fp->motion_id, 0, fp->cur_anim_frame,
                                      1.0f, 0.0f, NULL);
        }
    }
    if (!ftAnim_IsFramesRemaining(gobj)) {
        if (air) {
            ftSh_SpecialHi_WarpEnterAir(gobj);
        } else {
            ftSh_SpecialHi_WarpEnterGround(gobj);
        }
    }
}
void ftSh_SpecialHiStart_Anim(HSD_GObj* gobj) { ftSh_SpecialHi_StartAnim(gobj, false); }
void ftSh_SpecialAirHiStart_Anim(HSD_GObj* gobj) { ftSh_SpecialHi_StartAnim(gobj, true); }

void ftSh_SpecialHiStart_Coll(HSD_GObj* gobj)
{
    Fighter* fp = GET_FIGHTER(gobj);
    if (!ft_80082708(gobj)) {
        ftCommon_8007D60C(fp);
        Fighter_ChangeMotionState(gobj, ftSh_MS_SpecialAirHiStart, FTSH_MF_SWAP_A,
                                  fp->cur_anim_frame, 1.0f, 0.0f, NULL);
        fp->accessory4_cb = ftSh_GFXWarp;
    }
}
void ftSh_SpecialAirHiStart_Phys(HSD_GObj* gobj)
{
    Fighter* fp = GET_FIGHTER(gobj);
    ftSh_DatAttrs* da = ftSh_DA(fp);
    ftCommon_Fall(fp, da->specialhi_start_gravity, da->specialhi_start_terminal_vel);
    ftCommon_CalcSelfAccel_DeaccelAir(fp);
}
void ftSh_SpecialAirHiStart_Coll(HSD_GObj* gobj)
{
    Fighter* fp = GET_FIGHTER(gobj);
    if (ft_CheckGroundAndLedge(gobj, (s32) fp->facing_dir)) {
        ftCommon_8007D7FC(fp);
        Fighter_ChangeMotionState(gobj, ftSh_MS_SpecialHiStart, FTSH_MF_SWAP_A,
                                  fp->cur_anim_frame, 1.0f, 0.0f, NULL);
        fp->accessory4_cb = ftSh_GFXWarp;
    } else {
        ftCliffCommon_80081298(gobj);
    }
}
void ftSh_SpecialHiWarp_Anim(HSD_GObj* gobj)
{
    Fighter* fp = GET_FIGHTER(gobj);
    if (--ftSh_MV(fp)->specialhi.warp_frames <= 0) {
        ftSh_SpecialHi_EndEnter(gobj, false);
    }
}
void ftSh_SpecialAirHiWarp_Anim(HSD_GObj* gobj)
{
    Fighter* fp = GET_FIGHTER(gobj);
    if (--ftSh_MV(fp)->specialhi.warp_frames <= 0) {
        ftSh_SpecialHi_EndEnter(gobj, true);
    }
}

void ftSh_SpecialHiWarp_Coll(HSD_GObj* gobj)
{
    Fighter* fp = GET_FIGHTER(gobj);
    bool grounded = ft_80082708(gobj);
    unsigned walls = fp->coll_data.env_flags & 0xFFF;
    if (!grounded) {
        ftCommon_8007D60C(fp);
        if (walls) {
            ftSh_SpecialHi_EndEnter(gobj, true);
        } else {
            Fighter_ChangeMotionState(gobj, ftSh_MS_SpecialAirHiWarp, FTSH_MF_SWAP_A,
                                      fp->cur_anim_frame, 1.0f, 0.0f, NULL);
            fp->x2223_b3 = true;
            fp->invisible = true;
        }
    } else if (walls) {
        ftSh_SpecialHi_EndEnter(gobj, false);
    }
}

static bool ftSh_SpecialHi_BounceAngle(Fighter* fp, Vec3* normal)
{
    float angle = lbVector_AngleXY(normal, &fp->self_vel);
    float degrees = (float) ftSh_DA(fp)->specialhi_bounce_angle + 90.0f;
    double threshold = (double) degrees * FTSH_DEG2RAD;
    return angle > threshold;
}

/* +1AF8: a too-early platform landing can be passed. Ceiling and both walls are checked
 * in that order, including checks after an earlier one has already entered the end state. */
void ftSh_SpecialAirHiWarp_Coll(HSD_GObj* gobj)
{
    Fighter* fp = GET_FIGHTER(gobj);
    ftSh_MV(fp)->specialhi.coll_frames++;
    if (ft_CheckGroundAndLedge(gobj, (s32) fp->facing_dir)) {
        if ((float) ftSh_MV(fp)->specialhi.coll_frames >= ftSh_DA(fp)->specialhi_warp_min_frames ||
            !ftCo_8009A134(gobj)) {
            ftCommon_8007D7FC(fp);
            Fighter_ChangeMotionState(gobj, ftSh_MS_SpecialHiWarp, FTSH_MF_SWAP_A,
                                      fp->cur_anim_frame, 1.0f, 0.0f, NULL);
            fp->invisible = true;
            return;
        }
    }
    if (ftCliffCommon_80081298(gobj)) {
        return;
    }
    if ((fp->coll_data.env_flags & 0x6000) &&
        ftSh_SpecialHi_BounceAngle(fp, &fp->coll_data.ceiling.normal)) {
        ftSh_SpecialHi_EndEnter(gobj, true);
    }
    if ((fp->coll_data.env_flags & 0x3F) &&
        ftSh_SpecialHi_BounceAngle(fp, &fp->coll_data.left_facing_wall.normal)) {
        ftSh_SpecialHi_EndEnter(gobj, true);
    }
    if ((fp->coll_data.env_flags & 0xFC0) &&
        ftSh_SpecialHi_BounceAngle(fp, &fp->coll_data.right_facing_wall.normal)) {
        ftSh_SpecialHi_EndEnter(gobj, true);
    }
}

static void ftSh_SpecialHi_EndEnter(HSD_GObj* gobj, bool air)
{
    Fighter* fp = GET_FIGHTER(gobj);
    ftSh_DatAttrs* da = ftSh_DA(fp);
    ftSh_SpecialHiVars* mv = &ftSh_MV(fp)->specialhi;
    Fighter_ChangeMotionState(gobj, air ? ftSh_MS_SpecialAirHiEnd : ftSh_MS_SpecialHiEnd,
                              0, 0.0f, 1.0f, 0.0f, NULL);
    ftAnim_8006EBA4(gobj);
    fp->invisible = false;
    mv->gr_vel = fp->gr_vel;
    mv->vel_x = fp->self_vel.x;
    mv->vel_y = fp->self_vel.y;
    fp->gr_vel = fp->self_vel.x = fp->self_vel.y = 0.0f;
    fp->accessory4_cb = ftSh_GFXWarp;
    if (air) {
        fp->self_vel.x = mv->vel_x * da->specialhi_end_vel_mul;
        fp->self_vel.y = mv->vel_y * da->specialhi_end_vel_mul;
    } else {
        fp->gr_vel = mv->gr_vel * da->specialhi_end_vel_mul;
    }
}
void ftSh_SpecialHiEnd_Anim(HSD_GObj* gobj)
{
    if (!ftAnim_IsFramesRemaining(gobj)) {
        ft_8008A2BC(gobj);
    }
}
void ftSh_SpecialHiEnd_Coll(HSD_GObj* gobj)
{
    Fighter* fp = GET_FIGHTER(gobj);
    if (!ft_800827A0(gobj)) {
        ftCommon_8007D60C(fp);
        Fighter_ChangeMotionState(gobj, ftSh_MS_SpecialAirHiEnd, FTSH_MF_SWAP_A,
                                  fp->cur_anim_frame, 1.0f, 0.0f, NULL);
    }
}
void ftSh_SpecialAirHiEnd_Anim(HSD_GObj* gobj)
{
    ftSh_DatAttrs* da = ftSh_DA(GET_FIGHTER(gobj));
    if (!ftAnim_IsFramesRemaining(gobj)) {
        ftCo_80096900(gobj, 1, 0, true, da->specialhi_fall_mobility, da->specialhi_landing_lag);
    }
}
void ftSh_SpecialAirHiEnd_Phys(HSD_GObj* gobj)
{
    Fighter* fp = GET_FIGHTER(gobj);
    if (fp->cmd_vars[0] == 0) {
        fp->self_vel.y -= fp->self_vel.y / 10.0f;
        ftCommon_CalcSelfAccel_DeaccelAir(fp);
    } else {
        ftCommon_FallBasic(fp);
        ftCommon_ClampSelfVelX(fp, ftSh_DA(fp)->specialhi_end_drift * fp->co_attrs.air_drift_max);
    }
}
void ftSh_SpecialAirHiEnd_Coll(HSD_GObj* gobj)
{
    Fighter* fp = GET_FIGHTER(gobj);
    if (ft_CheckGroundAndLedge(gobj, (s32) fp->facing_dir)) {
        ftCo_LandingFallSpecial_Enter(gobj, false, ftSh_DA(fp)->specialhi_landing_lag);
    } else {
        ftCliffCommon_80081298(gobj);
    }
}
