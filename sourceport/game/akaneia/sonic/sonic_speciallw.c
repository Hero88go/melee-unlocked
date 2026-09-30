/* Akaneia's Sonic: down special, the spin charge.
 *
 * Start -> Charge: each B press adds a charge level (0..2, rate-limited) and refreshes a
 * release timer; letting the timer lapse without pressing cancels (End), tilting the stick up
 * out of the crouch (above -0.5) or holding for 300 frames releases. Released on the ground he
 * rolls (Run) at a speed picked by the level for 100 frames, able to turn around (RunTurn),
 * jump (RunJump, which keeps the spin hitbox and allows aerials, up B and the double jump),
 * roll off edges (RunJump), stop at walls (StopWall) or brake (RunBrake). Released in the air
 * he dives (Dive) and rolls on landing. Hit damage scales with the level.
 * Hand-written from PlSn.dat's ftFunction code; see NOTES.md. */
#include "sonic.h"

#include <melee/ef/eflib.h>
#include <melee/ef/efsync.h>
#include <melee/ft/fighter.h>
#include <melee/ft/ftdata.h>
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
#include <melee/ft/kinds/ftCommon/ftCo_JumpAerial.h>
#include <melee/ft/kinds/ftCommon/ftCo_Landing.h>
#include <melee/ft/types.h>
#include <melee/lb/lb_00B0.h>
#include <melee/mp/mpcoll.h>
#include <sysdolphin/baselib/controller.h>

static void ftSn_SpecialLwCharge_Enter(HSD_GObj* gobj);
static void ftSn_SpecialLwEnd_Enter(HSD_GObj* gobj);
static void ftSn_SpecialAirLwEnd_Enter(HSD_GObj* gobj);
static void ftSn_SpecialLw_EnterRun(HSD_GObj* gobj);
static void ftSn_SpecialLwRun_Enter(HSD_GObj* gobj);
static void ftSn_SpecialLwDive_Enter(HSD_GObj* gobj);
static void ftSn_SpecialLwRunBrake_Enter(HSD_GObj* gobj);
static void ftSn_SpecialLwStopWall_Enter(HSD_GObj* gobj);
static void ftSn_SpecialLwStart_Trans(HSD_GObj* gobj);
static void ftSn_SpecialAirLwStart_Trans(HSD_GObj* gobj);
static void ftSn_SpecialLwEnd_Trans(HSD_GObj* gobj);
static void ftSn_SpecialAirLwEnd_Trans(HSD_GObj* gobj);
static void ftSn_SpecialLwRun_Trans(HSD_GObj* gobj);
static void ftSn_SpecialLwRunTurn_Trans(HSD_GObj* gobj);
static void ftSn_SpecialLwRunJump_Trans(HSD_GObj* gobj, bool left_ground);
static void ftSn_SpecialLw_OnHit(HSD_GObj* gobj);

/* ---- helpers -------------------------------------------------------------------------------- */

/* p_ftCommonData->x224 is an int (20 on the disc) that the game compares as one; the m-ex code
 * was built with it typed as a float and loads the same bits with lfs, giving a denormal
 * (about 2.8e-44). Reproduced bit for bit; see NOTES.md. */
static inline float ftSn_CommonData_x224_AsFloat(void)
{
    union {
        s32 i;
        float f;
    } bits;
    bits.i = p_ftCommonData->x224;
    return bits.f;
}

/* Fighter_IASACheck_UpSpecial: ftCo_SpecialAir_CheckInput cut down to up and neutral B (side
 * and down B do nothing here). Neutral B keeps the common B-reverse. */
static bool ftSn_IASACheck_UpSpecial(HSD_GObj* gobj)
{
    Fighter* fp = GET_FIGHTER(gobj);
    float stick_x;
    float stick_y;

    if (!(fp->input.pressed_buttons & HSD_PAD_B)) {
        return false;
    }
    stick_x = fp->input.lstick[0].x;
    stick_y = fp->input.lstick[0].y;

    if (!(stick_y < p_ftCommonData->x21C)) {
        ftSn_SpecialHi_Enter(gobj);
        fp->x2227_b5 = true;
        return true;
    }
    if (stick_y <= -p_ftCommonData->x21C) {
        return false;
    }
    if (stick_x <= -p_ftCommonData->x218) {
        return false;
    }
    if (!(stick_x < p_ftCommonData->x218)) {
        return false;
    }

    if ((float) fp->active_duration.lstick.x < ftSn_CommonData_x224_AsFloat()) {
        if (fp->facing_dir == -1.0F) {
            if (fp->x2228_b7) {
                fp->facing_dir = -fp->facing_dir;
            }
        } else if (fp->facing_dir == 1.0F) {
            if (!fp->x2228_b7) {
                fp->facing_dir = -fp->facing_dir;
            }
        }
    }
    ftSn_SpecialNStart_Enter(gobj);
    fp->x2227_b5 = true;
    return true;
}

