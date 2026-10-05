/* Knuckles native port working copy; PlKx.dat comparison review in progress. */
/* Akaneia's Sonic: side special, the spin dash.
 *
 * Start -> Hold: he spins in place (ground or air) charging one point per frame, kicking up
 * dust. After 8 frames B launches the dash, L/R/Z cancels and throws the charge away. Reaching
 * the full 75 ends the move with the charge STORED: the next side B skips straight to a
 * full-power dash (SpecialSMax / SpecialAirSMax), and while stored every state change reapplies
 * a color overlay (OnActionStateChange). Dash speed scales from 4 to 6 with the charge.
 * Hand-written from PlSn.dat's ftFunction code; see NOTES.md. */
#include "knuckles.h"

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
#include <melee/ft/types.h>
#include <melee/lb/lb_00B0.h>
#include <sysdolphin/baselib/gobj.h>
#include <sysdolphin/baselib/jobj.h>
#include <sysdolphin/baselib/random.h>

#define ftKx_PI 3.141592653589793

static void ftKx_SpecialSStart_Enter(HSD_GObj* gobj);
static void ftKx_SpecialAirSStart_Enter(HSD_GObj* gobj);
static void ftKx_SpecialSHold_Enter(HSD_GObj* gobj);
static void ftKx_SpecialSEnd_Enter(HSD_GObj* gobj);
static void ftKx_SpecialAirSEnd_Enter(HSD_GObj* gobj);
static void ftKx_SpecialSAttack_EnterAirOrGround(HSD_GObj* gobj);
static void ftKx_SpecialSStart_Trans(HSD_GObj* gobj);
static void ftKx_SpecialAirSStart_Trans(HSD_GObj* gobj);
static void ftKx_SpecialSEnd_Trans(HSD_GObj* gobj);
static void ftKx_SpecialAirSEnd_Trans(HSD_GObj* gobj);
static void ftKx_SpecialS_Trans(HSD_GObj* gobj);
static void ftKx_SpecialAirS_Trans(HSD_GObj* gobj);
static void ftKx_SpecialS_OnHit(HSD_GObj* gobj);
static void ftKx_SpecialS_GiveDamage(HSD_GObj* gobj);

/* ---- entry ---------------------------------------------------------------------------------- */

/* SpecialS_EnterAirOrGround: specials and specialairs */
void ftKx_SpecialS_EnterAirOrGround(HSD_GObj* gobj)
{
    Fighter* fp = GET_FIGHTER(gobj);

    if (ftKx_FV(fp)->specials_charge == ftKx_DA(fp)->specials_max_charge) {
        ftKx_SpecialSAttack_EnterAirOrGround(gobj);
    } else {
        ftKnuckles_SpecialSVars* mv;

        if (fp->ground_or_air == GA_Ground) {
            ftKx_SpecialSStart_Enter(gobj);
        } else {
            ftKx_SpecialAirSStart_Enter(gobj);
        }
        mv = &ftKx_MV(fp)->specials;
        mv->dust_timer = 0;
        mv->hold_frames = 0;
        mv->cancel = 0;
    }
    fp->cmd_vars[0] = 0;
    fp->cmd_vars[1] = 0;
    fp->cmd_vars[2] = 0;
    fp->cmd_vars[3] = 0;
}

static void ftKx_SpecialSStart_Enter(HSD_GObj* gobj)
{
    Fighter* fp = GET_FIGHTER(gobj);

    Fighter_ChangeMotionState(gobj, ftKx_MS_SpecialSStart, Ft_MF_None, 0.0F, 1.0F, 0.0F, NULL);
    fp->self_vel.y = 0.0F;
    ftKx_SetEffectCallbacks(fp, ftKx_SpecialS_OnHit);
}

static void ftKx_SpecialAirSStart_Enter(HSD_GObj* gobj)
{
    Fighter* fp = GET_FIGHTER(gobj);

    Fighter_ChangeMotionState(gobj, ftKx_MS_SpecialAirSStart, Ft_MF_None, 0.0F, 1.0F, 0.0F,
                              NULL);
    ftKx_SetEffectCallbacks(fp, ftKx_SpecialS_OnHit);
}

/* ---- Start ---------------------------------------------------------------------------------- */

void ftKx_SpecialSStart_Anim(HSD_GObj* gobj)
{
    if (!ftAnim_IsFramesRemaining(gobj)) {
        ftKx_SpecialSHold_Enter(gobj);
    }
}

void ftKx_SpecialSStart_IASA(HSD_GObj* gobj) {}

