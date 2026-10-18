/* PlSd.dat +AAC, +B08, +272C..+2E94, +3020. */
#include "skullkid.h"
#include <melee/ft/ft_081B.h>
#include <melee/ft/ft_084E.h>
#include <melee/ft/ft_0892.h>
#include <melee/ft/ftanim.h>
#include <melee/ft/ftcommon.h>
#include <melee/ft/inlines.h>
#include <melee/ft/kinds/ftCommon/ftCo_Fall.h>
#include <melee/ft/kinds/ftCommon/ftCo_Wait.h>

static void ftSd_LwStartAir(HSD_GObj* gobj)
{
    Fighter* fp = GET_FIGHTER(gobj);
    fp->self_vel.x *= 0.4f;
    if (ftSd_Vars(fp)->f0 == 0) fp->self_vel.y = 0;
    ftSd_Change(gobj,372,0,0,1,0);
    ftAnim_8006EBA4(gobj);
}
void ftSd_SpecialLw_Enter(HSD_GObj* gobj)
{
    GET_FIGHTER(gobj)->cmd_vars[0] = 0;
    ftSd_Change(gobj,367,0,0,1,0);
    ftAnim_8006EBA4(gobj);
}
void ftSd_SpecialAirLw_Enter(HSD_GObj* gobj)
{
    GET_FIGHTER(gobj)->cmd_vars[0] = 0;
    ftSd_LwStartAir(gobj);
}
void ftSd_CB_272C(HSD_GObj* gobj)
{
    Fighter* fp = GET_FIGHTER(gobj);
    bool remaining = ftAnim_IsFramesRemaining(gobj);
    ftSd_Vars(fp)->e4 = fp->cmd_vars[0];
    if (!remaining) ft_8008A2BC(gobj);
}
static void ftSd_LwFall(HSD_GObj* gobj,int state)
{
    Fighter* fp = GET_FIGHTER(gobj);
    if (!ft_80082708(gobj)) {
        ftCommon_8007D5D4(fp);
        ftSd_Change(gobj,state,0x5000,fp->cur_anim_frame,1,0);
    }
}
void ftSd_CB_2798(HSD_GObj* gobj) { ftSd_LwFall(gobj,372); }
static void ftSd_LwNext(HSD_GObj* gobj,int state)
{
    if (!ftAnim_IsFramesRemaining(gobj)) {
        ftSd_Change(gobj,state,0,0,1,0);
        ftAnim_8006EBA4(gobj);
    }
}
void ftSd_CB_2820(HSD_GObj* gobj) { ftSd_LwNext(gobj,370); }
void ftSd_CB_2894(HSD_GObj* gobj)
{
    if (GET_FIGHTER(gobj)->cmd_vars[0] != 0) ftCo_Wait_IASA(gobj);
}
/* These callbacks really are bare collision tests, with their result ignored. */
void ftSd_CB_28AC(HSD_GObj* gobj) { ft_80082708(gobj); }
void ftSd_CB_28B0(HSD_GObj* gobj) { ftSd_CB_160C(gobj); }
void ftSd_CB_2904(HSD_GObj* gobj) { ftSd_LwFall(gobj,374); }
void ftSd_CB_2980(HSD_GObj* gobj) { ftSd_CB_160C(gobj); }
void ftSd_CB_29D4(HSD_GObj* gobj) { ft_80082708(gobj); }
void ftSd_CB_29D8(HSD_GObj* gobj) { ftSd_LwNext(gobj,370); }
void ftSd_CB_2A54(HSD_GObj* gobj) { ft_80082708(gobj); }
void ftSd_CB_2A58(HSD_GObj* gobj)
{
    Fighter* fp = GET_FIGHTER(gobj);
    bool remaining = ftAnim_IsFramesRemaining(gobj);
    ftSd_Vars(fp)->e4 = fp->cmd_vars[0];
    if (!remaining) {
        ftSd_Vars(fp)->f0++;
        ftCo_Fall_Enter(gobj);
    }
}
void ftSd_CB_2ACC(HSD_GObj* gobj) { ftSd_CB_1D6C(gobj); }
static void ftSd_LwLand(HSD_GObj* gobj,int state)
{
    Fighter* fp = GET_FIGHTER(gobj);
    if (ft_80081D0C(gobj)) {
        ftCommon_8007D7FC(fp);
        ftSd_Change(gobj,state,0,fp->cur_anim_frame,1,1);
    }
}
void ftSd_CB_2AD0(HSD_GObj* gobj) { ftSd_LwLand(gobj,367); }
void ftSd_CB_2B54(HSD_GObj* gobj) { ftSd_LwNext(gobj,375); }
void ftSd_CB_2BD0(HSD_GObj* gobj) { ft_80081D0C(gobj); }
void ftSd_CB_2BD4(HSD_GObj* gobj) { ftSd_LwNext(gobj,375); }
void ftSd_CB_2C4C(HSD_GObj* gobj)
{
    Fighter* fp = GET_FIGHTER(gobj);
    ftCommon_CalcSelfAccel_DriftSimple(fp,0,fp->co_attrs.aerial_drift_base*0.4f,
                                     fp->co_attrs.air_drift_max*0.4f);
    ftCommon_Fall(fp,fp->co_attrs.gravity*0.05f,fp->co_attrs.fast_fall_velocity*0.05f);
}
void ftSd_CB_2CB8(HSD_GObj* gobj) { ftSd_LwLand(gobj,369); }
void ftSd_CB_2D3C(HSD_GObj* gobj) { ftSd_CB_18A0(gobj); }
void ftSd_CB_2D90(HSD_GObj* gobj) { ftSd_LwLand(gobj,370); }
void ftSd_CB_2E14(HSD_GObj* gobj) { ftSd_LwNext(gobj,375); }
void ftSd_CB_2E90(HSD_GObj* gobj) { ft_80081D0C(gobj); }
