/* Literal own move callbacks, PlSm.dat +03AC..+0720 and +0E0C..+1AE4. */
#include "smewtwo.h"
#include <math.h>
#include <melee/ft/ft_081B.h>
#include <melee/ft/ft_084E.h>
#include <melee/ft/ft_0892.h>
#include <melee/ft/ftanim.h>
#include <melee/ft/ftcamera.h>
#include <melee/ft/ftcoll.h>
#include <melee/ft/ftcommon.h>
#include <melee/ft/ftwaitanim.h>
#include <melee/ft/inlines.h>
#include <melee/ft/kinds/ftCommon/ftCo_Fall.h>
#include <melee/ft/kinds/ftCommon/ftCo_FallSpecial.h>
#include <melee/lb/types.h>
#include <sysdolphin/baselib/jobj.h>

typedef struct ftSm_HiVars { float angle,rotation,step; } ftSm_HiVars;
static ftSm_HiVars* ftSm_Hi(Fighter* fp) { return (ftSm_HiVars*)&fp->mv; }
static void ftSm_Empty(HSD_GObj* gobj) {}
static void ftSm_GrabbedEmpty(HSD_GObj* gobj,HSD_GObj* victim) {}

void ftSm_SpecialN_Enter(HSD_GObj* gobj)
{
    Fighter* fp=GET_FIGHTER(gobj);
    if (ftSm_GetVars(fp)->cooldown>0) return;
    fp->throw_flags_b0=false;
    fp->cmd_vars[1]=fp->cmd_vars[2]=fp->cmd_vars[3]=0;
    ftSm_Change(gobj,359,0,0,1,0);
    ftAnim_8006EBA4(gobj);
    ftSm_GetVars(fp)->cooldown=100;
}
void ftSm_SpecialAirN_Enter(HSD_GObj* gobj) {}
static void ftSm_S_Enter(HSD_GObj* gobj,int state)
{
    Fighter* fp=GET_FIGHTER(gobj);
    ftSm_Change(gobj,state,0,0,1,0);
    ftAnim_8006EBA4(gobj);
    ftCommon_8007E2D0(fp,40,ftSm_CB_1B80,NULL,ftSm_GrabbedEmpty);
    fp->victim_gobj=NULL;
}
void ftSm_SpecialS_Enter(HSD_GObj* gobj) { ftSm_S_Enter(gobj,351); }
void ftSm_SpecialAirS_Enter(HSD_GObj* gobj) { ftSm_S_Enter(gobj,352); }
static void ftSm_HiEnter(HSD_GObj* gobj,int state)
{
    Fighter* fp=GET_FIGHTER(gobj);
    fp->self_vel.x=fp->self_vel.y=0;
    ftSm_Change(gobj,state,0,0,1,0);
    ftAnim_8006EBA4(gobj);
    ftSm_GetVars(fp)->air_used=1;
}
void ftSm_SpecialHi_Enter(HSD_GObj* gobj) { ftSm_HiEnter(gobj,353); }
void ftSm_SpecialAirHi_Enter(HSD_GObj* gobj) { ftSm_HiEnter(gobj,356); }
static void ftSm_LwEnter(HSD_GObj* gobj,int state)
{
    ftSm_Vars* v=ftSm_GetVars(GET_FIGHTER(gobj));
    if (v->transformed!=1) {
        if (!(v->energy>60)) return;
        v->energy=0;
    }
    ftSm_Change(gobj,state,0,0,1,0);
}
void ftSm_SpecialLw_Enter(HSD_GObj* gobj) { ftSm_LwEnter(gobj,341); }
void ftSm_SpecialAirLw_Enter(HSD_GObj* gobj) { ftSm_LwEnter(gobj,346); }

