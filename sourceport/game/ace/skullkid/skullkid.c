/* PlSd.dat exports, native kind 47, m-ex internal 40, external 39. */
#include "skullkid.h"
#include <math.h>
#include <melee/ft/ft_081B.h>
#include <melee/ft/ft_084E.h>
#include <melee/ft/ft_0892.h>
#include <melee/ft/ftanim.h>
#include <melee/ft/ftcamera.h>
#include <melee/ft/ftcolanim.h>
#include <melee/ft/ftcommon.h>
#include <melee/ft/ftparts.h>
#include <melee/ft/inlines.h>
#include <melee/ft/kinds/ftCommon/ftCo_0A01.h>
#include <melee/ft/kinds/ftCommon/ftCo_AttackHi4.h>
#include <melee/ft/kinds/ftCommon/ftCo_Fall.h>
#include <melee/ft/kinds/ftCommon/ftCo_Wait.h>
#include <melee/ft/kinds/ftNess/ftnessattacks4.h>
#include <melee/ft/kinds/ftNess/ftnessattackhi4.h>
#include <melee/ft/kinds/ftNess/ftnessspecialhi.h>
#include <melee/it/inlines.h>
#include <melee/it/item.h>
#include <melee/it/it_2725.h>
#include <melee/it/it_3F14.h>
#include <melee/it/kinds/types.h>
#include <sysdolphin/baselib/gobj.h>
#include <sysdolphin/baselib/gobjproc.h>
#include <sysdolphin/baselib/memory.h>

/* +3098 and +31B4. The console replaces these two retail state anim slots
 * in a private copy of the 341-state table, leaving both allocations alive. */
static void ftSd_SmashAnim(HSD_GObj* gobj,int state)
{
    Fighter* fp = GET_FIGHTER(gobj);
    ftSd_FighterVars* v = ftSd_Vars(fp);
    if (fp->cmd_vars[2] && v->model1 == NULL && v->model2 == NULL) {
        Vec3 pos = fp->cur_pos;
        fp->cmd_vars[2] = 0;
        ItemKind first = mu_ak_article_kind(gobj,1);
        ItemKind second = mu_ak_article_kind(gobj,2);
        v->model1 = itSd_Model_Spawn(gobj,&pos,second,state,31,fp->facing_dir);
        v->model2 = itSd_Model_Spawn(gobj,&pos,first,state,9,fp->facing_dir);
    }
    if (!ftAnim_IsFramesRemaining(gobj)) {
        Item_GObj* first = v->model1;
        Item_8026A8EC(v->model2);
        Item_8026A8EC(first);
        ft_8008A2BC(gobj);
    }
}
static void ftSd_SmashHiAnim(HSD_GObj* gobj) { ftSd_SmashAnim(gobj,1); }
static void ftSd_SmashLwAnim(HSD_GObj* gobj) { ftSd_SmashAnim(gobj,2); }

/* +32D0. Both IASA calls run before the second availability test. */
static void ftSd_FloatIASA(HSD_GObj* gobj)
{
    Fighter* fp = GET_FIGHTER(gobj);
    if ((fp->input.pressed_buttons & 0x800) && fp->input.lstick[0].y < -0.5f) {
        ftCo_Wait_IASA(gobj);
        ftCo_Fall_IASA_Inner(gobj);
        if (ftSd_Vars(fp)->float_used == 0) {
            ftSd_Change(gobj,361,0x5000,0,1,1);
            ftSd_Vars(fp)->float_used = 1;
            fp->cmd_vars[2] = 0;
            fp->cmd_vars[3] = fp->self_vel.y < -0.5f ? 2 : fp->self_vel.y < 0.5f;
            fp->cmd_vars[1] = 60;
        }
    } else if (fp->input.pressed_buttons != 0) ftCo_Fall_IASA_Inner(gobj);
}

static void ftSd_CpuProc(HSD_GObj* gobj)
{
    Fighter* fp = GET_FIGHTER(gobj);
    if (fp->is_sleeping || !ftCo_IsCpuControlled(fp)) return;
    FighterKind saved = fp->kind;
    /* +1608 is initialized to 0x1C, preserved literally pending the common
     * CPU-kind comparison. +0FC0 is zero and never written by this file. */
    fp->kind = (FighterKind)0x1C;
    mu_ak_cpu_process(gobj,NULL);
    fp->kind = saved;
}
static void ftSd_CpuInit(HSD_GObj* gobj)
{
    for (HSD_GObjProc* p=gobj->proc; p!=NULL; p=p->child) {
        if (p->on_invoke == Fighter_procCpu) {
            HSD_GObjProc_RemoveProc(p);
            HSD_GObj_SetupProc(gobj,ftSd_CpuProc,2);
            return;
        }
    }
}