void ftKx_SpecialSStart_Phys(HSD_GObj* gobj)
{
    ft_80084F3C(gobj);
}

void ftKx_SpecialSStart_Coll(HSD_GObj* gobj)
{
    if (!ft_80082708(gobj)) {
        ftKx_SpecialAirSStart_Trans(gobj);
    }
}

void ftKx_SpecialAirSStart_Anim(HSD_GObj* gobj)
{
    if (!ftAnim_IsFramesRemaining(gobj)) {
        ftKx_SpecialSHold_Enter(gobj);
    }
}

void ftKx_SpecialAirSStart_IASA(HSD_GObj* gobj) {}

void ftKx_SpecialAirSStart_Phys(HSD_GObj* gobj)
{
    ft_80084EEC(gobj);
}

void ftKx_SpecialAirSStart_Coll(HSD_GObj* gobj)
{
    if (ft_80081D0C(gobj) == true) { /* landed */
        ftKx_SpecialSStart_Trans(gobj);
    }
}

static void ftKx_SpecialSStart_Trans(HSD_GObj* gobj)
{
    Fighter* fp = GET_FIGHTER(gobj);

    Fighter_ChangeMotionState(gobj, ftKx_MS_SpecialSStart, ftCommon_GroundAirColl_MF,
                              fp->cur_anim_frame, 1.0F, 0.0F, NULL);
    ftCommon_8007D7FC(fp);
    fp->self_vel.y = 0.0F;
    ftKx_SetEffectCallbacks(fp, ftKx_SpecialS_OnHit);
}

static void ftKx_SpecialAirSStart_Trans(HSD_GObj* gobj)
{
    Fighter* fp = GET_FIGHTER(gobj);

    Fighter_ChangeMotionState(gobj, ftKx_MS_SpecialAirSStart, ftCommon_GroundAirColl_MF,
                              fp->cur_anim_frame, 1.0F, 0.0F, NULL);
    ftCommon_8007D5D4(fp);
    ftKx_SetEffectCallbacks(fp, ftKx_SpecialS_OnHit);
}

/* ---- Hold ----------------------------------------------------------------------------------- */

static void ftKx_SpecialSHold_Enter(HSD_GObj* gobj)
{
    Fighter* fp = GET_FIGHTER(gobj);

    Fighter_ChangeMotionState(gobj, ftKx_MS_SpecialSHold, Ft_MF_None, 0.0F, 1.0F, 0.0F, NULL);
    ftKx_SetEffectCallbacks(fp, ftKx_SpecialS_OnHit);
}

/* SpecialS_OnFinishCharge */
static void ftKx_SpecialS_OnFinishCharge(HSD_GObj* gobj)
{
    Fighter* fp = GET_FIGHTER(gobj);

    ftKx_FV(fp)->specials_charge = ftKx_DA(fp)->specials_max_charge;
    ft_800881D8(fp, ftKx_Sfx_ChargeFull, 0x7F, 0x40);
}

/* SpecialS_SpawnChargeEffect: dust at the feet every few frames, randomly turned; also marks the
 * move's effects as spawned (x2219_b0). */
static void ftKx_SpecialS_SpawnChargeEffect(HSD_GObj* gobj)
{
    Fighter* fp = GET_FIGHTER(gobj);
    ftKnuckles_DatAttrs* da = ftKx_DA(fp);
    ftKnuckles_SpecialSVars* mv = &ftKx_MV(fp)->specials;
    EF_Effect* effect;
    Vec3 pos;

    if (!fp->x2219_b0) {
        fp->x2219_b0 = true;
    }
    if (mv->dust_timer > 0) {
        mv->dust_timer--;
        return;
    }

    lb_8000B1CC(fp->parts[FtPart_TopN].joint, NULL, &pos);
    effect = efSync_Spawn(ftKx_Ef_ChargeDust, gobj, &pos);
    if (effect != NULL && effect->gobj != NULL) {
        HSD_JObj* jobj = effect->gobj->hsd_obj;
        if (jobj != NULL) {
            double turn = HSD_Randf() * ftKx_PI;
            jobj->rotate.y = (float) (turn + turn);
            jobj->rotate.z = HSD_Randf() - 0.5F;
            if (fp->ground_or_air == GA_Air) {
                jobj->translate.y = jobj->translate.y - 7.0F;
            }
        }
    }
    mv->dust_timer = da->specials_dust_interval;
}

