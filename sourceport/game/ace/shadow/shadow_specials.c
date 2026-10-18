/* PlSh.dat +1F20..+2964, +397C..+3AA0 and +48BC..+4DC0: stored-charge spin dash. */
#include "shadow.h"

#include <melee/ef/eflib.h>
#include <melee/ef/efsync.h>
#include <melee/ef/types.h>
#include <melee/ft/fighter.h>
#include <melee/ft/ft_081B.h>
#include <melee/ft/ft_084E.h>
#include <melee/ft/ft_0881.h>
#include <melee/ft/ft_0892.h>
#include <melee/ft/ftanim.h>
#include <melee/ft/ftcommon.h>
#include <melee/ft/inlines.h>
#include <melee/ft/kinds/ftCommon/ftCo_Fall.h>
#include <melee/ft/kinds/ftCommon/ftCo_FallSpecial.h>
#include <melee/lb/lb_00B0.h>
#include <sysdolphin/baselib/controller.h>
#include <sysdolphin/baselib/gobj.h>
#include <sysdolphin/baselib/jobj.h>
#include <sysdolphin/baselib/random.h>

static void ftSh_SpecialS_Callbacks(Fighter* fp, bool dash)
{
    fp->take_dmg_cb = ftSh_SpecialS_OnHit;
    fp->death2_cb = ftSh_SpecialS_OnHit;
    fp->take_dmg_2_cb = efLib_DestroyAll;
    fp->pre_hitlag_cb = efLib_PauseAll;
    fp->post_hitlag_cb = efLib_ResumeAll;
    if (dash) {
        fp->deal_dmg_cb = ftSh_Nothing;
    }
}
static void ftSh_SpecialS_State(HSD_GObj* gobj, int state)
{
    Fighter_ChangeMotionState(gobj, state, 0, 0.0f, 1.0f, 0.0f, NULL);
}
static void ftSh_SpecialS_DashEnter(HSD_GObj* gobj)
{
    Fighter* fp = GET_FIGHTER(gobj);
    ftSh_DatAttrs* da = ftSh_DA(fp);
    int state;
    float ratio;
    if (ftSh_FV(fp)->specials_charge >= da->specials_max_charge) {
        state = fp->ground_or_air == GA_Ground ? ftSh_MS_SpecialSMax : ftSh_MS_SpecialAirSMax;
    } else {
        state = fp->ground_or_air == GA_Ground ? ftSh_MS_SpecialS : ftSh_MS_SpecialAirS;
    }
    ftSh_SpecialS_State(gobj, state);
    ratio = (float) ftSh_FV(fp)->specials_charge / (float) da->specials_max_charge;
    fp->self_vel.y = 0.0f;
    ftSh_SpecialS_Callbacks(fp, true);
    ftSh_FV(fp)->specials_charge = 0;
    fp->cmd_vars[0] = 0;
    ftSh_MV(fp)->specials.charge_ratio = ratio;
    fp->self_vel.x = fp->facing_dir * MU_FMADDS(ratio,
        da->specials_max_speed - da->specials_min_speed, da->specials_min_speed);
}
void ftSh_SpecialS_Enter(HSD_GObj* gobj)
{
    Fighter* fp = GET_FIGHTER(gobj);
    if (ftSh_FV(fp)->specials_charge == ftSh_DA(fp)->specials_max_charge) {
        ftSh_SpecialS_DashEnter(gobj);
    } else {
        if (fp->ground_or_air == GA_Ground) {
            ftSh_SpecialS_State(gobj, ftSh_MS_SpecialSStart);
            fp->self_vel.y = 0.0f;
        } else {
            ftSh_SpecialS_State(gobj, ftSh_MS_SpecialAirSStart);
        }
        ftSh_SpecialS_Callbacks(fp, false);
        ftSh_MV(fp)->specials.dust_timer = 0;
        ftSh_MV(fp)->specials.hold_frames = 0;
    }
    fp->cmd_vars[0] = fp->cmd_vars[1] = fp->cmd_vars[2] = fp->cmd_vars[3] = 0;
}