static void ftSm_NAnim(HSD_GObj* gobj,bool air)
{
    Fighter* fp=GET_FIGHTER(gobj);
    ftSm_Vars* v=ftSm_GetVars(fp);
    if (air) fp->deal_dmg_cb=ftSm_SpecialN_Aura;
    if (fp->cmd_vars[2]!=1) {
        fp->cmd_vars[2]=1;
        if (v->transformed==0) ftSm_Transform(gobj);
        else if (v->transformed==1) { ftSm_ResetTransform(gobj); v->power=0; }
    }
    if (!ftAnim_IsFramesRemaining(gobj)) {
        if (air) ftCo_Fall_Enter(gobj);
        else ft_8008A2BC(gobj);
    }
}
void ftSm_CB_E0C(HSD_GObj* gobj) { ftSm_NAnim(gobj,false); }
void ftSm_CB_F34(HSD_GObj* gobj) { ftSm_NAnim(gobj,true); }
void ftSm_CB_EB4(HSD_GObj* gobj)
{
    Fighter* fp=GET_FIGHTER(gobj);
    if (!ft_80082708(gobj)) {
        ftCommon_8007D5D4(fp);
        ftSm_Change(gobj,346,0,fp->cur_anim_frame,1,1);
    }
}
void ftSm_CB_FE8(HSD_GObj* gobj)
{
    ft_80084DB0(gobj);
    GET_FIGHTER(gobj)->self_vel.y=0;
}
void ftSm_CB_1020(HSD_GObj* gobj)
{
    Fighter* fp=GET_FIGHTER(gobj);
    if (ft_80081D0C(gobj)) {
        ftCommon_8007D7FC(fp);
        ftSm_Change(gobj,341,0x5000,fp->cur_anim_frame,1,1);
    }
}
void ftSm_CB_10A0(HSD_GObj* gobj)
{
    if (!ftAnim_IsFramesRemaining(gobj)) {
        ftCo_8008A6D8(gobj,303);
        ftSm_Change(gobj,14,0,0,1,0);
    }
}
void ftSm_CB_1118(HSD_GObj* gobj)
{
    Fighter* fp=GET_FIGHTER(gobj);
    ftCamera_UpdateCameraBox(gobj);
    if (fp->facing_dir>0) {
        ftSm_Change(gobj,349,0,0,1,0);
        ftAnim_8006EBA4(gobj);
        fp->facing_dir=-1;
    }
    fp->cam_cb=ftCamera_UpdateCameraBox;
}
static void ftSm_SAnim(HSD_GObj* gobj,bool air)
{
    Fighter* fp=GET_FIGHTER(gobj);
    if (fp->cmd_vars[2]==1) {
        if (air) fp->cmd_vars[2]=0;
        else *(int*)&fp->mv=1;
        ReflectDesc reflect={.x0_bone_id=0,.x4_max_damage=50,
            .x8_offset={0,8,8},.x14_size=10,.x18_damage_mul=1,.x1C_speed_mul=1};
        ftColl_CreateReflectHit(gobj,&reflect,NULL);
    }
    if (!air && *(int*)&fp->mv==1 && fp->cmd_vars[2]==0) {
        *(int*)&fp->mv=0;
        fp->reflecting=false;
    }
    ftColl_8007AEF8(gobj);
    if (fp->victim_gobj!=NULL) fp->x914[0].scale=7;
    if (!ftAnim_IsFramesRemaining(gobj)) {
        if (air) ftCo_Fall_Enter(gobj);
        else ft_8008A2BC(gobj);
    }
}
void ftSm_CB_11B0(HSD_GObj* gobj) { ftSm_SAnim(gobj,false); }
void ftSm_CB_1314(HSD_GObj* gobj) { ftSm_SAnim(gobj,true); }
void ftSm_CB_1294(HSD_GObj* gobj) { ft_80084F3C(gobj); }
void ftSm_CB_1298(HSD_GObj* gobj)
{
    Fighter* fp=GET_FIGHTER(gobj);
    if (!ft_80082708(gobj)) {
        ftCommon_8007D5D4(fp);
        ftSm_Change(gobj,352,0x5000,fp->cur_anim_frame,1,0);
    }
}
void ftSm_CB_13D0(HSD_GObj* gobj)
{
    Fighter* fp=GET_FIGHTER(gobj);
    ftCommon_Fall(fp,(float)((double)fp->co_attrs.gravity*0.8),
                    (float)((double)fp->co_attrs.terminal_velocity*0.8));
    ftCommon_CalcSelfAccel_Deaccel(fp,fp->co_attrs.aerial_friction+fp->co_attrs.aerial_friction);
}
void ftSm_CB_1424(HSD_GObj* gobj)
{
    Fighter* fp=GET_FIGHTER(gobj);
    if (ft_80081D0C(gobj)) {
        ftCommon_8007D7FC(fp);
        ftSm_Change(gobj,351,0,fp->cur_anim_frame,1,1);
        ftAnim_8006EBA4(gobj);
    }
}