void ftKx_SpecialSHold_Anim(HSD_GObj* gobj)
{
    Fighter* fp = GET_FIGHTER(gobj);
    ftKnuckles_FighterVars* fv = ftKx_FV(fp);

    if (fv->specials_charge >= ftKx_DA(fp)->specials_max_charge) {
        ftKx_SpecialS_OnFinishCharge(gobj);
        if (fp->ground_or_air == GA_Air) {
            ftKx_SpecialAirSEnd_Enter(gobj);
        } else {
            ftKx_SpecialSEnd_Enter(gobj);
        }
        return;
    }
    ftKx_SpecialS_SpawnChargeEffect(gobj);
    fv->specials_charge++;
}

void ftKx_SpecialSHold_IASA(HSD_GObj* gobj)
{
    Fighter* fp = GET_FIGHTER(gobj);
    ftKnuckles_DatAttrs* da = ftKx_DA(fp);
    ftKnuckles_SpecialSVars* mv = &ftKx_MV(fp)->specials;
    HSD_Pad pressed = fp->input.pressed_buttons;

    if (pressed & (HSD_PAD_L | HSD_PAD_R | HSD_PAD_Z)) {
        mv->cancel = 1;
    }
    if (mv->hold_frames >= da->specials_min_hold_frames) {
        if (fp->ground_or_air == GA_Air) {
            if (mv->cancel == 1) {
                mv->cancel = 0;
                ftKx_FV(fp)->specials_charge = 0;
                ftKx_SpecialAirSEnd_Enter(gobj);
            } else if (pressed & HSD_PAD_B) {
                ftKx_SpecialSAttack_EnterAirOrGround(gobj);
            }
        } else {
            if (mv->cancel == 1) {
                mv->cancel = 0;
                ftKx_FV(fp)->specials_charge = 0;
                ftKx_SpecialSEnd_Enter(gobj);
            } else if (pressed & HSD_PAD_B) {
                ftKx_SpecialSAttack_EnterAirOrGround(gobj);
            }
        }
    }
    /* Runs even after leaving the state above, as shipped. */
    mv->hold_frames++;
}

void ftKx_SpecialSHold_Phys(HSD_GObj* gobj)
{
    Fighter* fp = GET_FIGHTER(gobj);
    ftKnuckles_DatAttrs* da = ftKx_DA(fp);

    if (fp->ground_or_air == GA_Air) {
        ftCommon_Fall(fp, da->specials_hold_gravity, da->specials_hold_terminal_vel);
        ftCommon_CalcSelfAccel_Deaccel(fp, da->specials_hold_air_decel);
    } else {
        ft_80084F3C(gobj);
    }
}

/* Hold works on the ground and in the air in one state: only the ground/air flag flips. */
void ftKx_SpecialSHold_Coll(HSD_GObj* gobj)
{
    Fighter* fp = GET_FIGHTER(gobj);

    if (fp->ground_or_air == GA_Ground) {
        if (!ft_80082708(gobj)) {
            ftCommon_8007D5D4(fp);
        }
    } else if (fp->ground_or_air == GA_Air) {
        ftCollisionBox box = ftKx_CollBox(fp);
        if (ft_800824A0(gobj, &box) == true) {
            ftCommon_8007D7FC(fp);
        }
    }
}

/* ---- End ------------------------------------------------------------------------------------ */

static void ftKx_SpecialSEnd_Enter(HSD_GObj* gobj)
{
    Fighter* fp = GET_FIGHTER(gobj);

    Fighter_ChangeMotionState(gobj, ftKx_MS_SpecialSEnd, Ft_MF_None, 0.0F,
                              ftKx_DA(fp)->specials_end_anim_rate, 0.0F, NULL);
}

static void ftKx_SpecialAirSEnd_Enter(HSD_GObj* gobj)
{
    Fighter* fp = GET_FIGHTER(gobj);

    Fighter_ChangeMotionState(gobj, ftKx_MS_SpecialAirSEnd, Ft_MF_None, 0.0F,
                              ftKx_DA(fp)->specials_end_anim_rate, 0.0F, NULL);
}

static void ftKx_SpecialSEnd_ClearCallbacks(Fighter* fp)
{
    fp->take_dmg_2_cb = NULL;
    fp->pre_hitlag_cb = NULL;
    fp->post_hitlag_cb = NULL;
}

void ftKx_SpecialSEnd_Anim(HSD_GObj* gobj)
{
    Fighter* fp = GET_FIGHTER(gobj);

    if (!ftAnim_IsFramesRemaining(gobj)) {
        ftKx_SpecialSEnd_ClearCallbacks(fp);
        ft_8008A2BC(gobj);
    }
}

