/* ACE Shadow copy ability, native from PlKbCpSh.dat kbFunction.
 * Exports +0000, +00CC, +0138, +013C, +0140 and +0144. The nine states
 * use the hat attributes at +0C, its dynamics at +10 and Kirby state changes.
 * Instruction comparison is recorded in kirby_identity.txt; callback bodies
 * shared with the fighter have the same operations and no hat-dependent reads.
 * The attack search and trail are the fighter helpers with explicit arguments.
 * The fixed trail color is FFAB03FF, from hat code +18B4/+18B8.
 * No articles, init-items or per-frame export exists in this hat.
 */

#include "shadow.h"

#include <math.h>

#include <dolphin/os.h>

#include <melee/ef/eflib.h>
#include <melee/ef/efsync.h>
#include <melee/ef/types.h>
#include <melee/ft/fighter.h>
#include <melee/ft/ft_081B.h>
#include <melee/ft/ft_0892.h>
#include <melee/ft/ftanim.h>
#include <melee/ft/ftcamera.h>
#include <melee/ft/ftcliffcommon.h>
#include <melee/ft/ftcommon.h>
#include <melee/ft/ftdynamics.h>
#include <melee/ft/inlines.h>
#include <melee/ft/kinds/ftCommon/ftCo_Fall.h>
#include <melee/ft/kinds/ftCommon/types.h>
#include <melee/ft/kinds/ftKirby/ftkirby.h>
#include <melee/ft/types.h>
#include <melee/lb/lb_00B0.h>
#include <melee/lb/lb_00F9.h>
#include <melee/lb/lbspdisplay.h>
#include <melee/lb/types.h>
#include <melee/mp/forward.h>
#include <sysdolphin/baselib/controller.h>
#include <sysdolphin/baselib/dobj.h>
#include <sysdolphin/baselib/gobj.h>
#include <sysdolphin/baselib/jobj.h>
#include <sysdolphin/baselib/mobj.h>
#include <sysdolphin/baselib/tobj.h>

#include "../../akaneia/common/mu_ak_kirby.h"

enum {
    ftKbSh_State_SpecialNStart,      
    ftKbSh_State_SpecialAirNStart,   
    ftKbSh_State_SpecialNCharge,     
    ftKbSh_State_SpecialNAttackMiss, 
    ftKbSh_State_SpecialNAttack,     
    ftKbSh_State_SpecialNCancel,     
    ftKbSh_State_SpecialNLanding,    
    ftKbSh_State_SpecialNRebound,    
    ftKbSh_State_SpecialNHit,        
    ftKbSh_State_Count
};

#define ftKbSh_Ef_Spin 5002

static const GXColor ftKbSh_TrailColor = { 0xFF, 0xAB, 0x03, 0xFF };

#define FTKBSH_LOG 8 

typedef struct ftKbSh_FighterVars {
     Vec3 trail_pos; 
     float trail_angle;
} ftKbSh_FighterVars;

_Static_assert(sizeof(ftKbSh_FighterVars) <= sizeof(CostumeTObjList),
               "Kirby's Shadow trail fields must fit the hat texture list they overlay");

static inline ftKbSh_FighterVars* ftKbSh_FV(Fighter* fp)
{
    return (ftKbSh_FighterVars*) &fp->u.kb.x44;
}

static ftSh_DatAttrs* ftKbSh_DA(Fighter* fp)
{
    KirbyHatStruct* hat = mu_ak_kirby_hat(fp->u.kb.hat.kind);

    if (hat == NULL) {
        return NULL;
    }
    return DISC_GET(ftSh_DatAttrs, hat->hat_dynamics[0]);
}

/* The hat and fighter use the same order of single precision operations. */
MU_NO_CONTRACT static float ftKbSh_DistSquared(const Vec3* a, const Vec3* b)
{
    float dy = a->y - b->y;
    float dx = a->x - b->x;
    float dz = a->z - b->z;
    dy = dy * dy;
    return MU_FMADDS(dz, dz, MU_FMADDS(dx, dx, dy));
}

