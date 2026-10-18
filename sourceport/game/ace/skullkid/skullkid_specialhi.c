/* Literal PlSd.dat +09B8, +0A44, +1EA4..+272C, +3B74..+3E50. */
#include "skullkid.h"
#include <math.h>
#include <melee/ft/ft_081B.h>
#include <melee/ft/ft_084E.h>
#include <melee/ft/ftanim.h>
#include <melee/ft/ftcliffcommon.h>
#include <melee/ft/ftcoll.h>
#include <melee/ft/ftcommon.h>
#include <melee/ft/inlines.h>
#include <melee/ft/kinds/ftCommon/ftCo_AirCatch.h>
#include <melee/ft/kinds/ftCommon/ftCo_Fall.h>
#include <melee/ft/kinds/ftCommon/ftCo_FallSpecial.h>
#include <melee/lb/lb_00B0.h>

void ftSd_SpecialHi_Enter(HSD_GObj* gobj)
{
    Fighter* fp = GET_FIGHTER(gobj);
    fp->cmd_vars[0] = 0;
    fp->throw_flags_b0 = false;
    ftCommon_8007D5D4(fp);
    fp->cmd_vars[0] = 0;
    fp->throw_flags_b0 = false;
    ftSd_Change(gobj,358,0,0,1,0);
    ftAnim_8006EBA4(gobj);
}

void ftSd_SpecialAirHi_Enter(HSD_GObj* gobj)
{
    Fighter* fp = GET_FIGHTER(gobj);
    fp->cmd_vars[0] = 0;
    fp->throw_flags_b0 = false;
    ftSd_Change(gobj,362,0,0,1,0);
    ftAnim_8006EBA4(gobj);
}

void ftSd_Hi_AirLaunch(HSD_GObj* gobj)
{
    Fighter* fp = GET_FIGHTER(gobj);
    float x = fp->input.lstick[0].x;
    float angle = atan2f(fp->input.lstick[0].y,x);
    ftSd_HideHeld(fp);
    ftSd_Vars(fp)->ec = 20;
    if (angle > 0) {
        if (angle < 1.23f) angle = 1.23f;
        else if (angle > 1.911592721939087f) angle = 1.911592721939087f;
    } else {
        if (angle < -1.911592721939087f) angle = -1.911592721939087f;
        else if (angle > -1.23f) angle = -1.23f;
    }
    if (x > 0) fp->facing_dir = 1;
    else if (x < 0) fp->facing_dir = -1;
    fp->self_vel.x = cosf(angle)*1.5f;
    fp->self_vel.y = sinf(angle)*1.5f;
    ftSd_Change(gobj,363,0,0,1,0);
    ftAnim_8006EBA4(gobj);
}

void ftSd_Hi_GroundEnd(HSD_GObj* gobj)
{
    ftSd_RestoreHeld(GET_FIGHTER(gobj));
    ftSd_Change(gobj,364,0,0,1,0);
    ftAnim_8006EBA4(gobj);
}
void ftSd_Hi_AirEnd(HSD_GObj* gobj)
{
    ftSd_Change(gobj,360,0,0,1,0);
    ftAnim_8006EBA4(gobj);
    ftSd_RestoreHeld(GET_FIGHTER(gobj));
}