void ftSh_SpecialSStart_Anim(HSD_GObj* gobj)
{
    if (!ftAnim_IsFramesRemaining(gobj)) {
        ftSh_SpecialS_State(gobj, ftSh_MS_SpecialSHold);
        ftSh_SpecialS_Callbacks(GET_FIGHTER(gobj), false);
    }
}
static void ftSh_SpecialS_StartSwap(HSD_GObj* gobj, bool air)
{
    Fighter* fp = GET_FIGHTER(gobj);
    Fighter_ChangeMotionState(gobj, air ? ftSh_MS_SpecialAirSStart : ftSh_MS_SpecialSStart,
                              FTSH_MF_SWAP_B, fp->cur_anim_frame, 1.0f, 0.0f, NULL);
    if (air) {
        ftCommon_8007D5D4(fp);
    } else {
        ftCommon_8007D7FC(fp);
        fp->self_vel.y = 0.0f;
    }
    ftSh_SpecialS_Callbacks(fp, false);
}
void ftSh_SpecialSStart_Coll(HSD_GObj* gobj)
{
    if (!ft_80082708(gobj)) { ftSh_SpecialS_StartSwap(gobj, true); }
}
void ftSh_SpecialAirSStart_Coll(HSD_GObj* gobj)
{
    if (ft_80081D0C(gobj) == 1) { ftSh_SpecialS_StartSwap(gobj, false); }
}

static void ftSh_SpecialS_EndSwap(HSD_GObj* gobj, bool air)
{
    Fighter* fp = GET_FIGHTER(gobj);
    Fighter_ChangeMotionState(gobj, air ? ftSh_MS_SpecialAirSEnd : ftSh_MS_SpecialSEnd,
                              FTSH_MF_SWAP_B, fp->cur_anim_frame, 1.0f, 0.0f, NULL);
    if (air) { ftCommon_8007D5D4(fp); } else { ftCommon_8007D7FC(fp); }
}
static void ftSh_SpecialS_EndAnim(HSD_GObj* gobj, bool air)
{
    Fighter* fp = GET_FIGHTER(gobj);
    if (!ftAnim_IsFramesRemaining(gobj)) {
        fp->take_dmg_2_cb = NULL;
        fp->pre_hitlag_cb = NULL;
        fp->post_hitlag_cb = NULL;
        if (air) { ftCo_Fall_Enter(gobj); } else { ft_8008A2BC(gobj); }
    }
}
void ftSh_SpecialSEnd_Anim(HSD_GObj* gobj) { ftSh_SpecialS_EndAnim(gobj, false); }
void ftSh_SpecialAirSEnd_Anim(HSD_GObj* gobj) { ftSh_SpecialS_EndAnim(gobj, true); }
void ftSh_SpecialSEnd_Coll(HSD_GObj* gobj)
{
    if (!ft_80082708(gobj)) { ftSh_SpecialS_EndSwap(gobj, true); }
}
void ftSh_SpecialAirSEnd_Coll(HSD_GObj* gobj)
{
    if (ft_80081D0C(gobj) == 1) { ftSh_SpecialS_EndSwap(gobj, false); }
}
void ftSh_SpecialAirSEnd_Phys(HSD_GObj* gobj)
{
    Fighter* fp = GET_FIGHTER(gobj);
    ftSh_DatAttrs* da = ftSh_DA(fp);
    ftCommon_Fall(fp, da->specials_hold_gravity, da->specials_hold_terminal_vel);
    ftCommon_CalcSelfAccel_Deaccel(fp, da->specials_hold_air_decel);
}