static void ftKbSh_ClampReboundVel(Fighter* fp, ftSh_DatAttrs* da, float vy)
{
    float vx = fp->self_vel.x;
    float max = da->specialn_rebound_max_vel_x;
    if (vx > max) {
        vx = max;
        fp->self_vel.x = max;
    }
    if (-max > vx) {
        fp->self_vel.x = -max;
    }
    max = da->specialn_rebound_max_vel_y;
    if (vy > max) {
        vy = max;
    }
    fp->self_vel.y = vy;
    fp->cmd_vars[0] = 0;
}

static void ftKbSh_SpecialNCharge_Enter(HSD_GObj* gobj);
static void ftKbSh_SearchTarget_EnterAttack(HSD_GObj* gobj);
static void ftKbSh_SpecialNAttackMiss_Enter(HSD_GObj* gobj);
static void ftKbSh_SpecialNAttack_Enter(HSD_GObj* gobj, float target_x, float target_y);
static void ftKbSh_SpecialNCancel_Enter(HSD_GObj* gobj);
static void ftKbSh_SpecialNLanding_Enter(HSD_GObj* gobj);
static void ftKbSh_SpecialNRebound_Enter(HSD_GObj* gobj, u32 env_flags);
static void ftKbSh_SpecialNRebound_OnEnter(HSD_GObj* gobj);
static void ftKbSh_SpecialNHit_Enter(HSD_GObj* gobj);
static void ftKbSh_GFXSpin(HSD_GObj* gobj);
static void ftKbSh_GFXSpinAndTrail(HSD_GObj* gobj);
static void ftKbSh_GFXSpinAndTrailVelocityDirection(HSD_GObj* gobj);

static void ftKbSh_SetupHatDynamics(Fighter* fp, KirbyHatStruct* hat)
{
    ftDynamics* dynamics = DP(hat->hat_dynamics[1]);
    int i;
    static int logged;

    fp->dynamics_num = dynamics->dynamicsNum;
    if (fp->dynamics_num >= Ft_Dynamics_NumMax) {
        OSReport("[ace] Kirby (Shadow): fighter dynamics num over! (%d); the hat gets none\n",
                 fp->dynamics_num);
        fp->dynamics_num = 0;
        return;
    }
    for (i = 0; i < fp->dynamics_num; i++) {
        BoneDynamicsDescDisc* bone = &DP(dynamics->ftDynamicBones)->array[i];
        HSD_JObj* jobj = NULL;

        lb_80011E24(fp->u.kb.hat.jobj, &jobj, (int) bone->bone_id, -1);
        lb_8000FD48(jobj, &fp->dynamic_bone_sets[i].dyn_desc, bone->dyn_desc.count);
        fp->dynamic_bone_sets[i].bone_id = 0;
        lb_80011710(&bone->dyn_desc, &fp->dynamic_bone_sets[i].dyn_desc);
    }
    if (logged < FTKBSH_LOG) {
        logged++;
        OSReport("[ace] Kirby (Shadow) hat: %d dynamic bones\n", fp->dynamics_num);
    }
}

static void ftKbSh_OnSwallow(HSD_GObj* gobj)
{
    Fighter* fp = GET_FIGHTER(gobj);
    KirbyHatStruct* hat;

    if (fp->u.kb.hat.jobj != NULL) {
        return;
    }
    mu_ak_kirby_attach_hat(gobj);
    hat = mu_ak_kirby_hat(fp->u.kb.hat.kind);
    
    if (hat == NULL || fp->u.kb.hat.jobj == NULL || DISC_NULL(hat->hat_dynamics[1])) {
        return;
    }
    ftKbSh_SetupHatDynamics(fp, hat);
}

static void ftKbSh_OnLose(HSD_GObj* gobj)
{
    Fighter* fp = GET_FIGHTER(gobj);

    ftKb_SpecialN_800EFAF0(gobj);
    ftCo_UnloadDynamicBones(fp);
}

static void ftKbSh_OnHurt(HSD_GObj* gobj) {}