/* SpecialLw_AdjustHitboxDamage: hitbox 0's damage goes from min at level 0 to max at the top
 * level. Only while that hitbox is active. */
static void ftSn_SpecialLw_AdjustHitboxDamage(HSD_GObj* gobj, float min, float max)
{
    Fighter* fp = GET_FIGHTER(gobj);

    if (fp->x914[0].state == HitCapsule_Disabled) {
        return;
    }
    fp->x914[0].damage = ((float) ftSn_MV(fp)->speciallw.charge_level /
                          (float) ftSn_DA(fp)->speciallw_max_charge_level) *
                             (max - min) +
                         min;
}

/* SpecialLwRun_DecrementLife: both the roll and dive timers tick down together. */
static void ftSn_SpecialLwRun_DecrementLife(HSD_GObj* gobj)
{
    ftSonic_SpecialLwVars* mv = &ftSn_MV(GET_FIGHTER(gobj))->speciallw;

    if (mv->run_life > 0) {
        mv->run_life--;
    }
    if (mv->dive_life > 0) {
        mv->dive_life--;
    }
}

/* The trail keeps running if it already is, otherwise it starts over. */
static void ftSn_SpecialLw_KeepTrail(Fighter* fp, HSD_GObjEvent prev)
{
    fp->accessory4_cb = prev == ftSn_GFXTrailLoop ? prev : ftSn_GFXTrail;
}

/* ---- entry ---------------------------------------------------------------------------------- */

/* SpecialLw_EnterAirOrGround: speciallw and specialairlw */
void ftSn_SpecialLw_EnterAirOrGround(HSD_GObj* gobj)
{
    Fighter* fp = GET_FIGHTER(gobj);
    ftSonic_SpecialLwVars* mv = &ftSn_MV(fp)->speciallw;

    if (fp->ground_or_air == GA_Air) {
        Fighter_ChangeMotionState(gobj, ftSn_MS_SpecialAirLwStart, Ft_MF_None, 0.0F, 1.0F, 0.0F,
                                  NULL);
    } else {
        Fighter_ChangeMotionState(gobj, ftSn_MS_SpecialLwStart, Ft_MF_None, 0.0F, 1.0F, 0.0F,
                                  NULL);
    }
    mv->left_ground = false;
    mv->can_jump = fp->x1968_jumpsUsed < fp->co_attrs.max_jumps;
    mv->run_life = ftSn_DA(fp)->speciallw_run_frames;
    ftSn_SetEffectCallbacks(fp, ftSn_SpecialLw_OnHit);
}

/* SpecialLw_OnHit */
static void ftSn_SpecialLw_OnHit(HSD_GObj* gobj)
{
    efLib_DestroyAll(gobj);
}

/* ---- Start / End (ground and air) ----------------------------------------------------------- */

void ftSn_SpecialLwStart_Anim(HSD_GObj* gobj)
{
    if (!ftAnim_IsFramesRemaining(gobj)) {
        ftSn_SpecialLwCharge_Enter(gobj);
    }
}

void ftSn_SpecialLwStart_IASA(HSD_GObj* gobj) {}

void ftSn_SpecialLwStart_Phys(HSD_GObj* gobj)
{
    ft_80084F3C(gobj);
}

void ftSn_SpecialLwStart_Coll(HSD_GObj* gobj)
{
    if (!ft_80082708(gobj)) {
        ftSn_SpecialAirLwStart_Trans(gobj);
    }
}

void ftSn_SpecialLwEnd_Anim(HSD_GObj* gobj)
{
    if (!ftAnim_IsFramesRemaining(gobj)) {
        ft_8008A2BC(gobj);
    }
}

void ftSn_SpecialLwEnd_IASA(HSD_GObj* gobj) {}

void ftSn_SpecialLwEnd_Phys(HSD_GObj* gobj)
{
    ft_80084F3C(gobj);
}

void ftSn_SpecialLwEnd_Coll(HSD_GObj* gobj)
{
    if (!ft_80082708(gobj)) {
        ftSn_SpecialAirLwEnd_Trans(gobj);
    }
}

void ftSn_SpecialAirLwStart_Anim(HSD_GObj* gobj)
{
    if (!ftAnim_IsFramesRemaining(gobj)) {
        ftSn_SpecialLwCharge_Enter(gobj);
    }
}