static void ftSd_Hi_LaunchAnim(HSD_GObj* gobj,bool ground)
{
    Fighter* fp = GET_FIGHTER(gobj);
    if (!ftAnim_IsFramesRemaining(gobj)) {
        lb_8000B1CC(fp->parts[1].joint,NULL,&fp->cur_pos);
        if (ground) ftCommon_8007D5D4(fp);
        ftSd_Hi_AirLaunch(gobj);
    }
}
void ftSd_CB_1EA4(HSD_GObj* gobj) { ftSd_Hi_LaunchAnim(gobj,true); }
void ftSd_CB_2430(HSD_GObj* gobj) { ftSd_Hi_LaunchAnim(gobj,false); }
void ftSd_CB_1F44(HSD_GObj* gobj)
{
    Fighter* fp = GET_FIGHTER(gobj);
    if (!ft_80082708(gobj)) {
        ftCommon_8007D5D4(fp);
        ftSd_Change(gobj,362,0x5000,fp->cur_anim_frame,1,0);
    }
}
void ftSd_CB_1FC0(HSD_GObj* gobj)
{
    Fighter* fp = GET_FIGHTER(gobj);
    ftSd_FighterVars* v = ftSd_Vars(fp);
    if (v->e8 != 0) {
        ftColl_8007B760(gobj,1);
        v->e8--;
    } else if (fp->ground_or_air != GA_Ground) {
        fp->self_vel.x *= 0.5f;
        ftSd_Hi_GroundEnd(gobj);
    } else ftSd_Hi_AirEnd(gobj);
}
void ftSd_CB_2040(HSD_GObj* gobj)
{
    Fighter* fp = GET_FIGHTER(gobj);
    if (ft_80081D0C(gobj)) ftCommon_8007D7FC(fp);
    if (fp->self_vel.y < 0 && ftCo_800C3A14(gobj) && !ft_80082E3C(gobj)) {
        ftCo_Fall_IASA_Inner(gobj);
        ftCliffCommon_80081370(gobj);
        ftSd_RestoreHeld(fp);
    }
}
void ftSd_CB_2128(HSD_GObj* gobj) { ftSd_CB_160C(gobj); }
void ftSd_CB_217C(HSD_GObj* gobj) { ftSd_CB_1844(gobj); }
void ftSd_CB_21D8(HSD_GObj* gobj)
{
    Fighter* fp = GET_FIGHTER(gobj);
    fp->cmd_vars[1]--;
    if (fp->cmd_vars[1] <= 0 || (fp->input.held_buttons[0] & 0x10C00u) == 0)
        ftCo_Fall_Enter(gobj);
}
void ftSd_CB_2204(HSD_GObj* gobj)
{
    Fighter* fp = GET_FIGHTER(gobj);
    if ((fp->input.pressed_buttons & 0x360) || fabsf(fp->input.cstick[0].x) > 0.1 ||
        fabsf(fp->input.cstick[0].y) > 0.1) ftCo_Fall_IASA_Inner(gobj);
}
void ftSd_CB_2268(HSD_GObj* gobj)
{
    Fighter* fp = GET_FIGHTER(gobj);
    int mode = fp->cmd_vars[3];
    if (mode == 1) {
        if (fp->self_vel.y <= 0) fp->self_vel.y = 0;
        else ftCommon_Fall(fp,fp->co_attrs.gravity*3,fp->co_attrs.terminal_velocity);
    } else if (mode == 2) {
        if (fp->self_vel.y < 0) ftCommon_Fall(fp,-fp->co_attrs.gravity,fp->co_attrs.terminal_velocity);
        else fp->self_vel.y = 0;
    } else if (mode == 0) {
        if (fp->cmd_vars[2] <= 12)
            ftCommon_Fall(fp,fp->co_attrs.gravity,fp->co_attrs.terminal_velocity);
        else if (fp->self_vel.y < 0)
            ftCommon_Fall(fp,-fp->co_attrs.gravity,fp->co_attrs.terminal_velocity);
        else fp->self_vel.y = 0;
    }
    fp->cmd_vars[2]++;
    float old = fp->self_vel.x;
    float speed = (float)__builtin_fma((double)fp->input.lstick[0].x,0.2,(double)old);
    float max = fp->co_attrs.air_drift_max;
    if (!(max > fabsf(speed))) speed = old > 0 ? max : -max;
    fp->self_vel.x = speed;
}
void ftSd_CB_23E4(HSD_GObj* gobj)
{
    if (ft_80081D0C(gobj)) ft_80082B1C(gobj);
}
void ftSd_CB_24C4(HSD_GObj* gobj) { GET_FIGHTER(gobj)->self_vel.y = 0; }
void ftSd_CB_24D8(HSD_GObj* gobj)
{
    Fighter* fp = GET_FIGHTER(gobj);
    if (ft_80081D0C(gobj)) {
        ftCommon_8007D7FC(fp);
        ftSd_Change(gobj,358,0,fp->cur_anim_frame,1,1);
    }
}
void ftSd_CB_255C(HSD_GObj* gobj)
{
    Fighter* fp = GET_FIGHTER(gobj);
    ftSd_FighterVars* v = ftSd_Vars(fp);
    if (v->ec > 0) {
        ftColl_8007B760(gobj,1);
        v->ec--;
    } else if (fp->ground_or_air != GA_Ground) ftSd_Hi_GroundEnd(gobj);
    else ftSd_Hi_AirEnd(gobj);
}
void ftSd_CB_25C8(HSD_GObj* gobj) { ftSd_CB_2040(gobj); }
void ftSd_CB_25CC(HSD_GObj* gobj)
{
    Fighter* fp = GET_FIGHTER(gobj);
    if (!ftAnim_IsFramesRemaining(gobj))
        ftCo_80096900(gobj,1,1,0,fp->co_attrs.air_drift_max,10);
}
void ftSd_CB_264C(HSD_GObj* gobj)
{
    Fighter* fp = GET_FIGHTER(gobj);
    if (ft_80081D0C(gobj)) {
        ftCommon_8007D7FC(fp);
        ftSd_Change(gobj,360,0,0,1,1);
        ftAnim_8006EBA4(gobj);
    }
    if (ftCo_800C3A14(gobj) && !ft_80082E3C(gobj)) {
        ftCo_Fall_IASA_Inner(gobj);
        ftCliffCommon_80081370(gobj);
    }
}