static void ftKbSh_SpawnSpin(HSD_GObj* gobj)
{
    Fighter* fp = GET_FIGHTER(gobj);
    EF_Effect* effect = efSync_Spawn(ftKbSh_Ef_Spin, gobj, fp->parts[FtPart_HipN].joint);
    HSD_JObj* body;
    HSD_JObj* ball;
    HSD_DObj* first;
    HSD_DObj* eighth;
    HSD_DObj* ball_dobj;
    int i;

    if (effect == NULL) {
        return;
    }
    body = GET_JOBJ(gobj);
    ball = effect->gobj->hsd_obj;
    if (body == NULL || ball == NULL || ball->child == NULL || ball->child->child == NULL) {
        return;
    }
    first = body->u.dobj;
    ball_dobj = ball->child->child->u.dobj;
    if (first == NULL || ball_dobj == NULL) {
        return;
    }

    eighth = first;
    for (i = 7; i > 0; i--) {
        if (eighth->next == NULL) {
            OSReport("[ace] Kirby (Shadow) spin ball: dobj not found! (Kirby's model has no eighth "
                     "display object)\n");
            return;
        }
        eighth = eighth->next;
    }

    if (first->mobj != NULL && first->mobj->tobj != NULL) {
        HSD_TObj* src = first->mobj->tobj;

        if (src->imagedesc != NULL && src->tlut != NULL && ball_dobj->mobj != NULL &&
            ball_dobj->mobj->tobj != NULL)
        {
            HSD_TObj* dst = ball_dobj->mobj->tobj;

            if (dst->imagedesc != NULL && dst->tlut != NULL) {
                dst->imagedesc->image_ptr = src->imagedesc->image_ptr;
                dst->tlut->lut = src->tlut->lut;
            }
        }
    }

    if (eighth->mobj != NULL && eighth->mobj->tobj != NULL) {
        HSD_TObj* src = eighth->mobj->tobj;

        if (src->tlut == NULL && src->imagedesc != NULL && ball_dobj->next != NULL &&
            ball_dobj->next->mobj != NULL && ball_dobj->next->mobj->tobj != NULL)
        {
            HSD_TObj* dst = ball_dobj->next->mobj->tobj;

            if (dst->imagedesc != NULL) {
                dst->imagedesc->image_ptr = src->imagedesc->image_ptr;
            }
        }
    }
}

static void ftKbSh_GFXSpin(HSD_GObj* gobj)
{
    Fighter* fp = GET_FIGHTER(gobj);

    if (!fp->x2219_b0) {
        ftKbSh_SpawnSpin(gobj);
        fp->x2219_b0 = true;
        fp->pre_hitlag_cb = efLib_PauseAll;
        fp->post_hitlag_cb = efLib_ResumeAll;
    }
    fp->accessory4_cb = NULL;
}

static void ftKbSh_UpdateTrailPosAndRot(HSD_GObj* gobj)
{
    Fighter* fp = GET_FIGHTER(gobj);
    ftKbSh_FighterVars* fv = ftKbSh_FV(fp);

    lb_8000B1CC(fp->parts[FtPart_XRotN].joint, NULL, &fv->trail_pos);
    fv->trail_angle =
        atan2f(fp->cur_pos.y - fp->prev_pos.y, fp->cur_pos.x - fp->prev_pos.x);
}

static void ftKbSh_GFXTrailLoop(HSD_GObj* gobj)
{
    Fighter* fp = GET_FIGHTER(gobj);
    ftKbSh_FighterVars* fv = ftKbSh_FV(fp);

    ftSh_SpawnTrailEffectAt(gobj, fp->cmd_vars[3] == 1, &fv->trail_pos, &fv->trail_angle,
                            &ftKbSh_TrailColor);
}

static void ftKbSh_GFXTrail(HSD_GObj* gobj)
{
    Fighter* fp = GET_FIGHTER(gobj);

    ftKbSh_UpdateTrailPosAndRot(gobj);
    fp->accessory4_cb = ftKbSh_GFXTrailLoop;
    if (fp->motion_id != ftCo_MS_AttackHi4) {
        fp->cmd_vars[3] = 1;
    }
}

static void ftKbSh_GFXSpinAndTrail(HSD_GObj* gobj)
{
    ftKbSh_GFXSpin(gobj);
    ftKbSh_GFXTrail(gobj);
}

static void ftKbSh_GFXSpinAndTrailVelocityDirection(HSD_GObj* gobj)
{
    Fighter* fp;

    ftKbSh_GFXSpin(gobj);
    ftKbSh_GFXTrail(gobj);
    fp = GET_FIGHTER(gobj);
    ftKbSh_FV(fp)->trail_angle = atan2f(fp->self_vel.y, fp->self_vel.x);
}

