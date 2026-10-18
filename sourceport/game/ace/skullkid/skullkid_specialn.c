/* PlSd.dat +620..+8FC, +160C..+1BFC, +2E94..+2FA0. */
#include "skullkid.h"
#include <melee/ft/fighter.h>
#include <melee/ft/ft_081B.h>
#include <melee/ft/ft_084E.h>
#include <melee/ft/ft_0892.h>
#include <melee/ft/ftanim.h>
#include <melee/ft/ftcliffcommon.h>
#include <melee/ft/ftcommon.h>
#include <melee/ft/ftparts.h>
#include <melee/ft/inlines.h>
#include <melee/ft/kinds/ftCommon/ftCo_AirCatch.h>
#include <melee/ft/kinds/ftCommon/ftCo_Fall.h>
#include <melee/ft/kinds/ftCommon/ftCo_FallSpecial.h>
#include <melee/it/inlines.h>
#include <melee/it/item.h>
#include <melee/it/it_2725.h>
#include <melee/it/it_3F14.h>
#include <melee/lb/lb_00B0.h>
#include <sysdolphin/baselib/jobj.h>

static void ftSd_N_Enter(HSD_GObj* gobj, bool air)
{
    Fighter* fp = GET_FIGHTER(gobj);
    ftSd_FighterVars* v = ftSd_Vars(fp);
    fp->cmd_vars[0] = 0;
    fp->throw_flags_b0 = false;
    fp->accessory4_cb = ftSd_SpecialN_Spawn;
    if (v->spell != NULL) {
        int state = GET_ITEM(v->spell)->msid;
        if ((u32)(state-2) <= 3) {
            if (air) fp->self_vel.y *= 0.7f;
            ftSd_Change(gobj, air ? 353 : 349, 0, 0, 1, 0);
            ftAnim_8006EBA4(gobj);
            fp->accessory4_cb = ftSd_SpecialN_Spawn;
            return;
        }
    } else if (v->ready != 0) {
        ftSd_Change(gobj, air ? 352 : 348, 0, 0, 1, 0);
        ftAnim_8006EBA4(gobj);
        fp->accessory4_cb = ftSd_SpecialN_Spawn;
    }
    fp->facing_dir = ((HSD_JObj*)gobj->hsd_obj)->rotate.y > 0 ? 1.0f : -1.0f;
}
void ftSd_SpecialN_Enter(HSD_GObj* gobj) { ftSd_N_Enter(gobj, false); }
void ftSd_SpecialAirN_Enter(HSD_GObj* gobj) { ftSd_N_Enter(gobj, true); }
void ftSd_Empty(HSD_GObj* gobj) {}

/* +160C, +16F4, +17DC are the same calls and branch. */
void ftSd_CB_160C(HSD_GObj* gobj)
{
    if (!ftAnim_IsFramesRemaining(gobj)) ft_8008A2BC(gobj);
}
void ftSd_CB_16F4(HSD_GObj* gobj) { ftSd_CB_160C(gobj); }
void ftSd_CB_17DC(HSD_GObj* gobj) { ftSd_CB_160C(gobj); }
static void ftSd_N_OffGround(HSD_GObj* gobj, int state)
{
    Fighter* fp = GET_FIGHTER(gobj);
    if (!ft_80082708(gobj)) {
        ftCommon_8007D5D4(fp);
        ftSd_Change(gobj, state, 0x5000, fp->cur_anim_frame, 1, 0);
        fp->accessory4_cb = ftSd_SpecialN_Spawn;
    }
}
void ftSd_CB_1660(HSD_GObj* gobj) { ftSd_N_OffGround(gobj,352); }
void ftSd_CB_1748(HSD_GObj* gobj) { ftSd_N_OffGround(gobj,353); }
void ftSd_CB_182C(HSD_GObj* gobj)
{
    Fighter* fp = GET_FIGHTER(gobj);
    ftCommon_CalcGroundAccel_Deaccel(fp, fp->co_attrs.ground_friction * 1.5f);
}
void ftSd_CB_1844(HSD_GObj* gobj)
{
    if (!ft_80082708(gobj)) {
        ftCommon_8007D5D4(GET_FIGHTER(gobj));
        ftCo_Fall_IASA_Inner(gobj);
        ftCo_Fall_Enter(gobj);
    }
}
void ftSd_CB_18A0(HSD_GObj* gobj)
{
    if (!ftAnim_IsFramesRemaining(gobj)) ftCo_Fall_Enter(gobj);
}
void ftSd_CB_1984(HSD_GObj* gobj) { ftSd_CB_18A0(gobj); }
static void ftSd_N_Land(HSD_GObj* gobj, int state)
{
    Fighter* fp = GET_FIGHTER(gobj);
    if (ft_80081D0C(gobj)) {
        ftCommon_8007D7FC(fp);
        /* +1940 and +1A68 load blend=1, not the usual zero. */
        ftSd_Change(gobj, state, 0, fp->cur_anim_frame, 1, 1);
        fp->accessory4_cb = ftSd_SpecialN_Spawn;
    }
}
void ftSd_CB_18F4(HSD_GObj* gobj) { ftSd_N_Land(gobj,348); }
void ftSd_CB_1A20(HSD_GObj* gobj) { ftSd_N_Land(gobj,349); }
void ftSd_CB_19D4(HSD_GObj* gobj)
{
    Fighter* fp = GET_FIGHTER(gobj);
    ftCommon_CalcSelfAccel_Drift(fp);
    ftCommon_Fall(fp,fp->co_attrs.gravity * 0.05f,fp->co_attrs.fast_fall_velocity * 0.05f);
}
void ftSd_CB_1AB0(HSD_GObj* gobj)
{
    Fighter* fp = GET_FIGHTER(gobj);
    if (!ftAnim_IsFramesRemaining(gobj))
        ftCo_80096900(gobj,1,1,1,fp->co_attrs.air_drift_max,30.0f);
}
void ftSd_CB_1B30(HSD_GObj* gobj)
{
    Fighter* fp = GET_FIGHTER(gobj);
    if (ft_80081D0C(gobj)) {
        ftCommon_8007D7FC(fp);
        ftSd_Change(gobj,351,0,fp->cur_anim_frame,1,1);
        ftAnim_8006EBA4(gobj);
    }
    if (ftCo_800C3A14(gobj) && !ft_80082E3C(gobj)) {
        ftCo_Fall_IASA_Inner(gobj);
        ftCliffCommon_80081370(gobj);
    }
}

/* +2E94. Existing spell states 2..5 are released; 0,1,6 can be replaced. */
void ftSd_SpecialN_Spawn(HSD_GObj* gobj)
{
    Fighter* fp = GET_FIGHTER(gobj);
    ftSd_FighterVars* v = ftSd_Vars(fp);
    if (!fp->throw_flags_b0) return;
    if (v->spell != NULL) {
        int state = GET_ITEM(v->spell)->msid;
        fp->throw_flags_b0 = false;
        if ((u32)state > 1 && state != 6) {
            Item_80268E5C(v->spell,6,2);
            it_80275158(v->spell,5.0f);
            return;
        }
    }
    if (v->ready > 0) {
        Vec3 pos;
        fp->throw_flags_b0 = false;
        int bone = ftParts_GetBoneIndex(fp,0x17);
        lb_8000B1CC(fp->parts[bone].joint,NULL,&pos);
        /* The creator stores the result in 22F8, not this caller. */
        itSd_Spell_Spawn(gobj,&pos,mu_ak_article_kind(gobj,0),fp->facing_dir);
    }
}
