/* PlSh.dat +0734/+07D8, +2964..+388C, +4DC0..+55A8 and +5970..+5A64.
 * The roll's digital stick bits, single shared run/dive timer and direct state changes are
 * the file's behavior. No retail or other fighter's down special is called. */
#include "shadow.h"

#include <melee/ef/eflib.h>
#include <melee/ef/efsync.h>
#include <melee/ft/fighter.h>
#include <melee/ft/ft_081B.h>
#include <melee/ft/ft_084E.h>
#include <melee/ft/ft_0881.h>
#include <melee/ft/ft_0892.h>
#include <melee/ft/ftanim.h>
#include <melee/ft/ftcommon.h>
#include <melee/ft/inlines.h>
#include <melee/ft/kinds/ftCommon/ftCo_AttackAir.h>
#include <melee/ft/kinds/ftCommon/ftCo_Fall.h>
#include <melee/ft/kinds/ftCommon/ftCo_FallAerial.h>
#include <melee/ft/kinds/ftCommon/ftCo_FallSpecial.h>
#include <melee/ft/kinds/ftCommon/ftCo_Jump.h>
#include <melee/ft/kinds/ftCommon/ftCo_Landing.h>
#include <melee/ft/kinds/ftKirby/ftkirby.h>
#include <melee/lb/lb_00B0.h>
#include <melee/lb/types.h>
#include <melee/mp/mpcoll.h>
#include <sysdolphin/baselib/controller.h>