static void ftKbSh_SpecialNStart_Enter(HSD_GObj* gobj)
{
    Fighter* fp = GET_FIGHTER(gobj);

    if (fp->ground_or_air == GA_Air) {
        mu_ak_kirby_state_change(gobj, ftKbSh_State_SpecialAirNStart, Ft_MF_None, 0.0F, 1.0F,
                                 0.0F);
    } else {
        ftCommon_8007D5D4(fp);
        mu_ak_kirby_state_change(gobj, ftKbSh_State_SpecialNStart, Ft_MF_None, 0.0F, 1.0F, 0.0F);
    }
    ftAnim_8006EBA4(gobj);
    fp->self_vel.x = 0.0F;
    fp->self_vel.y = 0.0F;
    ftSh_MV(fp)->specialn.charge_frames = 0;
}

static void ftKbSh_SpecialNStart_Anim(HSD_GObj* gobj)
{
    if (!ftAnim_IsFramesRemaining(gobj)) {
        ftKbSh_SpecialNCharge_Enter(gobj);
    }
}

static void ftKbSh_SpecialNCharge_Enter(HSD_GObj* gobj)
{
    Fighter* fp = GET_FIGHTER(gobj);
    ftSh_DatAttrs* da = ftKbSh_DA(fp);

    if (da == NULL) {
        return;
    }
    mu_ak_kirby_state_change(gobj, ftKbSh_State_SpecialNCharge, Ft_MF_None, 0.0F,
                             da->specialn_charge_anim_rate, 0.0F);
    fp->self_vel.y = da->specialn_charge_vel_y;
    fp->accessory4_cb = ftKbSh_GFXSpin;
}

static void ftKbSh_SpecialNCharge_Anim(HSD_GObj* gobj)
{
    Fighter* fp = GET_FIGHTER(gobj);
    ftSh_DatAttrs* da = ftKbSh_DA(fp);
    ftSh_SpecialNVars* mv = &ftSh_MV(fp)->specialn;

    if (da == NULL) {
        return;
    }
    if (mv->charge_frames >= da->specialn_max_charge_frames ||
        (mv->charge_frames >= da->specialn_min_charge_frames &&
         (fp->input.pressed_buttons & HSD_PAD_B)))
    {
        ftKbSh_SearchTarget_EnterAttack(gobj);
        return;
    }
    mv->charge_frames++;
}

static void ftKbSh_SpecialNCharge_Coll(HSD_GObj* gobj)
{
    Fighter* fp = GET_FIGHTER(gobj);
    ftSh_DatAttrs* da = ftKbSh_DA(fp);
    ftCollisionBox box;

    if (da == NULL) {
        return;
    }
    box = ftCollisionBox_FromDisc(&da->coll_box);
    ft_800824A0(gobj, &box);
    if (fp->coll_data.env_flags & Collide_CeilingMask) {
        fp->self_vel.y = 0.0F;
    }
}

static void ftKbSh_SearchTarget_EnterAttack(HSD_GObj* gobj)
{
    Fighter* fp = GET_FIGHTER(gobj);
    ftSh_DatAttrs* da = ftKbSh_DA(fp);
    float target_x;
    float target_y;
    static int logged;

    if (da == NULL) {
        return;
    }
    if (!ftSh_SpecialN_SearchTarget(gobj, da->specialn_search_range,
                                      da->specialn_target_offset_y, &target_x, &target_y)) {
        if (logged < FTKBSH_LOG) {
            logged++;
            OSReport("[ace] Kirby (Shadow) homing attack: no target within %.0f of (%.2f, %.2f), "
                     "dive\n",
                     da->specialn_search_range, fp->cur_pos.x, fp->cur_pos.y);
        }
        ftKbSh_SpecialNAttackMiss_Enter(gobj);
        return;
    }
    if (logged < FTKBSH_LOG) {
        logged++;
        OSReport("[ace] Kirby (Shadow) homing attack: from (%.2f, %.2f) at target (%.2f, %.2f)\n",
                 fp->cur_pos.x, fp->cur_pos.y, target_x, target_y);
    }

    ftKbSh_SpecialNAttack_Enter(gobj,
                                MU_FMADDS(da->specialn_aim_stick_x, fp->input.lstick[0].x, target_x),
                                MU_FMADDS(da->specialn_aim_stick_y, fp->input.lstick[0].y, target_y));
}