void ftKx_SpecialSEnd_IASA(HSD_GObj* gobj) {}

void ftKx_SpecialSEnd_Phys(HSD_GObj* gobj)
{
    ft_80084F3C(gobj);
}

void ftKx_SpecialSEnd_Coll(HSD_GObj* gobj)
{
    if (!ft_80082708(gobj)) {
        ftKx_SpecialAirSEnd_Trans(gobj);
    }
}

void ftKx_SpecialAirSEnd_Anim(HSD_GObj* gobj)
{
    Fighter* fp = GET_FIGHTER(gobj);

    if (!ftAnim_IsFramesRemaining(gobj)) {
        ftKx_SpecialSEnd_ClearCallbacks(fp);
        ftCo_Fall_Enter(gobj);
    }
}

void ftKx_SpecialAirSEnd_IASA(HSD_GObj* gobj) {}

void ftKx_SpecialAirSEnd_Phys(HSD_GObj* gobj)
{
    Fighter* fp = GET_FIGHTER(gobj);
    ftKnuckles_DatAttrs* da = ftKx_DA(fp);

    ftCommon_Fall(fp, da->specials_hold_gravity, da->specials_hold_terminal_vel);
    ftCommon_CalcSelfAccel_Deaccel(fp, da->specials_hold_air_decel);
}

void ftKx_SpecialAirSEnd_Coll(HSD_GObj* gobj)
{
    if (ft_80081D0C(gobj) == true) { /* landed */
        ftKx_SpecialSEnd_Trans(gobj);
    }
}

static void ftKx_SpecialSEnd_Trans(HSD_GObj* gobj)
{
    Fighter* fp = GET_FIGHTER(gobj);

    Fighter_ChangeMotionState(gobj, ftKx_MS_SpecialSEnd, ftCommon_GroundAirColl_MF,
                              fp->cur_anim_frame, ftKx_DA(fp)->specials_end_anim_rate, 0.0F,
                              NULL);
    ftCommon_8007D7FC(fp);
}

static void ftKx_SpecialAirSEnd_Trans(HSD_GObj* gobj)
{
    Fighter* fp = GET_FIGHTER(gobj);

    Fighter_ChangeMotionState(gobj, ftKx_MS_SpecialAirSEnd, ftCommon_GroundAirColl_MF,
                              fp->cur_anim_frame, ftKx_DA(fp)->specials_end_anim_rate, 0.0F,
                              NULL);
    ftCommon_8007D5D4(fp);
}

/* ---- the dash ------------------------------------------------------------------------------- */

/* SpecialSAttack_EnterAirOrGround: launch with speed lerped by the charge, spending it. */
static void ftKx_SpecialSAttack_EnterAirOrGround(HSD_GObj* gobj)
{
    Fighter* fp = GET_FIGHTER(gobj);
    ftKnuckles_DatAttrs* da = ftKx_DA(fp);
    ftKnuckles_FighterVars* fv = ftKx_FV(fp);
    ftKnuckles_SpecialSVars* mv;
    float ratio;
    FtMotionId msid;

    if (fv->specials_charge >= da->specials_max_charge) {
        msid = fp->ground_or_air == GA_Ground ? ftKx_MS_SpecialSMax : ftKx_MS_SpecialAirSMax;
    } else {
        msid = fp->ground_or_air == GA_Ground ? ftKx_MS_SpecialS : ftKx_MS_SpecialAirS;
    }
    Fighter_ChangeMotionState(gobj, msid, Ft_MF_None, 0.0F, 1.0F, 0.0F, NULL);

    mv = &ftKx_MV(fp)->specials;
    ratio = (float) fv->specials_charge / (float) da->specials_max_charge;
    mv->charge_ratio = ratio;
    fp->self_vel.x = ((da->specials_max_speed - da->specials_min_speed) * ratio +
                      da->specials_min_speed) *
                     fp->facing_dir;
    fp->self_vel.y = 0.0F;
    fv->specials_charge = 0;

    ftKx_SetEffectCallbacks(fp, ftKx_SpecialS_OnHit);
    fp->deal_dmg_cb = ftKx_SpecialS_GiveDamage;
    fp->cmd_vars[0] = 0;
}

/* SpecialS_ApplyFriction: scale x velocity down each frame, stopping under 0.5. */
static void ftKx_SpecialS_ApplyFriction(HSD_GObj* gobj)
{
    Fighter* fp = GET_FIGHTER(gobj);
    float vel = (1.0F - ftKx_DA(fp)->specials_friction) * fp->self_vel.x;
    float speed = vel < 0.0F ? -vel : vel;

    if (speed < 0.5F) {
        vel = 0.0F;
    }
    fp->self_vel.x = vel;
}