void ftSn_SpecialAirLwStart_IASA(HSD_GObj* gobj) {}

void ftSn_SpecialAirLwStart_Phys(HSD_GObj* gobj)
{
    ft_80084EEC(gobj);
}

void ftSn_SpecialAirLwStart_Coll(HSD_GObj* gobj)
{
    Fighter* fp = GET_FIGHTER(gobj);
    ftCollisionBox box = ftSn_CollBox(fp);

    if (ft_800824A0(gobj, &box) == true) {
        ftSn_SpecialLwStart_Trans(gobj);
    }
}

void ftSn_SpecialAirLwEnd_Anim(HSD_GObj* gobj)
{
    if (!ftAnim_IsFramesRemaining(gobj)) {
        ftCo_FallAerial_Enter(gobj);
    }
}

void ftSn_SpecialAirLwEnd_IASA(HSD_GObj* gobj) {}

void ftSn_SpecialAirLwEnd_Phys(HSD_GObj* gobj)
{
    ft_80084EEC(gobj);
}

void ftSn_SpecialAirLwEnd_Coll(HSD_GObj* gobj)
{
    Fighter* fp = GET_FIGHTER(gobj);
    ftCollisionBox box = ftSn_CollBox(fp);

    if (ft_800824A0(gobj, &box) == true) {
        ftSn_SpecialLwEnd_Trans(gobj);
    }
}

static void ftSn_SpecialLwEnd_Enter(HSD_GObj* gobj)
{
    Fighter_ChangeMotionState(gobj, ftSn_MS_SpecialLwEnd, Ft_MF_None, 0.0F, 1.0F, 0.0F, NULL);
    ftSn_SetEffectCallbacks(GET_FIGHTER(gobj), ftSn_SpecialLw_OnHit);
}

static void ftSn_SpecialAirLwEnd_Enter(HSD_GObj* gobj)
{
    Fighter_ChangeMotionState(gobj, ftSn_MS_SpecialAirLwEnd, Ft_MF_None, 0.0F, 1.0F, 0.0F,
                              NULL);
    ftSn_SetEffectCallbacks(GET_FIGHTER(gobj), ftSn_SpecialLw_OnHit);
}

/* The ground/air swaps keep the frame. Only the two Start swaps change the fighter's ground
 * state; the End swaps leave it to the next collision (as shipped). */
static void ftSn_SpecialAirLwStart_Trans(HSD_GObj* gobj)
{
    Fighter* fp = GET_FIGHTER(gobj);

    ftCommon_8007D5D4(fp);
    Fighter_ChangeMotionState(gobj, ftSn_MS_SpecialAirLwStart, Ft_MF_None, fp->cur_anim_frame,
                              1.0F, 0.0F, NULL);
    ftSn_SetEffectCallbacks(fp, ftSn_SpecialLw_OnHit);
}

static void ftSn_SpecialAirLwEnd_Trans(HSD_GObj* gobj)
{
    Fighter* fp = GET_FIGHTER(gobj);

    Fighter_ChangeMotionState(gobj, ftSn_MS_SpecialAirLwEnd, Ft_MF_None, fp->cur_anim_frame,
                              1.0F, 0.0F, NULL);
    ftSn_SetEffectCallbacks(fp, ftSn_SpecialLw_OnHit);
}

static void ftSn_SpecialLwStart_Trans(HSD_GObj* gobj)
{
    Fighter* fp = GET_FIGHTER(gobj);

    ftCommon_8007D7FC(fp);
    Fighter_ChangeMotionState(gobj, ftSn_MS_SpecialLwStart, Ft_MF_None, fp->cur_anim_frame,
                              1.0F, 0.0F, NULL);
    ftSn_SetEffectCallbacks(fp, ftSn_SpecialLw_OnHit);
}

static void ftSn_SpecialLwEnd_Trans(HSD_GObj* gobj)
{
    Fighter* fp = GET_FIGHTER(gobj);

    Fighter_ChangeMotionState(gobj, ftSn_MS_SpecialLwEnd, Ft_MF_None, fp->cur_anim_frame, 1.0F,
                              0.0F, NULL);
    ftSn_SetEffectCallbacks(fp, ftSn_SpecialLw_OnHit);
}

/* ---- Charge --------------------------------------------------------------------------------- */