static void ftKbSh_SpecialNAttackMiss_Enter(HSD_GObj* gobj)
{
    Fighter* fp = GET_FIGHTER(gobj);
    ftSh_DatAttrs* da = ftKbSh_DA(fp);

    if (da == NULL) {
        return;
    }
    mu_ak_kirby_state_change(gobj, ftKbSh_State_SpecialNAttackMiss, Ft_MF_None, 0.0F,
                             da->specialn_attack_anim_rate, 0.0F);
    fp->self_vel.x = da->specialn_miss_speed * fp->facing_dir;
    fp->self_vel.y = -da->specialn_miss_speed;
    ftSh_MV(fp)->specialn.timer = da->specialn_attack_frames;
    fp->accessory4_cb = ftKbSh_GFXSpinAndTrail;
    fp->deal_dmg_cb = ftKbSh_SpecialNRebound_OnEnter;
}

static void ftKbSh_SpecialNAttackMiss_Anim(HSD_GObj* gobj)
{
    Fighter* fp = GET_FIGHTER(gobj);
    ftSh_SpecialNVars* mv = &ftSh_MV(fp)->specialn;

    if (mv->timer > 0) {
        mv->timer--;
        return;
    }
    ftKbSh_SpecialNCancel_Enter(gobj);
}

static void ftKbSh_SpecialNDash_Coll(HSD_GObj* gobj)
{
    Fighter* fp = GET_FIGHTER(gobj);
    ftSh_DatAttrs* da = ftKbSh_DA(fp);
    ftCollisionBox box;
    u32 env;

    if (da == NULL) {
        return;
    }
    box = ftCollisionBox_FromDisc(&da->coll_box);
    ft_800824A0(gobj, &box);
    env = fp->coll_data.env_flags;
    if (env & (Collide_WallMask | Collide_CeilingMask | Collide_FloorMask)) {
        ftKbSh_SpecialNRebound_Enter(gobj, env);
        return;
    }
    if (ftCliffCommon_80081298(gobj)) {
        ftCliffCommon_80081370(gobj);
    }
}

static void ftKbSh_SpecialNAttack_Enter(HSD_GObj* gobj, float target_x, float target_y)
{
    Fighter* fp = GET_FIGHTER(gobj);
    ftSh_DatAttrs* da = ftKbSh_DA(fp);
    ftSh_SpecialNVars* mv = &ftSh_MV(fp)->specialn;
    float angle;

    if (da == NULL) {
        return;
    }
    mv->target.x = target_x;
    mv->target.y = target_y;
    mv->timer = da->specialn_attack_frames;
    angle = atan2f(target_y - fp->cur_pos.y, target_x - fp->cur_pos.x);
    mv->angle = angle;
    fp->self_vel.x = da->specialn_attack_speed * cosf(angle);
    fp->self_vel.y = da->specialn_attack_speed * sinf(mv->angle);

    mu_ak_kirby_state_change(gobj, ftKbSh_State_SpecialNAttack, Ft_MF_None, 0.0F,
                             da->specialn_attack_anim_rate, 0.0F);
    fp->accessory4_cb = ftKbSh_GFXSpinAndTrailVelocityDirection;
    fp->deal_dmg_cb = ftKbSh_SpecialNHit_Enter;
}

static void ftKbSh_SpecialNAttack_Anim(HSD_GObj* gobj)
{
    Fighter* fp = GET_FIGHTER(gobj);
    ftSh_DatAttrs* da = ftKbSh_DA(fp);
    ftSh_SpecialNVars* mv = &ftSh_MV(fp)->specialn;
    float dist2;

    if (da == NULL) {
        return;
    }
    fp->self_vel.x = da->specialn_attack_speed * cosf(mv->angle);
    fp->self_vel.y = da->specialn_attack_speed * sinf(mv->angle);

    if (mv->timer > 0) {
        mv->timer--;
    } else {
        ftKbSh_SpecialNCancel_Enter(gobj);
    }

    dist2 = ftKbSh_DistSquared(&fp->cur_pos, &mv->target);
    if (!(dist2 <= da->specialn_attack_speed * da->specialn_attack_speed)) {
        return;
    }
    ftKbSh_SpecialNCancel_Enter(gobj);
}