/* +305C, including the original unusual upper/lower-angle branch order. */
static void ftSm_HiAngle(Fighter* fp)
{
    float x=fp->input.lstick[0].x,y=fp->input.lstick[0].y;
    float angle;
    if ((double)fabsf(x)<0.3 && (double)fabsf(y)<0.3) {
        angle=fp->facing_dir>0 ? 0.7853981852531433f : 2.356194496154785f;
    } else {
        angle=atan2f(y,x);
        if (angle<0) angle=(float)((double)angle+6.283185307179586);
        if ((double)angle>4.812388980384689) angle=0.6283185482025146f;
        else if ((double)angle>4.61238898038469)
            angle=fp->facing_dir>0 ? 0.6283185482025146f : 2.5132741928100586f;
        else if ((double)angle>2.5132741228718345) angle=2.5132741928100586f;
        else if ((double)angle<0.6283185307179586) angle=0.6283185482025146f;
    }
    ftSm_Hi(fp)->angle=angle;
}
void ftSm_CB_14AC(HSD_GObj* gobj)
{
    Fighter* fp=GET_FIGHTER(gobj);
    if (!ftAnim_IsFramesRemaining(gobj)) {
        if ((double)fabsf(fp->input.lstick[0].x)>0.2) ftCommon_UpdateFacing(fp);
        ftSm_Change(gobj,358,0,0,1,0);
        ftSm_HiAngle(fp);
        ftSm_Hi(fp)->rotation=fp->facing_dir>0 ?
            (float)(1.5707963267948966-(double)ftSm_Hi(fp)->angle) :
            (float)((double)ftSm_Hi(fp)->angle-1.5707963267948966);
    }
}
void ftSm_CB_15B4(HSD_GObj* gobj)
{
    GET_FIGHTER(gobj)->self_vel.x=GET_FIGHTER(gobj)->self_vel.y=0;
}
void ftSm_CB_15CC(HSD_GObj* gobj)
{
    if (GET_FIGHTER(gobj)->ground_or_air==GA_Air) ft_80083B68(gobj);
    else ft_800827A0(gobj);
}
void ftSm_CB_15E4(HSD_GObj* gobj)
{
    if (!ftAnim_IsFramesRemaining(gobj)) ftCo_80096900(gobj,1,1,0,0.6f,30);
}
void ftSm_CB_164C(HSD_GObj* gobj)
{
    Fighter* fp=GET_FIGHTER(gobj);
    ftSm_HiVars* h=ftSm_Hi(fp);
    if (fp->cmd_vars[1]!=1) {
        h->step=h->rotation/5.0f;
        fp->cmd_vars[1]=1;
        fp->self_vel.y=sinf(h->angle);
        fp->self_vel.x=cosf(h->angle);
    }
    h->rotation-=h->step;
    fp->parts[2].joint->rotate.x=h->rotation;
}
void ftSm_CB_16DC(HSD_GObj* gobj)
{
    if (!ftAnim_IsFramesRemaining(gobj)) ftSm_Change(gobj,354,0,0,1,0);
}
void ftSm_CB_1748(HSD_GObj* gobj) {}
/* parts[2]'s joint +1C (stfs/lfs 0x1c) is rotate.x, here and at +16CC. */
void ftSm_CB_174C(HSD_GObj* gobj)
{
    Fighter* fp=GET_FIGHTER(gobj);
    ftSm_HiVars* h=ftSm_Hi(fp);
    HSD_JObj* joint=fp->parts[2].joint;
    if (fp->cmd_vars[1]!=1) {
        if (joint->rotate.x<h->rotation)
            joint->rotate.x=__builtin_fmaf(h->rotation,0.25f,joint->rotate.x);
        else { fp->cmd_vars[1]=1; joint->rotate.x=h->rotation; }
    } else if (fabsf(fp->input.lstick[0].x)>0.5f || fabsf(fp->input.lstick[0].y)>0.5f) {
        float facing=fp->facing_dir;
        float angle=atan2f(fp->input.lstick[0].y,fp->input.lstick[0].x);
        if (angle<0) angle=(float)((double)angle+6.283185307179586);
        float target;
        if (fp->facing_dir>0) {
            if ((double)angle>4.71238898038469) angle=(float)((double)angle-6.283185307179586);
            target=(float)(1.5707963267948966-(double)angle);
        } else target=(float)((double)angle-1.5707963267948966);
        float rotation=h->rotation;
        if (rotation<target) rotation=(float)((double)rotation+0.05);
        else if (rotation>target) rotation=(float)((double)rotation-0.05);
        if (rotation!=h->rotation) h->angle=(float)__builtin_fma((double)facing,-0.05,(double)h->angle);
        h->rotation=rotation;
        joint->rotate.x=rotation;
    }
    fp->self_vel.y=(float)((double)sinf(h->angle)*2.9869666695594788);
    fp->self_vel.x=(float)((double)cosf(h->angle)*2.9869666695594788);
}
void ftSm_CB_1930(HSD_GObj* gobj)
{
    Fighter* fp=GET_FIGHTER(gobj);
    if (fp->throw_flags_b0) {
        fp->throw_flags_b0=false;
        ftSm_Projectiles_Spawn(gobj);
        ftSm_GetVars(fp)->cooldown=100;
    }
    if (!ftAnim_IsFramesRemaining(gobj)) ft_8008A2BC(gobj);
}
void ftSm_CB_19B4(HSD_GObj* gobj)
{
    if (!ft_80082708(gobj)) {
        ftCommon_8007D5D4(GET_FIGHTER(gobj));
        ftCo_Fall_Enter(gobj);
    }
}
void ftSm_CB_1A08(HSD_GObj* gobj)
{
    GET_FIGHTER(gobj)->throw_flags_b0=false;
    if (!ftAnim_IsFramesRemaining(gobj)) ftCo_Fall_Enter(gobj);
}
void ftSm_CB_1A64(HSD_GObj* gobj) { ft_80084DB0(gobj); }
void ftSm_CB_1A68(HSD_GObj* gobj)
{
    Fighter* fp=GET_FIGHTER(gobj);
    if (ft_80081D0C(gobj)) {
        ftCommon_8007D7FC(fp);
        ftSm_Change(gobj,359,0x5000,fp->cur_anim_frame,1,0);
    }
}