static void ftSn_SpecialLwCharge_Enter(HSD_GObj* gobj)
{
    Fighter* fp = GET_FIGHTER(gobj);
    ftSonic_DatAttrs* da = ftSn_DA(fp);
    ftSonic_SpecialLwVars* mv = &ftSn_MV(fp)->speciallw;

    Fighter_ChangeMotionState(gobj, ftSn_MS_SpecialLwCharge, Ft_MF_None, 0.0F, 1.0F, 0.0F,
                              NULL);
    ftAnim_8006EBA4(gobj);
    ft_80088478(fp, ftSn_Sfx_SpinCharge, 0xFF, 0x40);
    mv->hold_timer = da->speciallw_max_hold_frames;
    mv->release_timer = da->speciallw_release_frames;
    mv->charge_level = -1;
    mv->charge_timer = da->speciallw_first_charge_delay;
    mv->x2354 = 0;
    ftSn_SetEffectCallbacks(fp, ftSn_SpecialLw_OnHit);
}

/* Holding past the limit releases; going too long without a B press gives up. The release
 * check runs even after the hold limit has already released (as shipped). */
void ftSn_SpecialLwCharge_Anim(HSD_GObj* gobj)
{
    Fighter* fp = GET_FIGHTER(gobj);
    ftSonic_SpecialLwVars* mv = &ftSn_MV(fp)->speciallw;

    if (mv->hold_timer > 0) {
        mv->hold_timer--;
    } else {
        ftSn_SpecialLw_EnterRun(gobj);
    }

    if (mv->release_timer > 0) {
        mv->release_timer--;
        return;
    }
    if (fp->ground_or_air == GA_Air) {
        ftSn_SpecialAirLwEnd_Enter(gobj);
    } else {
        ftSn_SpecialLwEnd_Enter(gobj);
    }
}

void ftSn_SpecialLwCharge_IASA(HSD_GObj* gobj)
{
    Fighter* fp = GET_FIGHTER(gobj);
    ftSonic_DatAttrs* da = ftSn_DA(fp);
    ftSonic_SpecialLwVars* mv = &ftSn_MV(fp)->speciallw;

    if (fp->ground_or_air == GA_Ground && ftCo_Jump_CheckInput(gobj) == true) {
        /* jump straight out of the charge */
        ftSn_SpecialLwRunJump_Trans(gobj, false);
        fp->self_vel.y = da->speciallw_jump_vel_y;
        fp->x914[0].state = HitCapsule_Disabled;
        ft_80088478(fp, ftSn_Sfx_Trail, 0xFF, 0x40);
    } else if (fp->input.lstick[0].y > -0.5F) {
        /* stick released from down: go */
        ftSn_SpecialLw_EnterRun(gobj);
    } else if (fp->input.pressed_buttons & HSD_PAD_B) {
        int level;

        mv->release_timer = da->speciallw_release_frames;
        if (mv->charge_timer == 0) {
            if (mv->charge_level < da->speciallw_max_charge_level) {
                mv->charge_level++;
            }
            mv->charge_timer = da->speciallw_charge_interval;
        }
        ft_80088478(fp, ftSn_Sfx_SpinCharge, 0xFF, 0x40);

        level = mv->charge_level;
        if ((u32) level <= 2) {
            Vec3 pos;
            lb_8000B1CC(fp->parts[FtPart_XRotN].joint, NULL, &pos);
            efSync_Spawn(ftSn_Ef_SpinLevel0 + level, gobj, &pos);
        }
    }

    /* the charge rate limiter; the first tick takes the level from -1 to 0 */
    if (mv->charge_timer > 0) {
        mv->charge_timer--;
        return;
    }
    if (mv->charge_level == -1) {
        mv->charge_level = 0;
        mv->charge_timer = da->speciallw_charge_interval;
    }
}

void ftSn_SpecialLwCharge_Phys(HSD_GObj* gobj)
{
    Fighter* fp = GET_FIGHTER(gobj);
    ftSonic_DatAttrs* da = ftSn_DA(fp);

    if (fp->ground_or_air == GA_Air) {
        ftCommon_Fall(fp, da->speciallw_charge_gravity, da->speciallw_charge_terminal_vel);
        ftCommon_CalcSelfAccel_Deaccel(fp, da->speciallw_charge_air_decel);
    } else {
        ft_80084F3C(gobj);
    }
}

/* Charge works on the ground and in the air in one state: only the ground/air flag flips. */
void ftSn_SpecialLwCharge_Coll(HSD_GObj* gobj)
{
    Fighter* fp = GET_FIGHTER(gobj);

    if (fp->ground_or_air == GA_Ground) {
        if (!ft_80082708(gobj)) {
            ftCommon_8007D5D4(fp);
        }
    } else if (fp->ground_or_air == GA_Air) {
        ftCollisionBox box = ftSn_CollBox(fp);
        if (ft_800824A0(gobj, &box) == true) {
            ftCommon_8007D7FC(fp);
        }
    }
}

