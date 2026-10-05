/* Knuckles native port working copy; PlKx.dat comparison review in progress. */
/* ACE Knuckles: ground dash, aerial dash and aerial turn.
 * The PlKx.dat callbacks skip the inherited charging state. */
#include "knuckles.h"

#include <melee/ef/eflib.h>
#include <melee/ef/efsync.h>
#include <melee/ef/types.h>
#include <melee/ft/fighter.h>
#include <melee/ft/ft_081B.h>
#include <melee/ft/ft_0819.h>
#include <melee/ft/ft_084E.h>
#include <melee/ft/ft_0881.h>
#include <melee/ft/ft_0892.h>
#include <melee/ft/ftanim.h>
#include <melee/ft/ftcliffcommon.h>
#include <melee/ft/ftwalljump.h>
#include <melee/ft/ftwalkcommon.h>
#include <melee/ft/kinds/ftCommon/ftCo_Jump.h>
#include <melee/ft/kinds/ftCommon/ftCo_AirCatch.h>
#include <melee/ft/kinds/ftCommon/ftCo_JumpAerial.h>
#include <melee/mp/mpcoll.h>
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
static void ftKx_SpecialSAttack_EnterAirOrGround(HSD_GObj* gobj);
static void ftKx_SpecialSStart_Trans(HSD_GObj* gobj);
static void ftKx_SpecialAirSStart_Trans(HSD_GObj* gobj);
static void ftKx_SpecialSEnd_Trans(HSD_GObj* gobj);
static void ftKx_SpecialAirSEnd_Trans(HSD_GObj* gobj);
static void ftKx_SpecialS_Trans(HSD_GObj* gobj);
static void ftKx_SpecialAirS_Trans(HSD_GObj* gobj);
static void ftKx_SpecialS_OnHit(HSD_GObj* gobj);
static void ftKx_SpecialS_GiveDamage(HSD_GObj* gobj);

/* PlKx compares r3 after the void collision helper at 800831CC. Its ground
 * callback leaves the ground-validation result there; otherwise the last wall/ledge check
 * leaves its boolean. Keep that branch without reading a native void return. */
static bool ftKx_AirSideCollision(HSD_GObj* gobj)
{
    Fighter* fp = GET_FIGHTER(gobj);
    CollData* coll = &fp->coll_data;
    bool landed;
    coll->last_pos = coll->cur_pos;
    coll->cur_pos = fp->cur_pos;
    if (fp->x2064_ledgeCooldown || fp->stamina_dead) {
        landed = mpColl_80047AC8(coll, NULL, gobj);
    } else {
        mpCollSetFacingDir(coll, fp->facing_dir < 0.0F ? -1 : 1);
        landed = mpColl_80047E14(coll, NULL, gobj);
    }
    fp->cur_pos = coll->cur_pos;
    if (!ft_80081A00(gobj) && landed) {
        ftKx_SpecialS_Trans(gobj);
        return true;
    }
    if (ftWallJump_8008169C(gobj)) return true;
    return ftCliffCommon_80081298(gobj);
}

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
    Fighter_ChangeMotionState(gobj, ftKx_MS_SpecialS, Ft_MF_None, 0.0F, 1.0F, 0.0F, NULL);
    fp->self_vel.x = ftKx_DA(fp)->specials_min_speed * fp->facing_dir;
    fp->self_vel.y = 0.0F;
    ftKx_SetEffectCallbacks(fp, ftKx_SpecialS_OnHit);
}

static void ftKx_SpecialAirSStart_Enter(HSD_GObj* gobj)
{
Fighter* fp = GET_FIGHTER(gobj);
    if (ftKx_FV(fp)->air_side_used == 1) {
        if (fp->facing_dir1 == 1.0F) fp->facing_dir = 1.0F;
        else if (fp->facing_dir1 == -1.0F) fp->facing_dir = -1.0F;
        return;
    }
    fp->self_vel.x = (float) (fp->facing_dir * 0.85);
    fp->self_vel.y = 0.0F;
    Fighter_ChangeMotionState(gobj, ftKx_MS_SpecialAirS, Ft_MF_None, 0.0F, 1.0F, 0.0F, NULL);
    ftKx_SetEffectCallbacks(fp, ftKx_SpecialS_OnHit);
    ftKx_FV(fp)->air_side_used = 1;
    fp->cmd_vars[3] = 0;
}

/* ---- Start ---------------------------------------------------------------------------------- */

void ftKx_SpecialSStart_Anim(HSD_GObj* gobj)
{

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
    Fighter_ChangeMotionState(gobj, ftKx_MS_SpecialS, ftCommon_GroundAirColl_MF,
                             fp->cur_anim_frame, 1.0F, 0.0F, NULL);
    ftCommon_8007D7FC(fp);
    fp->self_vel.y = 0.0F;
    ftKx_SetEffectCallbacks(fp, ftKx_SpecialS_OnHit);
}