static void ftSd_OnLoad(HSD_GObj* gobj)
{
    Fighter* fp = GET_FIGHTER(gobj);
    ftSd_FighterVars* v = ftSd_Vars(fp);
    MotionState* backup = HSD_MemAlloc(sizeof(MotionState)*341);
    MotionState* states = HSD_MemAlloc(sizeof(MotionState)*341);
    __builtin_memcpy(backup,fp->x1C_actionStateList,sizeof(MotionState)*341);
    __builtin_memcpy(states,fp->x1C_actionStateList,sizeof(MotionState)*341);
    backup[60].anim_cb = ftSd_SmashHiAnim;
    backup[60].input_cb = ftCo_AttackHi4_IASA;
    backup[63].anim_cb = ftSd_SmashLwAnim;
    backup[244].input_cb = ftSd_FloatIASA;
    fp->x1C_actionStateList = backup;
    /* Keep the native attribute-pool allocation for the engine teardown. */
    fp->dat_attrs = DP(fp->ft_data->ext_attr);
    DISC_PTR(void)* articles = DP(fp->ft_data->x48_items);
    for (int i=0;i<3;i++) mu_ak_register_article(fp->kind,DP(articles[i]),i);
    ftParts_80074A4C(gobj,0,0);
    __builtin_memset(v,0,sizeof(*v));
    /* The first six words are initialized as literal word values by +0098
     * through +00AC, including the inherited Ness item pointer slot. */
    v->ness.yoyo_gobj = (HSD_GObj*)(uintptr_t)9;
    u32 nine = 9;
    __builtin_memcpy(&v->ness.yoyo_hitbox_pos.x,&nine,sizeof(nine));
    v->ready = 1;
    HSD_GObj_SetupProc(gobj,ftSd_CpuInit,2);
    ftSd_Trails_OnLoad(gobj);
}

void ftSd_OnFrame(HSD_GObj* gobj)
{
    Fighter* fp = GET_FIGHTER(gobj);
    ftSd_FighterVars* v = ftSd_Vars(fp);
    if (v->spell != NULL) {
        if ((fp->input.pressed_buttons & 0x200) &&
            fabsf(fp->input.lstick[0].x)<0.4f && fabsf(fp->input.lstick[0].y)<0.4f &&
            ((u32)(fp->motion_id-94)<=21 || (u32)(fp->motion_id-42)<=32) &&
            (u32)(GET_ITEM(v->spell)->msid-2)<=3) {
            Item_GObj* spell = v->spell;
            if (fp->ground_or_air != GA_Ground) {
                ftCo_Fall_IASA_Inner(gobj);
                fp->self_vel.y *= 0.7f;
                ftSd_Change(gobj,353,0,0,1,0);
            } else {
                ftCo_Wait_IASA(gobj);
                ftSd_Change(gobj,349,0,0,1,0);
            }
            ftAnim_8006EBA4(gobj);
            fp->accessory4_cb = ftSd_SpecialN_Spawn;
            Item_80268E5C(spell,6,2);
            it_80275158(spell,5);
        }
    } else if (v->ready<=0) {
        if (v->recharge>299) {
            v->recharge=0;
            v->ready=1;
            ftCo_800BFFD0(fp,(FtColAnim)42,false);
        } else v->recharge++;
    }
    if (fp->cur_anim_frame<2 && (u32)(fp->motion_id-252)<=11) v->d0=0;
}
void ftSd_OnStateChange(HSD_GObj* gobj)
{
    Fighter* fp = GET_FIGHTER(gobj);
    ftSd_FighterVars* v = ftSd_Vars(fp);
    fp->cmd_vars[2]=0;
    if ((u32)(fp->motion_id-223)<=7) v->e4=0;
    if (v->model1!=NULL) Item_8026A8EC(v->model1);
    if (v->model2!=NULL) Item_8026A8EC(v->model2);
}
void ftSd_EnterFloat(HSD_GObj* gobj)
{
    Fighter* fp = GET_FIGHTER(gobj);
    if (ftSd_Vars(fp)->float_used != 0) return;
    ftSd_Change(gobj,361,0x5000,0,1,1);
    ftAnim_8006EBA4(gobj);
    fp->cmd_vars[2]=0;
    ftSd_Vars(fp)->float_used=1;
    fp->cmd_vars[3]=fp->self_vel.y < -0.5f ? 2 : fp->self_vel.y < 0.5f;
    fp->cmd_vars[1]=60;
}
void ftSd_OnLanding(HSD_GObj* gobj)
{
    ftSd_FighterVars* v=ftSd_Vars(GET_FIGHTER(gobj));
    v->landing_timer=20;
    v->float_used=v->d0=v->f0=0;
}

static const MotionState ftSd_States[] = {
#include "skullkid_states.inc"
};
extern const ItemLogicTable itSd_Articles[3];
const MuAkFighter mu_ak_skullkid = {
    .name="Skull Kid", .file="PlSd.dat", .onload=ftSd_OnLoad,
    .specialn=ftSd_SpecialN_Enter, .specialairn=ftSd_SpecialAirN_Enter,
    .specials=ftSd_SpecialS_Enter, .specialairs=ftSd_SpecialAirS_Enter,
    .specialhi=ftSd_SpecialHi_Enter, .specialairhi=ftSd_SpecialAirHi_Enter,
    .speciallw=ftSd_SpecialLw_Enter, .specialairlw=ftSd_SpecialAirLw_Enter,
    .onframe=ftSd_OnFrame, .onactionstatechange=ftSd_OnStateChange,
    .enterfloat=ftSd_EnterFloat, .onlanding=ftSd_OnLanding,
    .move_logic=ftSd_States, .move_logic_count=36,
    .articles=itSd_Articles, .article_count=3, .flags = MU_AK_READY,
};