/* SpecialLw_EnterRun: never charged -> End; charged -> roll on the ground, dive in the air. */
static void ftSn_SpecialLw_EnterRun(HSD_GObj* gobj)
{
    Fighter* fp = GET_FIGHTER(gobj);

    if (ftSn_MV(fp)->speciallw.charge_level < 0) {
        if (fp->ground_or_air == GA_Air) {
            ftSn_SpecialAirLwEnd_Enter(gobj);
        } else {
            ftSn_SpecialLwEnd_Enter(gobj);
        }
    } else {
        if (fp->ground_or_air == GA_Air) {
            ftSn_SpecialLwDive_Enter(gobj);
        } else {
            ftSn_SpecialLwRun_Enter(gobj);
        }
    }
}

/* ---- Run ------------------------------------------------------------------------------------ */

static void ftSn_SpecialLwRun_Enter(HSD_GObj* gobj)
{
    Fighter* fp = GET_FIGHTER(gobj);
    ftSonic_DatAttrs* da = ftSn_DA(fp);
    ftSonic_SpecialLwVars* mv = &ftSn_MV(fp)->speciallw;

    Fighter_ChangeMotionState(gobj, ftSn_MS_SpecialLwRun, Ft_MF_None, 0.0F, 1.0F, 0.0F, NULL);
    ftAnim_8006EBA4(gobj);
    mv->x2354 = 4;
    if (mv->charge_level == 1) {
        mv->run_speed = da->speciallw_run_speed_lv1;
    } else if (mv->charge_level == 2) {
        mv->run_speed = da->speciallw_run_speed_lv2;
    } else {
        mv->run_speed = da->speciallw_run_speed_lv0;
    }
    ftSn_SpecialLw_AdjustHitboxDamage(gobj, da->speciallw_run_damage_min,
                                      da->speciallw_run_damage_max);
    ftSn_SetEffectCallbacks(fp, ftSn_SpecialLw_OnHit);
    fp->accessory4_cb = ftSn_GFXTrail;
}

/* The brake is entered when the roll runs out; the timers tick either way. */
void ftSn_SpecialLwRun_Anim(HSD_GObj* gobj)
{
    Fighter* fp = GET_FIGHTER(gobj);

    if (ftSn_MV(fp)->speciallw.run_life <= 0) {
        ftSn_SpecialLwRunBrake_Enter(gobj);
    }
    ftSn_SpecialLwRun_DecrementLife(gobj);
}

void ftSn_SpecialLwRun_IASA(HSD_GObj* gobj)
{
    Fighter* fp = GET_FIGHTER(gobj);
    ftSonic_DatAttrs* da = ftSn_DA(fp);

    if (ftCo_Jump_GetInput(gobj) != JumpInput_None ||
        (fp->input.held_buttons[0] & HSD_PAD_XY))
    {
        ftSn_SpecialLwRunJump_Trans(gobj, false);
        fp->self_vel.y = da->speciallw_jump_vel_y;
        ft_80088478(fp, ftSn_Sfx_Trail, 0xFF, 0x40);
    }
}

/* Accelerate toward the level's speed; pulling the stick back past half turns around. */
void ftSn_SpecialLwRun_Phys(HSD_GObj* gobj)
{
    Fighter* fp = GET_FIGHTER(gobj);
    ftSonic_DatAttrs* da = ftSn_DA(fp);
    ftSonic_SpecialLwVars* mv = &ftSn_MV(fp)->speciallw;

    ftCommon_CalcGroundAccel_DashRun(fp, fp->facing_dir * da->speciallw_run_accel,
                                     fp->facing_dir * mv->run_speed,
                                     fp->co_attrs.ground_friction);
    ftCommon_SetSelfMovementFromGroundedMovement(gobj);

    if (fp->facing_dir == 1.0F) {
        if (fp->input.lstick[0].x < -0.5F) {
            ftSn_SpecialLwRunTurn_Trans(gobj);
        }
    } else if (fp->facing_dir == -1.0F) {
        if (fp->input.lstick[0].x > 0.5F) {
            ftSn_SpecialLwRunTurn_Trans(gobj);
        }
    }
}