static void ftKx_SpecialAirSStart_Trans(HSD_GObj* gobj)
{
Fighter* fp = GET_FIGHTER(gobj);
    Fighter_ChangeMotionState(gobj, ftKx_MS_SpecialAirS, ftCommon_GroundAirColl_MF,
                             fp->cur_anim_frame, 1.0F, 0.0F, NULL);
    ftCommon_8007D5D4(fp);
    ftKx_SetEffectCallbacks(fp, ftKx_SpecialS_OnHit);
    ftKx_FV(fp)->air_side_used = 1;
}

/* ---- Hold ----------------------------------------------------------------------------------- */



/* SpecialS_OnFinishCharge */


/* SpecialS_SpawnChargeEffect: dust at the feet every few frames, randomly turned; also marks the
 * move's effects as spawned (x2219_b0). */








/* Hold works on the ground and in the air in one state: only the ground/air flag flips. */


/* ---- End ------------------------------------------------------------------------------------ */





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
                             fp->cur_anim_frame, 1.0F, 0.0F, NULL);
    ftCommon_8007D7FC(fp);
}

static void ftKx_SpecialAirSEnd_Trans(HSD_GObj* gobj)
{
Fighter* fp = GET_FIGHTER(gobj);
    Fighter_ChangeMotionState(gobj, ftKx_MS_SpecialAirSEnd, ftCommon_GroundAirColl_MF,
                             fp->cur_anim_frame, 1.0F, 0.0F, NULL);
    ftCommon_8007D5D4(fp);
    ftKx_FV(fp)->air_side_used = 1;
}

/* ---- the dash ------------------------------------------------------------------------------- */

/* SpecialSAttack_EnterAirOrGround: launch with speed lerped by the charge, spending it. */
static void ftKx_SpecialSAttack_EnterAirOrGround(HSD_GObj* gobj)
{
Fighter* fp = GET_FIGHTER(gobj);
    ftKnuckles_DatAttrs* da = ftKx_DA(fp);
    ftKnuckles_FighterVars* fv = ftKx_FV(fp);
    float ratio;
    FtMotionId msid;
    if (fv->specials_charge >= da->specials_max_charge) {
        msid = fp->ground_or_air == GA_Ground ? ftKx_MS_SpecialSMax : ftKx_MS_SpecialAirSMax;
    } else {
        msid = fp->ground_or_air == GA_Ground ? ftKx_MS_SpecialS : ftKx_MS_SpecialAirS;
    }
    Fighter_ChangeMotionState(gobj, msid, Ft_MF_None, 0.0F, 1.0F, 0.0F, NULL);
    ratio = (float) fv->specials_charge / (float) da->specials_max_charge;
    ftKx_MV(fp)->specials.charge_ratio = ratio;
    fp->self_vel.x = ((da->specials_max_speed - da->specials_min_speed) * ratio +
                     da->specials_min_speed) * fp->facing_dir;
    fp->self_vel.y = 0.0F;
    fv->specials_charge = 2;
    ftKx_SetEffectCallbacks(fp, ftKx_SpecialS_OnHit);
    fp->deal_dmg_cb = ftKx_SpecialS_GiveDamage;
    fp->cmd_vars[0] = 0;
}

/* SpecialS_ApplyFriction: scale x velocity down each frame, stopping under 0.5. */
static void ftKx_SpecialS_ApplyFriction(HSD_GObj* gobj)
{
Fighter* fp = GET_FIGHTER(gobj);
    fp->self_vel.x = fp->self_vel.x * (1.0F - ftKx_DA(fp)->specials_friction);
}