void ftKx_SpecialS_Anim(HSD_GObj* gobj)
{
    Fighter* fp = GET_FIGHTER(gobj);

    if (!ftAnim_IsFramesRemaining(gobj)) {
        fp->gr_vel = 0.0F;
        ft_8008A2BC(gobj);
    }
}

void ftKx_SpecialS_IASA(HSD_GObj* gobj) {}

void ftKx_SpecialS_Phys(HSD_GObj* gobj)
{
    ftKx_SpecialS_ApplyFriction(gobj);
}

/* cmd_vars[0] (set by the animation script) switches from the ledge-stopping ground check to the
 * one that rolls off edges. */
void ftKx_SpecialS_Coll(HSD_GObj* gobj)
{
    Fighter* fp = GET_FIGHTER(gobj);

    if (fp->cmd_vars[0] == 0) {
        if (!ft_80082708(gobj)) {
            ftKx_SpecialAirS_Trans(gobj);
        }
    } else if (fp->cmd_vars[0] == 1) {
        if (!ft_800827A0(gobj)) {
            ftKx_SpecialAirS_Trans(gobj);
        }
    }
}

void ftKx_SpecialAirS_Anim(HSD_GObj* gobj)
{
    Fighter* fp = GET_FIGHTER(gobj);
    ftKnuckles_DatAttrs* da = ftKx_DA(fp);

    if (ftAnim_IsFramesRemaining(gobj)) {
        return;
    }
    if (da->specials_landing_lag == 0.0F) {
        ftCo_Fall_Enter(gobj);
    } else {
        ftCo_80096900(gobj, 1, 1, false, da->specials_fall_mobility, da->specials_landing_lag);
    }
}

void ftKx_SpecialAirS_IASA(HSD_GObj* gobj) {}

void ftKx_SpecialAirS_Phys(HSD_GObj* gobj)
{
    Fighter* fp = GET_FIGHTER(gobj);
    ftKnuckles_DatAttrs* da = ftKx_DA(fp);

    ftCommon_Fall(fp, da->specials_air_gravity, da->specials_air_terminal_vel);
    ftKx_SpecialS_ApplyFriction(gobj);
}

void ftKx_SpecialAirS_Coll(HSD_GObj* gobj)
{
    if (ft_80081D0C(gobj) == true) { /* landed */
        ftKx_SpecialS_Trans(gobj);
    }
}

/* Landing and leaving the ground always pick the uncharged dash states, even from the full-power
 * ones (as shipped). */
static void ftKx_SpecialS_Trans(HSD_GObj* gobj)
{
    Fighter* fp = GET_FIGHTER(gobj);

    Fighter_ChangeMotionState(gobj, ftKx_MS_SpecialS, ftCommon_GroundAirColl_MF | Ft_MF_SkipHit,
                              fp->cur_anim_frame, 1.0F, 0.0F, NULL);
    ftCommon_8007D7FC(fp);
    ftKx_SetEffectCallbacks(fp, ftKx_SpecialS_OnHit);
    fp->deal_dmg_cb = ftKx_SpecialS_GiveDamage;
    fp->cmd_vars[0] = 0;
}

static void ftKx_SpecialAirS_Trans(HSD_GObj* gobj)
{
    Fighter* fp = GET_FIGHTER(gobj);

    Fighter_ChangeMotionState(gobj, ftKx_MS_SpecialAirS,
                              ftCommon_GroundAirColl_MF | Ft_MF_SkipHit, fp->cur_anim_frame,
                              1.0F, 0.0F, NULL);
    ftCommon_8007D5D4(fp);
    ftKx_SetEffectCallbacks(fp, ftKx_SpecialS_OnHit);
    fp->deal_dmg_cb = ftKx_SpecialS_GiveDamage;
}

/* ---- callbacks ------------------------------------------------------------------------------ */

/* SpecialS_OnHit: getting hit loses the stored charge and the effects. */
static void ftKx_SpecialS_OnHit(HSD_GObj* gobj)
{
    Fighter* fp = GET_FIGHTER(gobj);

    ftKx_FV(fp)->specials_charge = 0;
    efLib_DestroyAll(gobj);
}

/* SpecialS_GiveDamage: empty in the shipped code (the dash keeps going through a hit). */
static void ftKx_SpecialS_GiveDamage(HSD_GObj* gobj) {}