/* SpecialLwInterrupt_StopWall: rolling fast into a wall on the facing side stops the roll. */
static bool ftSn_SpecialLwInterrupt_StopWall(HSD_GObj* gobj)
{
    Fighter* fp = GET_FIGHTER(gobj);
    float speed;

    if (fp->facing_dir == -1.0F) {
        if (!(fp->coll_data.env_flags & Collide_RightWallHug)) {
            return false;
        }
    } else if (fp->facing_dir == 1.0F) {
        if (!(fp->coll_data.env_flags & Collide_LeftWallHug)) {
            return false;
        }
    } else {
        return false;
    }

    speed = fp->gr_vel;
    if (speed < 0.0F) {
        speed = -speed;
    }
    if (fp->co_attrs.walk_max_vel < speed) {
        ftSn_SpecialLwStopWall_Enter(gobj);
        return true;
    }
    return false;
}

/* The ground check that lets him roll off edges (into RunJump). */
void ftSn_SpecialLwRun_Coll(HSD_GObj* gobj)
{
    Fighter* fp = GET_FIGHTER(gobj);
    CollData* coll = &fp->coll_data;
    bool grounded;

    coll->last_pos = coll->cur_pos;
    coll->cur_pos = fp->cur_pos;
    grounded = mpColl_8004B108(coll);
    fp->cur_pos = coll->cur_pos;

    if (!grounded) {
        ftSn_SpecialLwRunJump_Trans(gobj, true);
    } else {
        ftSn_SpecialLwInterrupt_StopWall(gobj);
    }
}

static void ftSn_SpecialLwRun_Trans(HSD_GObj* gobj)
{
    Fighter* fp = GET_FIGHTER(gobj);
    ftSonic_DatAttrs* da = ftSn_DA(fp);
    HSD_GObjEvent prev = fp->accessory4_cb;

    ftCommon_8007D7FC(fp);
    Fighter_ChangeMotionState(gobj, ftSn_MS_SpecialLwRun, Ft_MF_None, 0.0F, 1.0F, 0.0F, NULL);
    ftAnim_8006EBA4(gobj);
    ftSn_SpecialLw_AdjustHitboxDamage(gobj, da->speciallw_run_damage_min,
                                      da->speciallw_run_damage_max);
    ftSn_SetEffectCallbacks(fp, ftSn_SpecialLw_OnHit);
    ftSn_SpecialLw_KeepTrail(fp, prev);
}

/* ---- RunTurn -------------------------------------------------------------------------------- */

static void ftSn_SpecialLwRunTurn_Trans(HSD_GObj* gobj)
{
    Fighter* fp = GET_FIGHTER(gobj);

    fp->facing_dir = -fp->facing_dir;
    ftCommon_8007D7FC(fp);
    Fighter_ChangeMotionState(gobj, ftSn_MS_SpecialLwRunTurn, Ft_MF_None, 0.0F, 1.0F, 0.0F,
                              NULL);
    ftAnim_8006EBA4(gobj);
    ftSn_SetEffectCallbacks(fp, ftSn_SpecialLw_OnHit);
}

void ftSn_SpecialLwRunTurn_Anim(HSD_GObj* gobj) {}

void ftSn_SpecialLwRunTurn_IASA(HSD_GObj* gobj)
{
    ftSn_SpecialLwRun_DecrementLife(gobj);
}

/* Push along the new facing (on self_vel directly). */
void ftSn_SpecialLwRunTurn_Phys(HSD_GObj* gobj)
{
    Fighter* fp = GET_FIGHTER(gobj);

    fp->self_vel.x = fp->self_vel.x + ftSn_DA(fp)->speciallw_turn_accel * fp->facing_dir;
}

/* Back into the roll once moving the new way fast enough; off an edge, RunJump. */
void ftSn_SpecialLwRunTurn_Coll(HSD_GObj* gobj)
{
    Fighter* fp = GET_FIGHTER(gobj);
    ftSonic_DatAttrs* da = ftSn_DA(fp);

    if (!ft_800827A0(gobj)) {
        ftSn_SpecialLwRunJump_Trans(gobj, true);
        return;
    }
    if (fp->facing_dir == 1.0F) {
        if (fp->self_vel.x > da->speciallw_turn_exit_speed) {
            ftSn_SpecialLwRun_Trans(gobj);
        }
    } else if (fp->facing_dir == -1.0F) {
        if (fp->self_vel.x < -da->speciallw_turn_exit_speed) {
            ftSn_SpecialLwRun_Trans(gobj);
        }
    }
}

/* ---- RunJump -------------------------------------------------------------------------------- */