static void ftKbSh_SpecialNCancel_Enter(HSD_GObj* gobj)
{
    Fighter* fp = GET_FIGHTER(gobj);

    mu_ak_kirby_state_change(gobj, ftKbSh_State_SpecialNCancel, Ft_MF_None, 0.0F, 1.0F, 0.0F);
    fp->self_vel.y = 0.0F;
    fp->cmd_vars[0] = 0;
}

static void ftKbSh_SpecialNCancel_Phys(HSD_GObj* gobj)
{
    Fighter* fp = GET_FIGHTER(gobj);
    ftSh_DatAttrs* da = ftKbSh_DA(fp);

    if (da == NULL) {
        return;
    }
    ft_80084DB0(gobj);
    ftCommon_CalcSelfAccel_Deaccel(fp, da->specialn_cancel_air_decel);
}

static void ftKbSh_SpecialNCancel_Coll(HSD_GObj* gobj)
{
    Fighter* fp = GET_FIGHTER(gobj);

    if (ft_CheckGroundAndLedge(gobj, (int) fp->facing_dir)) {
        ftKbSh_SpecialNLanding_Enter(gobj);
    }
}

static void ftKbSh_SpecialNLanding_Enter(HSD_GObj* gobj)
{
    Fighter* fp = GET_FIGHTER(gobj);

    ftCommon_8007D6A4(fp);
    mu_ak_kirby_state_change(gobj, ftKbSh_State_SpecialNLanding, Ft_MF_None, 0.0F, 1.0F, 0.0F);
    fp->self_vel.y = 0.0F;
    fp->cmd_vars[0] = 0;
}

static void ftKbSh_SpecialNLanding_Phys(HSD_GObj* gobj)
{
    Fighter* fp = GET_FIGHTER(gobj);
    ftSh_DatAttrs* da = ftKbSh_DA(fp);

    if (da == NULL) {
        return;
    }
    ftCommon_CalcGroundAccel_Deaccel(fp, da->specialn_landing_decel);
    ftCommon_SetSelfMovementFromGroundedMovement(gobj);
}

static void ftKbSh_SpecialNRebound_Enter(HSD_GObj* gobj, u32 env_flags)
{
    Fighter* fp = GET_FIGHTER(gobj);
    ftSh_DatAttrs* da = ftKbSh_DA(fp);
    static int logged;

    if (da == NULL) {
        return;
    }
    mu_ak_kirby_state_change(gobj, ftKbSh_State_SpecialNRebound, Ft_MF_None, 0.0F, 1.0F, 0.0F);
    if (env_flags & (Collide_WallMask | Collide_CeilingMask)) {
        fp->self_vel.x = fp->self_vel.x * -da->specialn_rebound_mul_x;
    } else if (env_flags & Collide_FloorMask) {
        fp->self_vel.x = fp->self_vel.x * da->specialn_rebound_mul_x;
    }
    ftKbSh_ClampReboundVel(fp, da, -fp->self_vel.y);
    if (logged < FTKBSH_LOG) {
        logged++;
        OSReport("[ace] Kirby (Shadow) homing attack rebound: touched %08X, velocity (%.2f, %.2f)\n",
                 (unsigned) env_flags, fp->self_vel.x, fp->self_vel.y);
    }
}

static void ftKbSh_SpecialNRebound_OnEnter(HSD_GObj* gobj)
{
    ftKbSh_SpecialNRebound_Enter(gobj, 0);
}

static void ftKbSh_SpecialNHit_Enter(HSD_GObj* gobj)
{
    Fighter* fp = GET_FIGHTER(gobj);
    ftSh_DatAttrs* da = ftKbSh_DA(fp);
    float vel_y;
    static int logged;

    if (da == NULL) {
        return;
    }
    mu_ak_kirby_state_change(gobj, ftKbSh_State_SpecialNHit, Ft_MF_None, 0.0F, 1.0F, 0.0F);
    vel_y = fp->self_vel.y;
    if (vel_y < 0.0F) {
        vel_y = -vel_y;
    }
    if (vel_y < 2.0F) {
        vel_y = 2.0F;
    }
    fp->self_vel.y = vel_y;
    ftKbSh_ClampReboundVel(fp, da, vel_y);
    if (logged < FTKBSH_LOG) {
        logged++;
        OSReport("[ace] Kirby (Shadow) homing attack hit: velocity (%.2f, %.2f)\n", fp->self_vel.x,
                 fp->self_vel.y);
    }
}

