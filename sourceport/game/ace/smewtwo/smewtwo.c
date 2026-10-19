/* PlSm.dat onload and the literal 20-row action-state descriptor. */
#include "smewtwo.h"
#include <melee/ft/ft_081B.h>
#include <melee/ft/ft_084E.h>
#include <melee/ft/ftcamera.h>
#include <melee/ft/ftparts.h>
#include <melee/ft/inlines.h>
#include <melee/ft/kinds/ftCommon/ftCo_0A01.h>
#include <melee/ft/kinds/ftCommon/ftCo_AttackAir.h>
#include <melee/ft/kinds/ftCommon/ftCo_AppealS.h>
#include <melee/ft/kinds/ftMewtwo/ftmewtwospecialn.h>
#include <melee/ft/kinds/ftMewtwo/ftmewtwospecialhi.h>
#include <melee/ft/kinds/ftMewtwo/ftmewtwospeciallw.h>
#include <melee/it/kinds/types.h>
#include <sysdolphin/baselib/gobj.h>
#include <sysdolphin/baselib/gobjproc.h>
#include <sysdolphin/baselib/memory.h>

static void ftSm_CpuProc(HSD_GObj* gobj)
{
    Fighter* fp=GET_FIGHTER(gobj);
    if(fp->is_sleeping || !ftCo_IsCpuControlled(fp)) return;
    FighterKind saved=fp->kind;
    /* Literal spoof word +0E08, initialized to 21 by +0110. */
    fp->kind=(FighterKind)21;
    mu_ak_cpu_process(gobj,NULL);
    fp->kind=saved;
}
static void ftSm_CpuInit(HSD_GObj* gobj)
{
    for(HSD_GObjProc* p=gobj->proc;p!=NULL;p=p->child) {
        if(p->on_invoke==Fighter_procCpu) {
            HSD_GObjProc_RemoveProc(p);
            HSD_GObj_SetupProc(gobj,ftSm_CpuProc,2);
            return;
        }
    }
}
static void ftSm_OnLoad(HSD_GObj* gobj)
{
    Fighter* fp=GET_FIGHTER(gobj);
    ftSm_Vars* v=ftSm_GetVars(fp);
    fp->dat_attrs=DP(fp->ft_data->ext_attr);
    v->hud=ftSm_Hud_Create(gobj);
    MotionState* states=HSD_MemAlloc(sizeof(MotionState)*341);
    __builtin_memcpy(states,fp->x1C_actionStateList,sizeof(MotionState)*341);
    /* Console byte offsets divided by the console 0x20 state-row stride. */
    states[264].anim_cb=ftSm_CB_10A0;
    states[265].anim_cb=ftSm_CB_10A0;
    states[264].cam_cb=ftSm_CB_1118;
    states[27].phys_cb=ftSm_CB_2634;
    states[28].phys_cb=ftSm_CB_2634;
    for(int i=65;i<=69;i++) states[i].phys_cb=ftSm_CB_2638;
    const int cameras[]={223,226,275,278,276,288,331,301,302,336,327,334,333,232,231,230};
    for(int i=0;i<(int)(sizeof(cameras)/sizeof(cameras[0]));i++) states[cameras[i]].cam_cb=ftSm_CB_2704;
    fp->x1C_actionStateList=states;
    DISC_PTR(void)* articles=DP(fp->ft_data->x48_items);
    mu_ak_register_article(fp->kind,DP(articles[1]),1);
    mu_ak_register_article(fp->kind,DP(articles[3]),3);
    HSD_GObj_SetupProc(gobj,ftSm_CpuInit,2);
    /* +0088 also clears the HUD reference immediately after creating it. */
    v->hud=NULL;
    v->f4=v->jump_timer=v->jump_used=v->e8=0;
    v->energy=0;
    v->transformed=0;
    __builtin_memset(v->hitbox_upgraded,0,4);
    v->cooldown=v->air_used=v->power=v->capped=v->level=0;
    __builtin_memset(v->targets,0,sizeof(v->targets));
    ftSm_Trails_OnLoad(gobj);
    ftParts_80074A4C(gobj,1,1);
    ftParts_80074A4C(gobj,2,0);
}
static const MotionState ftSm_States[]={
#include "smewtwo_states.inc"
};
extern ItemLogicTable* const itSm_ArticleTables[4];
const MuAkFighter mu_ak_smewtwo = {
    .name="S. Mewtwo",.file="PlSm.dat",.onload=ftSm_OnLoad,
    .specialn=ftSm_SpecialN_Enter,.specialairn=ftSm_SpecialAirN_Enter,
    .specials=ftSm_SpecialS_Enter,.specialairs=ftSm_SpecialAirS_Enter,
    .specialhi=ftSm_SpecialHi_Enter,.specialairhi=ftSm_SpecialAirHi_Enter,
    .speciallw=ftSm_SpecialLw_Enter,.specialairlw=ftSm_SpecialAirLw_Enter,
    .onframe=ftSm_OnFrame,.onactionstatechange=ftSm_OnStateChange,
    .onrespawn=ftSm_OnRespawn,.enterdoublejump=ftSm_DoubleJump,.onlanding=ftSm_OnLanding,
    .move_logic=ftSm_States,.move_logic_count=20,
    .article_tables=itSm_ArticleTables,.article_count=4,.flags = MU_AK_READY,
};