static void ftSn_SpecialLwRunJump_Trans(HSD_GObj* gobj, bool left_ground)
{
    Fighter* fp = GET_FIGHTER(gobj);
    ftSonic_DatAttrs* da = ftSn_DA(fp);
    ftSonic_SpecialLwVars* mv = &ftSn_MV(fp)->speciallw;
    HSD_GObjEvent prev = fp->accessory4_cb;

    ftCommon_8007D5D4(fp);
    Fighter_ChangeMotionState(gobj, ftSn_MS_SpecialLwRunJump, Ft_MF_None, 0.0F, 1.0F, 0.0F,
                              NULL);
    ftAnim_8006EBA4(gobj);

    if (fp->self_vel.x < -da->speciallw_air_max_vel_x) {
        fp->self_vel.x = -da->speciallw_air_max_vel_x;
    } else if (fp->self_vel.x > da->speciallw_air_max_vel_x) {
        fp->self_vel.x = da->speciallw_air_max_vel_x;
    }

    mv->jump_lockout = da->speciallw_jump_attack_lockout;
    mv->left_ground = left_ground;
    ftSn_SpecialLw_AdjustHitboxDamage(gobj, da->speciallw_jump_damage_min,
                                      da->speciallw_jump_damage_max);
    ftSn_SetEffectCallbacks(fp, ftSn_SpecialLw_OnHit);
    ftSn_SpecialLw_KeepTrail(fp, prev);
}

/* Out of roll time -> Fall; timers tick only while falling. */
void ftSn_SpecialLwRunJump_Anim(HSD_GObj* gobj)
{
    Fighter* fp = GET_FIGHTER(gobj);

    if (ftSn_MV(fp)->speciallw.run_life <= 0) {
        ftCo_Fall_Enter(gobj);
    }
    if (fp->self_vel.y < 0.0F) {
        ftSn_SpecialLwRun_DecrementLife(gobj);
    }
}

/* Fighter_IASA_AirAttack is always checked (m-ex calls ftCo_AttackAir_CheckItemThrowInput
 * through a pointer); up B and the double jump wait for the lockout. */
void ftSn_SpecialLwRunJump_IASA(HSD_GObj* gobj)
{
    Fighter* fp = GET_FIGHTER(gobj);
    ftSonic_SpecialLwVars* mv = &ftSn_MV(fp)->speciallw;

    if (mv->jump_lockout > 0) {
        mv->jump_lockout--;
        ftCo_AttackAir_CheckItemThrowInput(gobj);
        return;
    }
    if (ftCo_AttackAir_CheckItemThrowInput(gobj)) {
        return;
    }
    if (ftSn_IASACheck_UpSpecial(gobj)) {
        return;
    }
    if (mv->can_jump) {
        ftCo_800CB870(gobj);
    }
}

void ftSn_SpecialLwRunJump_Phys(HSD_GObj* gobj)
{
    Fighter* fp = GET_FIGHTER(gobj);
    ftSonic_DatAttrs* da = ftSn_DA(fp);
    float vel;

    ftCommon_Fall(fp, da->speciallw_air_gravity, da->speciallw_air_terminal_vel);
    ftCommon_CheckFallFast(fp);

    vel = da->speciallw_air_drift * fp->input.lstick[0].x + fp->self_vel.x;
    fp->self_vel.x = vel;
    if (vel < -da->speciallw_air_max_vel_x) {
        vel = -da->speciallw_air_max_vel_x;
    }
    fp->self_vel.x = vel;
    if (da->speciallw_air_max_vel_x < vel) {
        vel = da->speciallw_air_max_vel_x;
    }
    fp->self_vel.x = vel;
}

/* Landing: back into the roll if he rolled off an edge; otherwise land (special landing when
 * that lag is set). */
void ftSn_SpecialLwRunJump_Coll(HSD_GObj* gobj)
{
    Fighter* fp = GET_FIGHTER(gobj);
    ftSonic_DatAttrs* da = ftSn_DA(fp);
    ftCollisionBox box = ftSn_CollBox(fp);

    if (ft_800824A0(gobj, &box) != true) {
        return;
    }
    if (ftSn_MV(fp)->speciallw.left_ground) {
        ftSn_SpecialLwRun_Trans(gobj);
    } else if (da->speciallw_landing_lag == 0.0F) {
        ft_80082B1C(gobj);
    } else {
        ftCo_LandingFallSpecial_Enter(gobj, false, da->speciallw_landing_lag);
    }
}

/* ---- Dive (released in the air) ------------------------------------------------------------- */

static void ftSn_SpecialLwDive_Enter(HSD_GObj* gobj)
{
    Fighter* fp = GET_FIGHTER(gobj);
    ftSonic_DatAttrs* da = ftSn_DA(fp);

    Fighter_ChangeMotionState(gobj, ftSn_MS_SpecialLwDive, Ft_MF_None, 0.0F, 1.0F, 0.0F, NULL);
    ftAnim_8006EBA4(gobj);
    fp->self_vel.x = da->speciallw_dive_vel_x * fp->facing_dir;
    ftSn_MV(fp)->speciallw.dive_life = da->speciallw_dive_frames;
    ftSn_SetEffectCallbacks(fp, ftSn_SpecialLw_OnHit);
    fp->accessory4_cb = ftSn_GFXTrail;
}