static void ftKbSh_SpecialNRebound_Phys(HSD_GObj* gobj)
{
    Fighter* fp = GET_FIGHTER(gobj);
    ftSh_DatAttrs* da = ftKbSh_DA(fp);

    if (da == NULL) {
        return;
    }
    ftCommon_CalcSelfAccel_Deaccel(fp, da->specialn_rebound_air_decel);
    ftCommon_Fall(fp, da->specialn_rebound_gravity, da->specialn_rebound_terminal_vel);
}

static void ftKbSh_SpecialNRebound_Coll(HSD_GObj* gobj)
{
    if (ft_80081D0C(gobj)) {
        ftKbSh_SpecialNLanding_Enter(gobj);
    }
}

static const MotionState ftKbSh_MotionStateTable[ftKbSh_State_Count] = {
    {
        
        0,
        0x00340111,
        0x12 << 24,
        ftKbSh_SpecialNStart_Anim,
        ftSh_SpecialN_Empty,
        ftSh_SpecialN_Empty,
        ftSh_SpecialNStart_Coll,
        ftCamera_UpdateCameraBox,
    },
    {
        
        1,
        0x00340111,
        0x12 << 24,
        ftKbSh_SpecialNStart_Anim,
        ftSh_SpecialN_Empty,
        ftSh_SpecialN_Empty,
        ftSh_SpecialNStart_Coll,
        ftCamera_UpdateCameraBox,
    },
    {
        
        2,
        0x00340111,
        0x12 << 24,
        ftKbSh_SpecialNCharge_Anim,
        ftSh_SpecialN_Empty,
        ftSh_SpecialN_Empty,
        ftKbSh_SpecialNCharge_Coll,
        ftCamera_UpdateCameraBox,
    },
    {
        
        3,
        0x00340111,
        0x12 << 24,
        ftKbSh_SpecialNAttackMiss_Anim,
        ftSh_SpecialN_Empty,
        ftSh_SpecialN_Empty,
        ftKbSh_SpecialNDash_Coll,
        ftCamera_UpdateCameraBox,
    },
    {
        
        3,
        0x00340111,
        0x12 << 24,
        ftKbSh_SpecialNAttack_Anim,
        ftSh_SpecialN_Empty,
        ftSh_SpecialN_Empty,
        ftKbSh_SpecialNDash_Coll,
        ftCamera_UpdateCameraBox,
    },
    {
        
        4,
        0x00340111,
        0x12 << 24,
        ftSh_SpecialNCancel_Anim,
        ftSh_SpecialNCancel_IASA,
        ftKbSh_SpecialNCancel_Phys,
        ftKbSh_SpecialNCancel_Coll,
        ftCamera_UpdateCameraBox,
    },
    {
        
        7,
        0x00340111,
        0x12 << 24,
        ftSh_SpecialNLanding_Anim,
        ftSh_SpecialNLanding_IASA,
        ftKbSh_SpecialNLanding_Phys,
        ftSh_SpecialNLanding_Coll,
        ftCamera_UpdateCameraBox,
    },
    {
        
        6,
        0x00340111,
        0x12 << 24,
        ftSh_SpecialNRebound_Anim,
        ftSh_SpecialNRebound_IASA,
        ftKbSh_SpecialNRebound_Phys,
        ftKbSh_SpecialNRebound_Coll,
        ftCamera_UpdateCameraBox,
    },
    {
        
        5,
        0x00340111,
        0x12 << 24,
        ftSh_SpecialNRebound_Anim,
        ftSh_SpecialNRebound_IASA,
        ftKbSh_SpecialNRebound_Phys,
        ftKbSh_SpecialNRebound_Coll,
        ftCamera_UpdateCameraBox,
    },
};

const MuAkKirbyCopy ftKbSh_Copy = {
    .on_swallow = ftKbSh_OnSwallow,
    .on_lose = ftKbSh_OnLose,
    .special_n = ftKbSh_SpecialNStart_Enter,
    .special_air_n = ftKbSh_SpecialNStart_Enter,
    .on_hurt = ftKbSh_OnHurt,
    .init_items = NULL,
    .move_logic = ftKbSh_MotionStateTable,
    .move_logic_count = ftKbSh_State_Count,
    .on_frame = NULL,
};
