/* PlSd.dat +8FC..+9B8, +1BFC..+1EA4 and +37F4..+3B74. */
#include "skullkid.h"
#include <melee/ft/ft_081B.h>
#include <melee/ft/ft_084E.h>
#include <melee/ft/ftanim.h>
#include <melee/ft/ftcommon.h>
#include <melee/ft/inlines.h>
#include <melee/it/inlines.h>
#include <melee/it/it_2725.h>
#include <melee/it/kinds/types.h>
#include <sysdolphin/baselib/jobj.h>
#include <math.h>

void ftSd_SpecialS_Enter(HSD_GObj* gobj)
{
    ftSd_Change(gobj,356,0,0,1,0);
    ftAnim_8006EBA4(gobj);
}
void ftSd_SpecialAirS_Enter(HSD_GObj* gobj)
{
    Fighter* fp = GET_FIGHTER(gobj);
    fp->self_vel.x *= 0.4f;
    fp->self_vel.y = 0;
    ftSd_Change(gobj,357,0,0,1,0);
    ftAnim_8006EBA4(gobj);
}
void ftSd_CB_1BFC(HSD_GObj* gobj)
{
    if (!ftAnim_IsFramesRemaining(gobj)) {
        ftCommon_8007D5D4(GET_FIGHTER(gobj));
        ftSd_Hi_GroundLaunch(gobj);
    }
}
void ftSd_CB_1C70(HSD_GObj* gobj)
{
    Fighter* fp = GET_FIGHTER(gobj);
    if (!ft_80082708(gobj)) {
        ftCommon_8007D5D4(fp);
        ftSd_Change(gobj,357,0x5000,fp->cur_anim_frame,1,0);
    }
}
void ftSd_CB_1CF8(HSD_GObj* gobj)
{
    if (!ftAnim_IsFramesRemaining(gobj)) {
        ftSd_Vars(GET_FIGHTER(gobj))->f0++;
        ftSd_Hi_GroundLaunch(gobj);
    }
}
void ftSd_CB_1D6C(HSD_GObj* gobj)
{
    Fighter* fp = GET_FIGHTER(gobj);
    float repetitions = (float)(10 * ftSd_Vars(fp)->f0 + 1);
    ftCommon_CalcSelfAccel_DriftSimple(fp,0,fp->co_attrs.aerial_drift_base * 0.4f,
                                     fp->co_attrs.air_drift_max * 0.4f);
    float terminal = fp->co_attrs.fast_fall_velocity * 0.05f;
    float gravity = fp->co_attrs.gravity * 0.05f;
    terminal *= repetitions;
    gravity *= repetitions;
    ftCommon_Fall(fp,gravity,terminal);
}
void ftSd_CB_1E18(HSD_GObj* gobj)
{
    Fighter* fp = GET_FIGHTER(gobj);
    if (ft_80081D0C(gobj)) {
        ftCommon_8007D7FC(fp);
        ftSd_Change(gobj,356,0,fp->cur_anim_frame,1,1);
        ftAnim_8006EBA4(gobj);
    }
}

void ftSd_HideHeld(Fighter* fp)
{
    if (fp->item_gobj != NULL) {
        HSD_JObj* joint = it_80272CC0(fp->item_gobj,0);
        joint->scale.x = joint->scale.y = joint->scale.z = 0.00001f;
        GET_ITEM(fp->item_gobj)->scl = 0.00001f;
    }
}
void ftSd_RestoreHeld(Fighter* fp)
{
    if (fp->item_gobj != NULL) {
        HSD_JObj* joint = it_80272CC0(fp->item_gobj,0);
        joint->scale = fp->x34_scale;
        GET_ITEM(fp->item_gobj)->scl = fp->x34_scale.x;
    }
}

/* +37F4. The small-stick branches choose facing-based diagonals before the
 * angle clamp. The pi/2 comparison is the literal DOUBLE 1.5708. */
void ftSd_Hi_GroundLaunch(HSD_GObj* gobj)
{
    Fighter* fp = GET_FIGHTER(gobj);
    float x = fp->input.lstick[0].x, y = fp->input.lstick[0].y;
    float ax = x < 0 ? -x : x;
    float angle;
    if (ax < 0.2f && y >= 0 && y < 0.2f) {
        angle = fp->facing_dir > 0 ? 0.5f : 2.6415927410125732f;
    } else if (ax < 0.2f && y < 0 && y > -0.2f) {
        angle = fp->facing_dir > 0 ? 0.5f : 2.6415927410125732f;
    } else if (ax < 0.1f && y > 0.9f) {
        angle = fp->facing_dir > 0 ? 0.5f : 2.6415927410125732f;
    } else if (ax < 0.1f && y < -0.9f) {
        angle = fp->facing_dir > 0 ? -0.5f : -2.6415927410125732f;
    } else {
        angle = atan2f(y,x);
    }
    ftSd_Vars(fp)->e8 = 20;
    ftSd_HideHeld(fp);
    if (angle > 0) {
        if ((double)angle <= 1.5708) {
            if (angle < 0.5f) angle = 0.5f;
        } else if (angle > 2.6415927410125732f) angle = 2.6415927410125732f;
    } else {
        if ((double)angle >= -1.5708) {
            if (angle < -0.5f) angle = -0.5f;
        } else if (angle < -2.6415927410125732f) angle = -2.6415927410125732f;
    }
    if (x > 0) fp->facing_dir = 1;
    else if (x < 0) fp->facing_dir = -1;
    float vx = cosf(angle), vy;
    fp->self_vel.x = vx + vx;
    vy = sinf(angle);
    fp->self_vel.y = vy + vy;
    ftSd_Change(gobj,359,0,0,1,0);
    ftAnim_8006EBA4(gobj);
    fp->accessory4_cb = NULL;
}