void ftKx_SpecialS_Anim(HSD_GObj* gobj)
{
Fighter* fp = GET_FIGHTER(gobj);
    if (!ftAnim_IsFramesRemaining(gobj)) {
        fp->gr_vel = 0.0F;
        ft_8008A2BC(gobj);
        ftKx_FV(fp)->air_side_used = 0;
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
    if (!ftAnim_IsFramesRemaining(gobj)) ftCo_Fall_Enter(gobj);
    if (!(fp->cur_anim_frame < 25.0F)) return;
    if (fp->facing_dir == -1.0F && fp->input.lstick[0].x > 0.5F) {
        if (ftWalkCommon_800DFC70(gobj) != 1) {
            fp->self_vel.x = (float) (fp->facing_dir * 0.85);
            Fighter_ChangeMotionState(gobj, ftKx_MS_SpecialAirSTurn, Ft_MF_None,
                                     0.0F, 1.0F, 0.0F, NULL);
        }
        if (!(fp->cur_anim_frame < 25.0F)) return;
    }
    if (fp->facing_dir == 1.0F && !(fp->input.lstick[0].x >= -0.5F) &&
        ftWalkCommon_800DFC70(gobj) != 1) {
        fp->self_vel.x = (float) (fp->facing_dir * 0.85);
        Fighter_ChangeMotionState(gobj, ftKx_MS_SpecialAirSTurn, Ft_MF_None,
                                 0.0F, 1.0F, 0.0F, NULL);
    }
}

void ftKx_SpecialAirS_IASA(HSD_GObj* gobj) {
Fighter* fp = GET_FIGHTER(gobj);
    float frame = fp->cur_anim_frame;
    if (!(frame > 2.0F)) goto fall;
    if (!(frame < 25.0F)) goto legacy_decel;
    if (fp->facing_dir == 1.0F && fp->input.lstick[0].x >= 0.85) {
        if (ftWalkCommon_800DFC70(gobj) != 0) {
            fp->self_vel.x = (float) (fp->facing_dir * 1.6);
            Fighter_ChangeMotionState(gobj, ftKx_MS_SpecialAirS, Ft_MF_None,
                                     0.0F, 1.0F, 0.0F, NULL);
        }
        frame = fp->cur_anim_frame;
        if (!(frame > 2.0F)) goto fall;
        if (!(frame < 25.0F)) goto legacy_decel;
    }
    if (fp->facing_dir == -1.0F && !(fp->input.lstick[0].x > -0.85) &&
        ftWalkCommon_800DFC70(gobj) != 0) {
        fp->self_vel.x = (float) (fp->facing_dir * 1.6);
        Fighter_ChangeMotionState(gobj, ftKx_MS_SpecialAirS, Ft_MF_None,
                                 0.0F, 1.0F, 0.0F, NULL);
        goto fall;
    }
    if (fp->cur_anim_frame <= 25.0F) goto fall;
legacy_decel:
    /* +1A54 passes the GObj to a Fighter-only acceleration helper. It does not
     * update this fighter's acceleration. The guest heap side effect needs its
     * own check before this port can be declared ready. */
fall:
    if (ftCo_Fall_IASA_Inner(gobj)) {
        ftCo_Fall_IASA_Inner(gobj);
        if (ftCo_800CB870(gobj) && ftCo_Jump_GetInput(gobj)) {
            Fighter_ChangeMotionState(gobj, ftCo_MS_JumpAerialF, Ft_MF_None,
                                     0.0F, 1.0F, 0.0F, NULL);
        }
    }
}

void ftKx_SpecialAirS_Phys(HSD_GObj* gobj)
{
Fighter* fp = GET_FIGHTER(gobj);
    ftKnuckles_DatAttrs* da = ftKx_DA(fp);
    ftCommon_Fall(fp, da->specials_air_gravity, da->specials_air_terminal_vel);
    if (fp->cur_anim_frame > 20.0F) ftKx_SpecialS_ApplyFriction(gobj);
    if (fp->cur_anim_frame < 10.0F) fp->self_vel.x = (float) (fp->facing_dir * 1.2);
}

void ftKx_SpecialAirS_Coll(HSD_GObj* gobj)
{
    if (ftKx_AirSideCollision(gobj)) ftWallJump_8008169C(gobj);
    if (ft_80081D0C(gobj) == true) ftKx_SpecialS_Trans(gobj);
    ftCo_800C3A14(gobj);
    ftCliffCommon_80081298(gobj);
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
                             ftCommon_GroundAirColl_MF | Ft_MF_SkipHit,
                             fp->cur_anim_frame, 1.0F, 0.0F, NULL);
    ftCommon_8007D5D4(fp);
    ftKx_FV(fp)->air_side_used = 1;
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
/* PlKx +2A64..+2C50: separate air turnaround state. */
void ftKx_SpecialAirSTurn_Anim(HSD_GObj* gobj)
{
    if (!ftAnim_IsFramesRemaining(gobj)) {
        Fighter_ChangeMotionState(gobj, ftKx_MS_SpecialAirS,
                                 ftCommon_GroundAirColl_MF | Ft_MF_SkipHit,
                                 0.0F, 1.0F, 0.0F, NULL);
    }
}

void ftKx_SpecialAirSTurn_IASA(HSD_GObj* gobj)
{
    Fighter* fp = GET_FIGHTER(gobj);
    if (fp->input.pressed_buttons & HSD_PAD_A) ftCo_Fall_IASA_Inner(gobj);
    if (ftCo_800CB870(gobj) && ftCo_Jump_GetInput(gobj)) {
        Fighter_ChangeMotionState(gobj, ftCo_MS_JumpAerialF, Ft_MF_None,
                                 0.0F, 1.0F, 0.0F, NULL);
    }
}

void ftKx_SpecialAirSTurn_Phys(HSD_GObj* gobj)
{
    Fighter* fp = GET_FIGHTER(gobj);
    fp->self_vel.x = (float) (fp->facing_dir * 0.85);
    fp->self_vel.y = -0.45F;
    /* +2B94 has the same GObj/Fighter mismatch as +1A54; preserve fighter
     * acceleration and keep the guest heap consequence open for verification. */
    if (fp->cmd_vars[3] != 0) {
        fp->cmd_vars[3] = 0;
        fp->facing_dir = fp->facing_dir == 1.0F ? -1.0F : 1.0F;
    }
}

void ftKx_SpecialAirSTurn_Coll(HSD_GObj* gobj)
{
    if (ftKx_AirSideCollision(gobj)) ftWallJump_8008169C(gobj);
    if (ft_80081D0C(gobj) == true) ftKx_SpecialS_Trans(gobj);
}
