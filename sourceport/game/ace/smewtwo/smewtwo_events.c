/* Own fighter events and mechanics, PlSm.dat +0720..+0AE4, +1B80..+2398. */
#include "smewtwo.h"
#include <dolphin/os.h>
#include <math.h>
#include <melee/ef/efsync.h>
#include <melee/ft/ft_081B.h>
#include <melee/ft/ft_0881.h>
#include <melee/ft/ftanim.h>
#include <melee/ft/ftcamera.h>
#include <melee/ft/ftcolanim.h>
#include <melee/ft/ftcommon.h>
#include <melee/ft/ftlib.h>
#include <melee/ft/ftparts.h>
#include <melee/ft/inlines.h>
#include <melee/ft/kinds/ftCommon/ftCo_AttackAir.h>
#include <melee/ft/kinds/ftCommon/ftCo_Fall.h>
#include <melee/ft/kinds/ftCommon/ftCo_Pass.h>
#include <melee/pl/player.h>
#include <melee/gm/gmvs.h>

/* The captured fighter's console cmd_var1 stores a Fighter pointer. Native
 * cmd_vars are 32 bit, so keep the native pointer in its own 64-bit field. */
typedef struct ftSm_Capture { Fighter* captor; } ftSm_Capture;
static ftSm_Capture ftSm_Captures[12];
static ftSm_Capture* ftSm_Captured(Fighter* fp)
{
    return &ftSm_Captures[fp->player_idx*2+fp->is_sub_fighter];
}
void ftSm_CB_1B80(HSD_GObj* gobj)
{
    Fighter* fp=GET_FIGHTER(gobj);
    HSD_GObj* victim=fp->victim_gobj;
    if (victim==NULL) return;
    Fighter* vp=GET_FIGHTER(victim);
    vp->facing_dir=-fp->facing_dir;
    ftCo_8009A184(victim,224,0,0);
    ftSm_Captured(vp)->captor=fp;
    vp->cmd_vars[2]=(int)fp->facing_dir;
    vp->cmd_vars[3]=0;
    vp->self_vel.y=1;
    float x=__builtin_fmaf(fp->facing_dir,20,fp->cur_pos.x);
    vp->self_vel.x=vp->cur_pos.x<x ? 1 : vp->cur_pos.x>x ? -1 : 0;
    vp->phys_cb=ftSm_CB_3328;
    vp->anim_cb=ftSm_CB_3420;
    vp->input_cb=NULL;
}
void ftSm_CB_3328(HSD_GObj* gobj)
{
    Fighter* fp=GET_FIGHTER(gobj);
    Fighter* captor=ftSm_Captured(fp)->captor;
    float y=captor->cur_pos.y+5;
    float x=__builtin_fmaf(captor->facing_dir,20,captor->cur_pos.x);
    fp->cmd_vars[3]++;
    if (!(fp->cur_pos.y<=y)) fp->cur_pos.y=y;
    float delta=x-fp->cur_pos.x;
    if (!(delta>=1.5f) && !(delta<=-1.5f)) fp->self_vel.x=0;
    else if (fp->self_vel.x!=0) return;
    if ((fp->cmd_vars[3]&1)==0) {
        if (fp->cur_pos.x<x) fp->cur_pos.x+=1;
        else if (fp->cur_pos.x>x) fp->cur_pos.x-=1;
        else fp->cur_pos.x+=0.5f;
    }
}
void ftSm_CB_3420(HSD_GObj* gobj)
{
    Fighter* fp=GET_FIGHTER(gobj);
    if (fp->cmd_vars[3]>44 || (u32)(ftSm_Captured(fp)->captor->motion_id-351)>1) {
        ftCommon_8007D5D4(fp);
        ftCo_Fall_Enter(gobj);
        ftSm_Captured(fp)->captor=NULL;
    }
}
void ftSm_CB_1CC8(HSD_GObj* gobj) { ftSm_GetVars(GET_FIGHTER(gobj))->power+=2; }
bool ftSm_CB_1CDC(HSD_GObj* gobj)
{
    Fighter* fp=GET_FIGHTER(gobj);
    ftSm_Vars* v=ftSm_GetVars(fp);
    if (v->transformed==0 && v->energy>=60) return true;
    u32 off=(u32)(fp->motion_id-351);
    if (off<=7 && ((0x93u>>off)&1)) return false;
    float max=0;
    for (int i=0;i<4;i++) {
        HitCapsule* hit=&fp->x914[i];
        if (!hit->state) continue;
        int damage=0;
        for (int j=0;j<24;j++) {
            HitVictim* h=j<12 ? &hit->victims_1[j] : &hit->victims_2[j-12];
            Fighter* victim=h->victim;
            if (victim!=NULL && victim->gobj!=NULL && victim->x1994==0)
                damage=(int)hit->damage;
        }
        if ((float)damage>max) max=(float)damage;
    }
    if (v->transformed==1 && max>0) v->power+=(u32)(fp->motion_id-44)<=1 ? 1 : 3;
    else {
        v->energy+=max;
        if (v->energy>60) ftCo_800BFFD0(fp,(FtColAnim)94,false);
    }
    return true;
}
void ftSm_HitboxUpgrade(HSD_GObj* gobj)
{
    Fighter* fp=GET_FIGHTER(gobj);
    ftSm_Vars* v=ftSm_GetVars(fp);
    for(int i=0;i<4;i++) {
        HitCapsule* hit=&fp->x914[i];
        if (v->hitbox_upgraded[i]) {
            if (hit->state!=0) continue;
            v->hitbox_upgraded[i]=0;
        }
        if (hit->state!=0) {
            hit->x2C+=20;
            hit->unk_count+=5;
            hit->damage+=5;
            hit->x34+=15;
            v->hitbox_upgraded[i]=1;
        }
    }
}
void ftSm_TargetsCheck(HSD_GObj* gobj)
{
    Fighter* fp=GET_FIGHTER(gobj);
    ftSm_Vars* v=ftSm_GetVars(fp);
    for(int i=0;i<4;i++) {
        if (v->targets[i]!=NULL && v->targets[i]->x221F_b1 && Player_GetStocks(fp->player_idx)>0)
            v->cancel=1;
    }
}
static void ftSm_TrackTargets(Fighter* fp)
{
    ftSm_Vars* v=ftSm_GetVars(fp);
    for(int i=0;i<4;i++) {
        HSD_GObj* g=Player_GetEntity(i);
        Fighter* other=g ? GET_FIGHTER(g) : NULL;
        if (other && (fp->player_idx==i || (gm_8016B168() && other->team==fp->team))) other=NULL;
        v->targets[i]=other;
    }
}
static void ftSm_Attrs(Fighter* fp,bool transformed)
{
    fp->co_attrs.dash_initial_velocity=transformed ? 1.8f : 1.6f;
    fp->co_attrs.dash_max_velocity=transformed ? 1.95f : 1.7f;
    fp->co_attrs.jump_h_max_velocity=transformed ? 1.7f : 1.5f;
    fp->co_attrs.ground_to_air_jump_momentum_multiplier=transformed ? 0.9f : 0.75f;
    fp->co_attrs.landingairn_lag=transformed ? 16 : 20;
    fp->co_attrs.landingairf_lag=transformed ? 16 : 19;
    fp->co_attrs.landingairb_lag=transformed ? 16 : 24;
    fp->co_attrs.landingairhi_lag=transformed ? 16 : 20;
    fp->co_attrs.landingairlw_lag=transformed ? 16 : 19;
}
void ftSm_Transform(HSD_GObj* gobj)
{
    Fighter* fp=GET_FIGHTER(gobj);
    ftSm_TrackTargets(fp);
    ftSm_GetVars(fp)->transformed=1;
    ftSm_GetVars(fp)->power=10;
    ftParts_80074A4C(gobj,1,0);
    ftParts_80074A4C(gobj,2,1);
    ftParts_80074B0C(gobj,1,0);
    ftParts_80074B0C(gobj,2,1);
    ftSm_Attrs(fp,true);
    ftLib_SetScale(gobj,1.06f);
}
void ftSm_ResetTransform(HSD_GObj* gobj)
{
    Fighter* fp=GET_FIGHTER(gobj);
    ftSm_Vars* v=ftSm_GetVars(fp);
    v->transformed=v->capped=v->power=0;
    ftLib_SetScale(gobj,1);
    /* +20DC passes the GObj retained in r31 to the Fighter-only sound API.
     * A native cast would read and write beyond that smaller host object.
     * Keep the invalid site visible until its reference behavior is judged. */
    static bool logged;
    if(!logged) {
        logged=true;
        OSReport("[sm] invalid GObj sound call at PlSm +20E8 skipped\n");
    }
    ftParts_80074A4C(gobj,1,1);
    ftParts_80074A4C(gobj,2,0);
    ftParts_80074B0C(gobj,1,1);
    ftParts_80074B0C(gobj,2,0);
    efSync_Spawn(39,gobj,fp->parts[3].joint);
    int bones[]={3,7,13,28,31,42,44,53};
    for(int i=0;i<8;i++) efSync_Spawn(5002,gobj,fp->parts[bones[i]].joint);
    ftSm_Attrs(fp,false);
    ftLib_SetScale(gobj,1);
}
void ftSm_UpdateLevel(Fighter* fp)
{
    ftSm_Vars* v=ftSm_GetVars(fp);
    if (v->power>0) {
        if (v->capped==1 && v->power>5) v->power=5;
        else if (v->capped==0) {
            if(v->power>10) v->power=10;
            else if(v->power<=5) v->capped=1;
        }
    } else { v->power=0; v->cancel=1; }
    v->level=v->power;
    int motion=fp->motion_id;
    if (motion<=69) {
        if (motion>43 && ((0x03F93AC3u>>(motion-44))&1)) v->power--;
    } else if (motion==195 || motion==187 || (u32)(motion-256)<=1) v->power--;
    else if ((u32)(motion-75)<=14 && fp->dmg.x1838_percentTemp>4) v->power-=4;
}
void ftSm_OnFrame(HSD_GObj* gobj)
{
    Fighter* fp=GET_FIGHTER(gobj);
    ftSm_Vars* v=ftSm_GetVars(fp);
    fp->pre_hitlag_cb=ftSm_CB_1CC8;
    v->cooldown--;
    if(fp->cur_anim_frame<2) {
        fp->deal_dmg_cb=(HSD_GObjEvent)ftSm_CB_1CDC;
        if(v->cancel==1) { ftSm_ResetTransform(gobj); v->cancel=0; }
        if((u32)(fp->motion_id-252)<=11) v->air_used=v->jump_used=0;
    }
    if(v->transformed==1) {
        ftSm_HitboxUpgrade(gobj);
        fp->pre_hitlag_cb=ftSm_CB_1CC8;
        ftSm_TargetsCheck(gobj);
    }
}
void ftSm_OnStateChange(HSD_GObj* gobj)
{
    Fighter* fp=GET_FIGHTER(gobj);
    ftSm_Vars* v=ftSm_GetVars(fp);
    if(v->transformed==1) {
        int m=fp->motion_id;
        int masked=m&~2;
        if(masked==212) v->landing=1;
        else if(m==216) { v->landing=0; v->power+=2; }
        else if(v->landing==1 && masked!=213) { v->landing=0;v->power-=3; }
        ftSm_UpdateLevel(fp);
    }
    fp->cmd_vars[0]=fp->cmd_vars[1]=fp->cmd_vars[2]=fp->cmd_vars[3]=0;
    if(v->energy>60) ftCo_800BFFD0(fp,(FtColAnim)94,false);
}
void ftSm_OnRespawn(HSD_GObj* gobj)
{
    Fighter* fp=GET_FIGHTER(gobj);
    ftSm_Vars* v=ftSm_GetVars(fp);
    v->energy=0;
    v->transformed=v->e8=v->air_used=v->jump_used=0;
    __builtin_memset(v->hitbox_upgraded,0,4);
    ftParts_80074A4C(gobj,1,1);
    ftParts_80074A4C(gobj,2,0);
}
void ftSm_OnLanding(HSD_GObj* gobj)
{
    ftSm_Vars* v=ftSm_GetVars(GET_FIGHTER(gobj));
    v->jump_timer=18;
    v->jump_used=v->air_used=0;
}
void ftSm_DoubleJump(HSD_GObj* gobj)
{
    Fighter* fp=GET_FIGHTER(gobj);
    ftSm_Vars* v=ftSm_GetVars(fp);
    if(v->air_used>0 || v->jump_used==1) return;
    int facing=(int)fp->facing_dir;
    bool right=fp->input.lstick[0].x>0;
    int state=((right && facing>=0) || (!right && facing<0)) ? 27 : 28;
    ftSm_Change(gobj,state,0,0,1,0);
    v->jump_timer=18;
    v->jump_used=1;
    if(fp->input.pressed_buttons&0x800) v->jump_input=1;
    else if(fp->input.pressed_buttons&0x400) v->jump_input=2;
    else if(fp->input.pressed_buttons&0x10000) v->jump_input=3;
    fp->x1968_jumpsUsed++;
    v->air_used++;
}
static bool ftSm_JumpHeld(Fighter* fp)
{
    int type=ftSm_GetVars(fp)->jump_input;
    return (type==1 && (fp->input.held_buttons[0]&0x800)) ||
           (type==2 && (fp->input.held_buttons[0]&0x400)) ||
           (type==3 && (fp->input.held_buttons[0]&0x10000));
}
static void ftSm_JumpPhys(HSD_GObj* gobj)
{
    Fighter* fp=GET_FIGHTER(gobj);
    ftSm_Vars* v=ftSm_GetVars(fp);
    if(v->jump_timer==18) {
        fp->self_vel.y=0;
        fp->self_vel.x=fp->co_attrs.jump_h_initial_velocity*fp->input.lstick[0].x;
    } else {
        if(v->jump_timer>0) fp->self_vel.y=(float)((double)fp->self_vel.y+0.2833267319947481);
        if(v->jump_timer<=15) ft_80084DB0(gobj);
    }
    ftCommon_CalcSelfAccel_Deaccel(fp,fp->co_attrs.aerial_friction);
    ftCommon_CalcSelfAccel_Drift(fp);
    v->jump_timer--;
}
static void ftSm_ContinueJump(HSD_GObj* gobj)
{
    Fighter* fp=GET_FIGHTER(gobj);
    if(ftSm_JumpHeld(fp)) {
        fp->cmd_vars[1]++;
        ftSm_JumpPhys(gobj);
        if(fp->cmd_vars[1]==8) fp->phys_cb=ftSm_JumpPhys;
    } else { ftCo_AttackAir_Phys(gobj); fp->phys_cb=ftCo_AttackAir_Phys; }
}
void ftSm_CB_2634(HSD_GObj* gobj) { ftSm_JumpPhys(gobj); }
void ftSm_CB_2638(HSD_GObj* gobj)
{
    Fighter* fp=GET_FIGHTER(gobj);
    ftSm_Vars* v=ftSm_GetVars(fp);
    if(v->jump_used && ftSm_JumpHeld(fp)) {
        fp->cmd_vars[1]=1;
        v->jump_used=0;
        ftSm_JumpPhys(gobj);
        fp->phys_cb=ftSm_ContinueJump;
    } else { ftCo_AttackAir_Phys(gobj); fp->phys_cb=ftCo_AttackAir_Phys; }
}
void ftSm_CB_2704(HSD_GObj* gobj)
{
    Fighter* fp=GET_FIGHTER(gobj);
    ftCamera_UpdateCameraBox(gobj);
    if(fp->cmd_vars[1]!=2) {
        ftSm_Vars* v=ftSm_GetVars(fp);
        fp->cmd_vars[1]=2;
        v->power-=3;
        v->air_used=v->jump_used=0;
        fp->x1968_jumpsUsed=1;
    }
}
void ftSm_SpecialN_Aura(HSD_GObj* gobj)
{
    Fighter* fp=GET_FIGHTER(gobj);
    fp->x1968_jumpsUsed=1;
    ftSm_GetVars(fp)->air_used=ftSm_GetVars(fp)->jump_used=0;
}