static void ftSh_Lw_Callbacks(Fighter* fp)
{
    fp->take_dmg_cb = ftSh_DestroyAll;
    fp->death2_cb = ftSh_DestroyAll;
    fp->take_dmg_2_cb = efLib_DestroyAll;
    fp->pre_hitlag_cb = efLib_PauseAll;
    fp->post_hitlag_cb = efLib_ResumeAll;
}
static void ftSh_Lw_State(HSD_GObj* gobj, int state, float frame)
{
    Fighter_ChangeMotionState(gobj, state, 0, frame, 1.0f, 0.0f, NULL);
}
static void ftSh_Lw_End(HSD_GObj* gobj)
{
    Fighter* fp = GET_FIGHTER(gobj);
    ftSh_Lw_State(gobj, fp->ground_or_air == GA_Air
                           ? ftSh_MS_SpecialAirLwEnd : ftSh_MS_SpecialLwEnd, 0.0f);
    ftSh_Lw_Callbacks(fp);
}
static void ftSh_Lw_Damage(Fighter* fp, float min, float max)
{
    if (fp->x914[0].state != HitCapsule_Disabled) {
        float ratio = (float) ftSh_MV(fp)->speciallw.level /
                      (float) ftSh_DA(fp)->speciallw_max_charge_level;
        fp->x914[0].damage = MU_FMADDS(max - min, ratio, min);
    }
}
static void ftSh_Lw_KeepTrail(Fighter* fp, HSD_GObjEvent prev)
{
    fp->accessory4_cb = prev == ftSh_GFXTrailLoop ? prev : ftSh_GFXTrail;
}
static void ftSh_Lw_RunEnter(HSD_GObj* gobj)
{
    Fighter* fp = GET_FIGHTER(gobj);
    ftSh_DatAttrs* da = ftSh_DA(fp);
    ftSh_SpecialLwVars* mv = &ftSh_MV(fp)->speciallw;
    float ratio;
    ftSh_Lw_State(gobj, ftSh_MS_SpecialLwRun, 0.0f);
    ratio = (float) mv->level / (float) da->speciallw_max_charge_level;
    mv->run_life = da->speciallw_run_frames;
    mv->run_speed = MU_FMADDS(da->speciallw_run_speed_max - da->speciallw_run_speed_min,
                              ratio, da->speciallw_run_speed_min);
    ftSh_Lw_Damage(fp, da->speciallw_run_damage_min, da->speciallw_run_damage_max);
    ftSh_Lw_Callbacks(fp);
    fp->accessory4_cb = ftSh_GFXTrail;
}
static void ftSh_Lw_RunTrans(HSD_GObj* gobj)
{
    Fighter* fp = GET_FIGHTER(gobj);
    ftSh_DatAttrs* da = ftSh_DA(fp);
    HSD_GObjEvent prev = fp->accessory4_cb;
    ftCommon_8007D7FC(fp);
    ftSh_Lw_State(gobj, ftSh_MS_SpecialLwRun, 0.0f);
    ftSh_Lw_Damage(fp, da->speciallw_run_damage_min, da->speciallw_run_damage_max);
    ftSh_Lw_Callbacks(fp);
    ftSh_Lw_KeepTrail(fp, prev);
}
static void ftSh_Lw_JumpTrans(HSD_GObj* gobj, bool left_ground)
{
    Fighter* fp = GET_FIGHTER(gobj);
    ftSh_DatAttrs* da = ftSh_DA(fp);
    HSD_GObjEvent prev = fp->accessory4_cb;
    ftCommon_8007D5D4(fp);
    ftSh_Lw_State(gobj, ftSh_MS_SpecialLwRunJump, 0.0f);
    ftSh_MV(fp)->speciallw.jump_lockout = da->speciallw_jump_lockout;
    ftSh_MV(fp)->speciallw.left_ground = left_ground;
    ftSh_Lw_Damage(fp, da->speciallw_jump_damage_min, da->speciallw_jump_damage_max);
    ftSh_Lw_Callbacks(fp);
    ftSh_Lw_KeepTrail(fp, prev);
}
static void ftSh_Lw_DiveEnter(HSD_GObj* gobj)
{
    Fighter* fp = GET_FIGHTER(gobj);
    ftSh_Lw_State(gobj, ftSh_MS_SpecialLwDive, 0.0f);
    fp->self_vel.x = ftSh_DA(fp)->speciallw_dive_vel_x * fp->facing_dir;
    ftSh_MV(fp)->speciallw.run_life = 40;
    ftSh_Lw_Callbacks(fp);
    fp->accessory4_cb = ftSh_GFXTrail;
}
void ftSh_SpecialLw_Enter(HSD_GObj* gobj)
{
    Fighter* fp = GET_FIGHTER(gobj);
    ftSh_Lw_State(gobj, fp->ground_or_air == GA_Air
                           ? ftSh_MS_SpecialAirLwStart : ftSh_MS_SpecialLwStart, 0.0f);
    ftSh_Lw_Callbacks(fp);
    ftSh_MV(fp)->speciallw.left_ground = 0;
}
static void ftSh_Lw_StartAnim(HSD_GObj* gobj)
{
    if (!ftAnim_IsFramesRemaining(gobj)) {
        Fighter* fp = GET_FIGHTER(gobj);
        ftSh_DatAttrs* da = ftSh_DA(fp);
        ftSh_SpecialLwVars* mv = &ftSh_MV(fp)->speciallw;
        ftSh_Lw_State(gobj, ftSh_MS_SpecialLwCharge, 0.0f);
        ft_80088478(fp, ftSh_Sfx_LwCharge, 0xFF, 0x40);
        mv->sfx_timer = 30;
        mv->release_timer = da->speciallw_release_frames;
        mv->charge_timer = da->speciallw_first_charge_delay;
        mv->level = -1;
        mv->fx_timer = 0;
        ftSh_Lw_Callbacks(fp);
    }
}
void ftSh_SpecialLwStart_Anim(HSD_GObj* gobj) { ftSh_Lw_StartAnim(gobj); }
void ftSh_SpecialAirLwStart_Anim(HSD_GObj* gobj) { ftSh_Lw_StartAnim(gobj); }
void ftSh_SpecialLwEnd_Anim(HSD_GObj* gobj)
{
    if (!ftAnim_IsFramesRemaining(gobj)) { ft_8008A2BC(gobj); }
}
void ftSh_SpecialAirLwEnd_Anim(HSD_GObj* gobj)
{
    if (!ftAnim_IsFramesRemaining(gobj)) { ftCo_FallAerial_Enter(gobj); }
}
static void ftSh_Lw_StartSwap(HSD_GObj* gobj, bool air)
{
    Fighter* fp = GET_FIGHTER(gobj);
    if (air) { ftCommon_8007D5D4(fp); } else { ftCommon_8007D7FC(fp); }
    ftSh_Lw_State(gobj, air ? ftSh_MS_SpecialAirLwStart : ftSh_MS_SpecialLwStart,
                  fp->cur_anim_frame);
    ftSh_Lw_Callbacks(fp);
}
void ftSh_SpecialLwStart_Coll(HSD_GObj* gobj)
{
    if (!ft_80082708(gobj)) { ftSh_Lw_StartSwap(gobj, true); }
}
void ftSh_SpecialAirLwStart_Coll(HSD_GObj* gobj)
{
    ftCollisionBox box = ftSh_CollBox(GET_FIGHTER(gobj));
    if (ft_800824A0(gobj, &box) == 1) { ftSh_Lw_StartSwap(gobj, false); }
}
void ftSh_SpecialLwEnd_Coll(HSD_GObj* gobj)
{
    if (!ft_80082708(gobj)) {
        Fighter* fp = GET_FIGHTER(gobj);
        ftSh_Lw_State(gobj, ftSh_MS_SpecialAirLwEnd, fp->cur_anim_frame);
        ftSh_Lw_Callbacks(fp);
    }
}
void ftSh_SpecialAirLwEnd_Coll(HSD_GObj* gobj)
{
    Fighter* fp = GET_FIGHTER(gobj);
    ftCollisionBox box = ftSh_CollBox(fp);
    if (ft_800824A0(gobj, &box) == 1) {
        ftSh_Lw_State(gobj, ftSh_MS_SpecialLwEnd, fp->cur_anim_frame);
        ftSh_Lw_Callbacks(fp);
    }
}
void ftSh_SpecialLwCharge_Anim(HSD_GObj* gobj)
{
    Fighter* fp = GET_FIGHTER(gobj);
    ftSh_DatAttrs* da = ftSh_DA(fp);
    ftSh_SpecialLwVars* mv = &ftSh_MV(fp)->speciallw;
    if (mv->sfx_timer > 0) { mv->sfx_timer--; }
    else {
        ft_80088478(fp, ftSh_Sfx_LwLoop, 0xFF, 0x40);
        mv->sfx_timer = da->speciallw_sfx_interval;
    }
    if (mv->release_timer <= 0) {
        ftSh_Lw_End(gobj);
        return;
    }
    mv->release_timer--;
    if (mv->fx_timer > 0) { mv->fx_timer--; }
    else {
        if ((unsigned) mv->level <= 2) {
            Vec3 pos;
            lb_8000B1CC(fp->parts[2].joint, NULL, &pos);
            efSync_Spawn(ftSh_Ef_LwLevel0 + mv->level, gobj, &pos);
        }
        mv->fx_timer = da->speciallw_effect_interval;
    }
}
void ftSh_SpecialLwCharge_IASA(HSD_GObj* gobj)
{
    Fighter* fp = GET_FIGHTER(gobj);
    ftSh_DatAttrs* da = ftSh_DA(fp);
    ftSh_SpecialLwVars* mv = &ftSh_MV(fp)->speciallw;
    if (fp->ground_or_air == GA_Ground && ftCo_Jump_CheckInput(gobj) == 1) {
        ftSh_Lw_JumpTrans(gobj, false);
        fp->self_vel.y = da->speciallw_jump_vel_y;
        fp->x914[0].state = HitCapsule_Disabled;
        ft_80088478(fp, ftSh_Sfx_Jump, 0xFF, 0x40);
    } else if (!(fp->input.held_buttons[0] & 0x20000)) {
        if (mv->level < 0) { ftSh_Lw_End(gobj); }
        else if (fp->ground_or_air == GA_Air) { ftSh_Lw_DiveEnter(gobj); }
        else { ftSh_Lw_RunEnter(gobj); }
    } else if (fp->input.pressed_buttons & HSD_PAD_B) {
        mv->release_timer = da->speciallw_release_frames;
        if (mv->charge_timer == 0) {
            if (mv->level < da->speciallw_max_charge_level) { mv->level++; }
            mv->charge_timer = da->speciallw_charge_interval;
        }
    }
    if (mv->charge_timer > 0) { mv->charge_timer--; }
    else if (mv->level == -1) { mv->level = 0; }
}
void ftSh_SpecialLwCharge_Phys(HSD_GObj* gobj)
{
    Fighter* fp = GET_FIGHTER(gobj);
    ftSh_DatAttrs* da = ftSh_DA(fp);
    if (fp->ground_or_air == GA_Air) {
        ftCommon_Fall(fp, da->speciallw_charge_gravity, da->speciallw_charge_terminal_vel);
        ftCommon_CalcSelfAccel_Deaccel(fp, da->speciallw_charge_air_decel);
    } else { ft_80084F3C(gobj); }
}
void ftSh_SpecialLwCharge_Coll(HSD_GObj* gobj)
{
    Fighter* fp = GET_FIGHTER(gobj);
    if (fp->ground_or_air == GA_Ground) {
        if (!ft_80082708(gobj)) { ftCommon_8007D5D4(fp); return; }
    }
    if (fp->ground_or_air == GA_Air) {
        ftCollisionBox box = ftSh_CollBox(fp);
        if (ft_800824A0(gobj, &box) == 1) { ftCommon_8007D7FC(fp); }
    }
}
void ftSh_SpecialLwRun_Anim(HSD_GObj* gobj)
{
    Fighter* fp = GET_FIGHTER(gobj);
    if (ftSh_MV(fp)->speciallw.run_life > 0) {
        ftSh_MV(fp)->speciallw.run_life--;
    } else {
        ftSh_Lw_State(gobj, ftSh_MS_SpecialLwRunBrake, 0.0f);
        if (ftSh_MV(fp)->speciallw.run_life > 0) { ftSh_MV(fp)->speciallw.run_life--; }
    }
}
void ftSh_SpecialLwRun_IASA(HSD_GObj* gobj)
{
    Fighter* fp = GET_FIGHTER(gobj);
    if (ftCo_Jump_GetInput(gobj) != JumpInput_None || (fp->input.held_buttons[0] & HSD_PAD_XY)) {
        ftSh_Lw_JumpTrans(gobj, false);
        fp->self_vel.y = ftSh_DA(fp)->speciallw_jump_vel_y;
        ft_80088478(fp, ftSh_Sfx_Jump, 0xFF, 0x40);
    }
}
static void ftSh_Lw_TurnTrans(HSD_GObj* gobj)
{
    Fighter* fp = GET_FIGHTER(gobj);
    fp->facing_dir = -fp->facing_dir;
    ftCommon_8007D7FC(fp);
    ftSh_Lw_State(gobj, ftSh_MS_SpecialLwRunTurn, 0.0f);
    ftSh_Lw_Callbacks(fp);
}
void ftSh_SpecialLwRun_Phys(HSD_GObj* gobj)
{
    Fighter* fp = GET_FIGHTER(gobj);
    ftCommon_CalcGroundAccel_DashRun(fp, fp->facing_dir * ftSh_DA(fp)->speciallw_run_accel,
                                    fp->facing_dir * ftSh_MV(fp)->speciallw.run_speed,
                                    fp->co_attrs.ground_friction);
    ftCommon_SetSelfMovementFromGroundedMovement(gobj);
    if ((fp->facing_dir == 1.0f && (fp->input.held_buttons[0] & 0x40000)) ||
        (fp->facing_dir == -1.0f && (fp->input.held_buttons[0] & 0x80000))) {
        ftSh_Lw_TurnTrans(gobj);
    }
}
static void ftSh_Lw_StopWall(HSD_GObj* gobj)
{
    Fighter* fp = GET_FIGHTER(gobj);
    Vec3 edge = { fp->coll_data.ecb.left.x, fp->coll_data.ecb.left.y, 0.0f };
    ftKb_SpecialN_800F1F1C(gobj, &edge);
    Fighter_ChangeMotionState(gobj, fp->facing_dir == -1.0f
                                       ? ftSh_MS_SpecialLwStopWallL : ftSh_MS_SpecialLwStopWallR,
                              0, 0.0f, ftSh_DA(fp)->speciallw_stopwall_anim_rate, 0.0f, NULL);
    fp->cur_pos.x = MU_FMADDS(fp->x68C_transNPos.z, fp->facing_dir, fp->cur_pos.x + edge.x);
    ft_800843FC(gobj);
    ftCommon_8007E2FC(gobj);
}
void ftSh_SpecialLwRun_Coll(HSD_GObj* gobj)
{
    Fighter* fp = GET_FIGHTER(gobj);
    CollData* coll = &fp->coll_data;
    bool grounded;
    coll->last_pos = coll->cur_pos;
    coll->cur_pos = fp->cur_pos;
    grounded = mpColl_8004B108(coll);
    fp->cur_pos = coll->cur_pos;
    if (!grounded) { ftSh_Lw_JumpTrans(gobj, true); }
    else if ((fp->facing_dir == -1.0f && (coll->env_flags & 0x800)) ||
             (fp->facing_dir == 1.0f && (coll->env_flags & 0x20))) {
        float speed = fp->gr_vel;
        if (speed < 0.0f) { speed = -speed; }
        if (fp->co_attrs.walk_max_vel < speed) { ftSh_Lw_StopWall(gobj); }
    }
}
void ftSh_SpecialLwRunTurn_IASA(HSD_GObj* gobj)
{
    ftSh_SpecialLwVars* mv = &ftSh_MV(GET_FIGHTER(gobj))->speciallw;
    if (mv->run_life > 0) { mv->run_life--; }
}
void ftSh_SpecialLwRunTurn_Phys(HSD_GObj* gobj)
{
    Fighter* fp = GET_FIGHTER(gobj);
    fp->self_vel.x = MU_FMADDS(fp->facing_dir, ftSh_DA(fp)->speciallw_turn_accel, fp->self_vel.x);
}
void ftSh_SpecialLwRunTurn_Coll(HSD_GObj* gobj)
{
    Fighter* fp = GET_FIGHTER(gobj);
    float speed = ftSh_DA(fp)->speciallw_turn_exit_speed;
    if (!ft_800827A0(gobj)) { ftSh_Lw_JumpTrans(gobj, true); }
    else if ((fp->facing_dir == 1.0f && fp->self_vel.x > speed) ||
             (fp->facing_dir == -1.0f && fp->self_vel.x < -speed)) {
        ftSh_Lw_RunTrans(gobj);
    }
}
void ftSh_SpecialLwRunJump_IASA(HSD_GObj* gobj)
{
    ftSh_SpecialLwVars* mv = &ftSh_MV(GET_FIGHTER(gobj))->speciallw;
    if (mv->jump_lockout > 0) {
        mv->jump_lockout--;
        ftCo_AttackAir_CheckItemThrowInput(gobj);
    } else { ftCo_Fall_IASA_Inner(gobj); }
}
void ftSh_SpecialLwRunJump_Phys(HSD_GObj* gobj)
{
    Fighter* fp = GET_FIGHTER(gobj);
    ftSh_DatAttrs* da = ftSh_DA(fp);
    float vel;
    ftCommon_Fall(fp, da->speciallw_air_gravity, da->speciallw_air_terminal_vel);
    ftCommon_CheckFallFast(fp);
    vel = MU_FMADDS(da->speciallw_air_drift, fp->input.lstick[0].x, fp->self_vel.x);
    if (vel < -da->speciallw_air_max_vel_x) { vel = -da->speciallw_air_max_vel_x; }
    if (da->speciallw_air_max_vel_x < vel) { vel = da->speciallw_air_max_vel_x; }
    fp->self_vel.x = vel;
}
void ftSh_SpecialLwRunJump_Coll(HSD_GObj* gobj)
{
    Fighter* fp = GET_FIGHTER(gobj);
    ftCollisionBox box = ftSh_CollBox(fp);
    if (ft_800824A0(gobj, &box) == 1) {
        if (ftSh_MV(fp)->speciallw.left_ground != 0) { ftSh_Lw_RunTrans(gobj); }
        else if (ftSh_DA(fp)->speciallw_landing_lag == 0.0f) { ft_80082B1C(gobj); }
        else { ftCo_LandingFallSpecial_Enter(gobj, false, ftSh_DA(fp)->speciallw_landing_lag); }
    }
}
void ftSh_SpecialLwDive_Anim(HSD_GObj* gobj)
{
    Fighter* fp = GET_FIGHTER(gobj);
    ftSh_SpecialLwVars* mv = &ftSh_MV(fp)->speciallw;
    if (mv->run_life > 0 && --mv->run_life != 0) { return; }
    ftCo_80096900(gobj, 0, 1, false, ftSh_DA(fp)->speciallw_air_drift,
                  ftSh_DA(fp)->speciallw_landing_lag);
}
void ftSh_SpecialLwDive_Phys(HSD_GObj* gobj)
{
    Fighter* fp = GET_FIGHTER(gobj);
    ftCommon_Fall(fp, ftSh_DA(fp)->speciallw_air_gravity, ftSh_DA(fp)->speciallw_air_terminal_vel);
}
void ftSh_SpecialLwDive_Coll(HSD_GObj* gobj)
{
    Fighter* fp = GET_FIGHTER(gobj);
    ftCollisionBox box = ftSh_CollBox(fp);
    if (ft_800824A0(gobj, &box) == 1) {
        ftCommon_8007D6A4(fp);
        ftSh_Lw_RunEnter(gobj);
    }
}
void ftSh_SpecialLwRunBrake_Anim(HSD_GObj* gobj)
{
    if (!ftAnim_IsFramesRemaining(gobj)) { ft_8008A2BC(gobj); }
}
void ftSh_SpecialLwRunBrake_Phys(HSD_GObj* gobj)
{
    Fighter* fp = GET_FIGHTER(gobj);
    ftCommon_CalcGroundAccel_Deaccel(fp, ftSh_DA(fp)->speciallw_brake_decel);
    ftCommon_SetSelfMovementFromGroundedMovement(gobj);
}
void ftSh_SpecialLwRunBrake_Coll(HSD_GObj* gobj) { ft_800827A0(gobj); }
void ftSh_SpecialLwStopWall_Anim(HSD_GObj* gobj)
{
    if (!ftAnim_IsFramesRemaining(gobj)) { ft_8008A2BC(gobj); }
}
void ftSh_SpecialLwStopWall_Coll(HSD_GObj* gobj) { ft_800843FC(gobj); }