/* +49E4: mark the effect flag even if the dust is not due or spawning fails. */
static void ftSh_SpecialS_Dust(HSD_GObj* gobj)
{
    Fighter* fp = GET_FIGHTER(gobj);
    ftSh_SpecialSVars* mv = &ftSh_MV(fp)->specials;
    Vec3 pos;
    EF_Effect* effect;
    fp->x2219_b0 = true;
    if (mv->dust_timer > 0) {
        mv->dust_timer--;
        return;
    }
    lb_8000B1CC(fp->parts[0].joint, NULL, &pos);
    effect = efSync_Spawn(ftSh_Ef_Dust, gobj, &pos);
    if (effect != NULL && effect->gobj != NULL && effect->gobj->hsd_obj != NULL) {
        HSD_JObj* joint = effect->gobj->hsd_obj;
        double turn = HSD_Randf() * FTSH_PI;
        joint->rotate.y = (float) (turn + turn);
        joint->rotate.z = HSD_Randf() - 0.5f;
        if (fp->ground_or_air == GA_Air) {
            joint->translate.y -= 7.0f;
        }
    }
    mv->dust_timer = ftSh_DA(fp)->specials_dust_interval;
}
void ftSh_SpecialSHold_Anim(HSD_GObj* gobj)
{
    Fighter* fp = GET_FIGHTER(gobj);
    if (ftSh_FV(fp)->specials_charge >= ftSh_DA(fp)->specials_max_charge) {
        ftSh_FV(fp)->specials_charge = ftSh_DA(fp)->specials_max_charge;
        ft_800881D8(fp, ftSh_Sfx_SChargeFull, 0x7F, 0x40);
        ftSh_SpecialS_State(gobj, fp->ground_or_air == GA_Air
                                    ? ftSh_MS_SpecialAirSEnd : ftSh_MS_SpecialSEnd);
    } else {
        ftSh_SpecialS_Dust(gobj);
        ftSh_FV(fp)->specials_charge++;
    }
}
void ftSh_SpecialSHold_IASA(HSD_GObj* gobj)
{
    Fighter* fp = GET_FIGHTER(gobj);
    ftSh_SpecialSVars* mv = &ftSh_MV(fp)->specials;
    if (mv->hold_frames >= ftSh_DA(fp)->specials_min_hold_frames) {
        HSD_Pad pressed = fp->input.pressed_buttons;
        if (pressed & (HSD_PAD_L | HSD_PAD_R | HSD_PAD_Z)) {
            ftSh_FV(fp)->specials_charge = 0;
            ftSh_SpecialS_State(gobj, fp->ground_or_air == GA_Air
                                        ? ftSh_MS_SpecialAirSEnd : ftSh_MS_SpecialSEnd);
        } else if (pressed & HSD_PAD_B) {
            ftSh_SpecialS_DashEnter(gobj);
        }
    }
    mv->hold_frames++;
}
void ftSh_SpecialSHold_Phys(HSD_GObj* gobj)
{
    if (GET_FIGHTER(gobj)->ground_or_air == GA_Air) {
        ftSh_SpecialAirSEnd_Phys(gobj);
    } else {
        ft_80084F3C(gobj);
    }
}
void ftSh_SpecialSHold_Coll(HSD_GObj* gobj)
{
    Fighter* fp = GET_FIGHTER(gobj);
    if (fp->ground_or_air == GA_Ground) {
        if (!ft_80082708(gobj)) {
            ftCommon_8007D5D4(fp);
            return;
        }
    }
    if (fp->ground_or_air == GA_Air) {
        ftCollisionBox box = ftSh_CollBox(fp);
        if (ft_800824A0(gobj, &box) == 1) {
            ftCommon_8007D7FC(fp);
        }
    }
}
void ftSh_SpecialS_Anim(HSD_GObj* gobj)
{
    if (!ftAnim_IsFramesRemaining(gobj)) {
        GET_FIGHTER(gobj)->gr_vel = 0.0f;
        ft_8008A2BC(gobj);
    }
}
void ftSh_SpecialS_Phys(HSD_GObj* gobj)
{
    Fighter* fp = GET_FIGHTER(gobj);
    float vel = (1.0f - ftSh_DA(fp)->specials_friction) * fp->self_vel.x;
    float abs_vel = vel < 0.0f ? -vel : vel;
    if (abs_vel < 0.5f) { vel = 0.0f; }
    fp->self_vel.x = vel;
}
static void ftSh_SpecialS_DashSwap(HSD_GObj* gobj, bool air)
{
    Fighter* fp = GET_FIGHTER(gobj);
    Fighter_ChangeMotionState(gobj, air ? ftSh_MS_SpecialAirS : ftSh_MS_SpecialS,
                              FTSH_MF_SWAP_B, fp->cur_anim_frame, 1.0f, 0.0f, NULL);
    if (air) { ftCommon_8007D5D4(fp); } else { ftCommon_8007D7FC(fp); }
    ftSh_SpecialS_Callbacks(fp, true);
    if (!air) { fp->cmd_vars[0] = 0; }
}
void ftSh_SpecialS_Coll(HSD_GObj* gobj)
{
    Fighter* fp = GET_FIGHTER(gobj);
    if (fp->cmd_vars[0] == 0) {
        if (!ft_80082708(gobj)) {
            ftSh_SpecialS_DashSwap(gobj, true);
        }
    }
    if (fp->cmd_vars[0] == 1) {
        if (!ft_800827A0(gobj)) { ftSh_SpecialS_DashSwap(gobj, true); }
    }
}
void ftSh_SpecialAirS_Anim(HSD_GObj* gobj)
{
    ftSh_DatAttrs* da = ftSh_DA(GET_FIGHTER(gobj));
    if (!ftAnim_IsFramesRemaining(gobj)) {
        if (da->specials_landing_lag == 0.0f) {
            ftCo_Fall_Enter(gobj);
        } else {
            ftCo_80096900(gobj, 1, 1, false, da->specials_fall_mobility, da->specials_landing_lag);
        }
    }
}
void ftSh_SpecialAirS_Phys(HSD_GObj* gobj)
{
    Fighter* fp = GET_FIGHTER(gobj);
    ftSh_DatAttrs* da = ftSh_DA(fp);
    ftCommon_Fall(fp, da->specials_air_gravity, da->specials_air_terminal_vel);
    ftSh_SpecialS_Phys(gobj);
}
void ftSh_SpecialAirS_Coll(HSD_GObj* gobj)
{
    if (ft_80081D0C(gobj) == 1) { ftSh_SpecialS_DashSwap(gobj, false); }
}