void ftSn_SpecialLwDive_Anim(HSD_GObj* gobj)
{
    Fighter* fp = GET_FIGHTER(gobj);
    ftSonic_DatAttrs* da = ftSn_DA(fp);

    if (ftSn_MV(fp)->speciallw.dive_life <= 0) {
        ftCo_80096900(gobj, 0, 1, false, da->speciallw_air_drift, da->speciallw_landing_lag);
    }
    ftSn_SpecialLwRun_DecrementLife(gobj);
}

void ftSn_SpecialLwDive_IASA(HSD_GObj* gobj) {}

void ftSn_SpecialLwDive_Phys(HSD_GObj* gobj)
{
    Fighter* fp = GET_FIGHTER(gobj);
    ftSonic_DatAttrs* da = ftSn_DA(fp);

    ftCommon_Fall(fp, da->speciallw_air_gravity, da->speciallw_air_terminal_vel);
}

void ftSn_SpecialLwDive_Coll(HSD_GObj* gobj)
{
    Fighter* fp = GET_FIGHTER(gobj);
    ftCollisionBox box = ftSn_CollBox(fp);

    if (ft_800824A0(gobj, &box) == true) {
        ftCommon_8007D6A4(fp);
        ftSn_SpecialLwRun_Enter(gobj);
    }
}

/* ---- RunBrake ------------------------------------------------------------------------------- */

static void ftSn_SpecialLwRunBrake_Enter(HSD_GObj* gobj)
{
    Fighter_ChangeMotionState(gobj, ftSn_MS_SpecialLwRunBrake, Ft_MF_None, 0.0F, 1.0F, 0.0F,
                              NULL);
}

void ftSn_SpecialLwRunBrake_Anim(HSD_GObj* gobj)
{
    if (!ftAnim_IsFramesRemaining(gobj)) {
        ft_8008A2BC(gobj);
    }
}

void ftSn_SpecialLwRunBrake_IASA(HSD_GObj* gobj) {}

void ftSn_SpecialLwRunBrake_Phys(HSD_GObj* gobj)
{
    Fighter* fp = GET_FIGHTER(gobj);

    ftCommon_CalcGroundAccel_Deaccel(fp, ftSn_DA(fp)->speciallw_brake_decel);
    ftCommon_SetSelfMovementFromGroundedMovement(gobj);
}

void ftSn_SpecialLwRunBrake_Coll(HSD_GObj* gobj)
{
    ft_800827A0(gobj);
}

/* ---- StopWall ------------------------------------------------------------------------------- */

/* Snap flush against the wall (ECB edge plus the TransN offset) and play the bonk. */
static void ftSn_SpecialLwStopWall_Enter(HSD_GObj* gobj)
{
    Fighter* fp = GET_FIGHTER(gobj);
    ftSonic_DatAttrs* da = ftSn_DA(fp);
    float edge;

    if (fp->coll_data.env_flags & Collide_RightWallMask) {
        edge = fp->coll_data.ecb.left.x;
    } else {
        edge = fp->coll_data.ecb.right.x;
    }

    if (fp->facing_dir == -1.0F) {
        Fighter_ChangeMotionState(gobj, ftSn_MS_SpecialLwStopWallL, Ft_MF_None, 0.0F,
                                  da->speciallw_stopwall_anim_rate, 0.0F, NULL);
    } else {
        Fighter_ChangeMotionState(gobj, ftSn_MS_SpecialLwStopWallR, Ft_MF_None, 0.0F,
                                  da->speciallw_stopwall_anim_rate, 0.0F, NULL);
    }
    fp->cur_pos.x = fp->facing_dir * fp->x68C_transNPos.z + (edge + fp->cur_pos.x);
    ft_800843FC(gobj);
    ftCommon_8007E2FC(gobj);
}

void ftSn_SpecialLwStopWall_Anim(HSD_GObj* gobj)
{
    if (!ftAnim_IsFramesRemaining(gobj)) {
        ft_8008A2BC(gobj);
    }
}

void ftSn_SpecialLwStopWall_IASA(HSD_GObj* gobj) {}

void ftSn_SpecialLwStopWall_Phys(HSD_GObj* gobj) {}

void ftSn_SpecialLwStopWall_Coll(HSD_GObj* gobj)
{
    ft_800843FC(gobj);
}
