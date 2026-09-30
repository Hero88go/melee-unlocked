/* Akaneia's Tails, native: down special (spin dash).
 *
 * Start -> Charge: each B press revs (sound, particles) and, every lw_level_cooldown frames, raises
 * the charge level (up to lw_max_level). Releasing down (stick y above -0.5) or the auto-release
 * timer launches: on the ground a Run (a rolling hitbox whose damage scales with the level; speed
 * from the level), in the air a Dive. Charging without ever reaching level 0 just ends. The run can
 * turn around, jump (X/Y or jump input), roll off edges into a jump, brake when its timer runs out,
 * and bonk off walls when fast enough. Jumping out of the roll keeps aerials and up special open,
 * and the air jump if one was left when the move started. */
#include "ftTs.h"

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
#include <melee/ft/kinds/ftCommon/ftCo_JumpAerial.h>
#include <melee/ft/kinds/ftCommon/ftCo_Landing.h>
#include <melee/ft/types.h>
#include <melee/lb/forward.h>
#include <melee/lb/lb_00B0.h>
#include <melee/mp/forward.h>
#include <melee/mp/mpcoll.h>
#include <sysdolphin/baselib/psstructs.h>

/* code+0x54C4 SpecialLw_OnHit: drop the ball effects when hit. */
static void ftTs_SpecialLw_OnHit(Fighter_GObj* gobj)
{
    efLib_DestroyAll(gobj);
}

/* Every SpecialLw state installs these: effects follow hitlag and die with the move. */
static inline void ftTs_SpecialLw_SetCallbacks(Fighter* fp)
{
    fp->take_dmg_cb = ftTs_SpecialLw_OnHit;
    fp->death2_cb = ftTs_SpecialLw_OnHit;
    fp->take_dmg_2_cb = efLib_DestroyAll;
    fp->pre_hitlag_cb = efLib_PauseAll;
    fp->post_hitlag_cb = efLib_ResumeAll;
}

static inline void ftTs_SpecialLw_ChangeState(Fighter_GObj* gobj, FtMotionId msid, float frame)
{
    Fighter_ChangeMotionState(gobj, msid, Ft_MF_None, frame, 1.0f, 0.0f, NULL);
    ftTs_SpecialLw_SetCallbacks(GET_FIGHTER(gobj));
}

/* code+0x4AD8 SpecialLw_SpawnParticle: a particle generator at a bone, recoloured per costume
 * (the generator's user hook is ftTs_OnSpawnParticle). */
static void ftTs_SpecialLw_SpawnParticle(Fighter_GObj* gobj, int effect_id, int part)
{
    Fighter* fp = GET_FIGHTER(gobj);
    Vec3 pos;
    HSD_Generator* gen;

    lb_8000B1CC(fp->parts[part].joint, NULL, &pos);
    gen = efSync_Spawn(effect_id, gobj, &pos);
    if (gen != NULL) {
        gen->userfunc = &ftTs_Vars(fp)->particle_hook.funcs;
    }
}

/* code+0x60E8 SpecialLw_AdjustHitboxDamage: hitbox 0 scales from `min` (level 0) to `max`
 * (lw_max_level). */
static void ftTs_SpecialLw_AdjustHitboxDamage(Fighter_GObj* gobj, float min, float max)
{
    Fighter* fp = GET_FIGHTER(gobj);
    float t;
    float scaled;

    if (fp->x914[0].state == HitCapsule_Disabled) {
        return;
    }
    t = (float) ftTs_LwVars(fp)->level / (float) ftTs_Attrs(fp)->lw_max_level;
    scaled = t * (max - min);
    fp->x914[0].damage = scaled + min;
}

/* code+0x4B60 SpecialLwRun_DecrementLife */
static void ftTs_SpecialLwRun_DecrementLife(Fighter_GObj* gobj)
{
    ftTails_SpecialLwVars* mv = ftTs_LwVars(GET_FIGHTER(gobj));

    if (mv->run_timer > 0) {
        mv->run_timer--;
    }
    if (mv->dive_timer > 0) {
        mv->dive_timer--;
    }
}

/* code+0x4B90 SpecialLw_SpawnRunningParticles: dust every fifth frame. */
static void ftTs_SpecialLw_SpawnRunningParticles(Fighter_GObj* gobj)
{
    ftTails_SpecialLwVars* mv = ftTs_LwVars(GET_FIGHTER(gobj));

    if (mv->dust_timer > 0) {
        mv->dust_timer--;
        return;
    }
    ftTs_SpecialLw_SpawnParticle(gobj, ftTs_Pt_RollDust, 3);
    mv->dust_timer = 4;
}

/* ------------------------------------------------------------------------------------------------
 * State entries
 * --------------------------------------------------------------------------------------------- */

/* code+0x3144 SpecialLw_EnterAirOrGround (both the ground and the air export) */
void ftTs_SpecialLw_EnterAirOrGround(Fighter_GObj* gobj)
{
    Fighter* fp = GET_FIGHTER(gobj);
    ftTails_SpecialLwVars* mv = ftTs_LwVars(fp);

    if (fp->ground_or_air == GA_Air) {
        Fighter_ChangeMotionState(gobj, ftTs_MS_SpecialAirLwStart, Ft_MF_None, 0.0f, 1.0f, 0.0f,
                                  NULL);
    } else {
        Fighter_ChangeMotionState(gobj, ftTs_MS_SpecialLwStart, Ft_MF_None, 0.0f, 1.0f, 0.0f,
                                  NULL);
    }
    mv->rolled_off = false;
    mv->has_air_jump = fp->x1968_jumpsUsed < fp->co_attrs.max_jumps;
    mv->run_timer = ftTs_Attrs(fp)->lw_run_frames;
    ftTs_SpecialLw_SetCallbacks(fp);
}

/* code+0x48BC SpecialLwEnd_Enter */
static void ftTs_SpecialLwEnd_Enter(Fighter_GObj* gobj)
{
    ftTs_SpecialLw_ChangeState(gobj, ftTs_MS_SpecialLwEnd, 0.0f);
}

/* code+0x493C SpecialAirLwEnd_Enter */
static void ftTs_SpecialAirLwEnd_Enter(Fighter_GObj* gobj)
{
    ftTs_SpecialLw_ChangeState(gobj, ftTs_MS_SpecialAirLwEnd, 0.0f);
}

/* code+0x4528 SpecialLwCharge_Enter */
static void ftTs_SpecialLwCharge_Enter(Fighter_GObj* gobj)
{
    Fighter* fp = GET_FIGHTER(gobj);
    ftTails_DatAttrs* da = ftTs_Attrs(fp);
    ftTails_SpecialLwVars* mv = ftTs_LwVars(fp);

    Fighter_ChangeMotionState(gobj, ftTs_MS_SpecialLwCharge, Ft_MF_None, 0.0f, 1.0f, 0.0f, NULL);
    ftAnim_8006EBA4(gobj);
    ft_80088478(fp, ftTs_Se_Rev, 0xFF, 0x40);
    mv->auto_release = da->lw_auto_release;
    mv->idle_timer = da->lw_idle_timeout;
    mv->level = -1;
    mv->cooldown = da->lw_first_cooldown;
    mv->dust_timer = 0;
    ftTs_SpawnBallEffect(gobj, ftTs_Ef_BallRoll, ftTs_Part_Ball);
    ftTs_SpecialLw_SetCallbacks(fp);
    ftTs_InitDashTailAnim(gobj, 0.0f);
}

/* code+0x4EEC SpecialLwRun_Enter */
static void ftTs_SpecialLwRun_Enter(Fighter_GObj* gobj)
{
    Fighter* fp = GET_FIGHTER(gobj);
    ftTails_DatAttrs* da = ftTs_Attrs(fp);
    ftTails_SpecialLwVars* mv = ftTs_LwVars(fp);

    Fighter_ChangeMotionState(gobj, ftTs_MS_SpecialLwRun, Ft_MF_None, 0.0f, 1.0f, 0.0f, NULL);
    ftAnim_8006EBA4(gobj);
    ftTs_SpawnBallEffect(gobj, ftTs_Ef_BallRoll, ftTs_Part_Ball);
    mv->dust_timer = 4;
    switch (mv->level) {
    case 1:
        mv->run_speed = da->lw_run_speed_lv1;
        break;
    case 2:
        mv->run_speed = da->lw_run_speed_lv2;
        break;
    default:
        mv->run_speed = da->lw_run_speed_lv0;
        break;
    }
    ftTs_SpecialLw_AdjustHitboxDamage(gobj, da->lw_run_damage_min, da->lw_run_damage_max);
    ftTs_SpecialLw_SetCallbacks(fp);
    ftTs_InitDashTailAnim(gobj, 0.0f);
}

/* code+0x5FF4 SpecialLwDive_Enter */
static void ftTs_SpecialLwDive_Enter(Fighter_GObj* gobj)
{
    Fighter* fp = GET_FIGHTER(gobj);
    ftTails_DatAttrs* da = ftTs_Attrs(fp);

    Fighter_ChangeMotionState(gobj, ftTs_MS_SpecialLwDive, Ft_MF_None, 0.0f, 1.0f, 0.0f, NULL);
    ftAnim_8006EBA4(gobj);
    ftTs_SpawnBallEffect(gobj, ftTs_Ef_BallJump, ftTs_Part_Ball);
    fp->self_vel.x = da->lw_dive_vel_x * fp->facing_dir;
    if (da->lw_dive_vel_y != 0.0f) {
        fp->self_vel.y = da->lw_dive_vel_y;
    }
    ftTs_LwVars(fp)->dive_timer = da->lw_dive_frames;
    ftTs_SpecialLw_SetCallbacks(fp);
    ftTs_InitDashTailAnim(gobj, 0.0f);
}

/* code+0x485C SpecialLw_EnterRun: launch. Never charged: just end. */
static void ftTs_SpecialLw_EnterRun(Fighter_GObj* gobj)
{
    Fighter* fp = GET_FIGHTER(gobj);

    if (ftTs_LwVars(fp)->level < 0) {
        if (fp->ground_or_air == GA_Air) {
            ftTs_SpecialAirLwEnd_Enter(gobj);
        } else {
            ftTs_SpecialLwEnd_Enter(gobj);
        }
    } else if (fp->ground_or_air == GA_Air) {
        ftTs_SpecialLwDive_Enter(gobj);
    } else {
        ftTs_SpecialLwRun_Enter(gobj);
    }
}

/* code+0x4BE0 SpecialLwRunBrake_Enter */
static void ftTs_SpecialLwRunBrake_Enter(Fighter_GObj* gobj)
{
    Fighter_ChangeMotionState(gobj, ftTs_MS_SpecialLwRunBrake, Ft_MF_None, 0.0f, 1.0f, 0.0f,
                              NULL);
}

/* code+0x6164 SpecialLwStopWall_Enter: bonk, pushed out to the wall. */
static void ftTs_SpecialLwStopWall_Enter(Fighter_GObj* gobj)
{
    Fighter* fp = GET_FIGHTER(gobj);
    ftTails_DatAttrs* da = ftTs_Attrs(fp);
    float wall_x;
    float offset;

    if (fp->coll_data.env_flags & Collide_RightWallMask) {
        wall_x = fp->coll_data.ecb.left.x;
    } else {
        wall_x = fp->coll_data.ecb.right.x;
    }
    if (fp->facing_dir == -1.0f) {
        Fighter_ChangeMotionState(gobj, ftTs_MS_SpecialLwStopWallL, Ft_MF_None, 0.0f,
                                  da->lw_stopwall_anim_rate, 0.0f, NULL);
    } else {
        Fighter_ChangeMotionState(gobj, ftTs_MS_SpecialLwStopWallR, Ft_MF_None, 0.0f,
                                  da->lw_stopwall_anim_rate, 0.0f, NULL);
    }
    offset = fp->facing_dir * fp->x68C_transNPos.z;
    fp->cur_pos.x = offset + (wall_x + fp->cur_pos.x);
    ft_800843FC(gobj);
    ftCommon_8007E2FC(gobj);
}

/* code+0x4CE4 SpecialLwInterrupt_StopWall: running into the wall we face, faster than walking. */
static bool ftTs_SpecialLwInterrupt_StopWall(Fighter_GObj* gobj)
{
    Fighter* fp = GET_FIGHTER(gobj);
    float speed;

    if (fp->facing_dir == -1.0f) {
        if (!(fp->coll_data.env_flags & Collide_RightWallHug)) {
            return false;
        }
    } else if (fp->facing_dir == 1.0f) {
        if (!(fp->coll_data.env_flags & Collide_LeftWallHug)) {
            return false;
        }
    } else {
        return false;
    }
    speed = fp->gr_vel;
    if (speed < 0.0f) {
        speed = -speed;
    }
    if (fp->co_attrs.walk_max_vel < speed) {
        ftTs_SpecialLwStopWall_Enter(gobj);
        return true;
    }
    return false;
}

/* ------------------------------------------------------------------------------------------------
 * Ground/air swaps
 * --------------------------------------------------------------------------------------------- */

/* code+0x462C SpecialAirLwStart_Trans */
static void ftTs_SpecialAirLwStart_Trans(Fighter_GObj* gobj)
{
    Fighter* fp = GET_FIGHTER(gobj);

    ftCommon_8007D5D4(fp);
    ftTs_SpecialLw_ChangeState(gobj, ftTs_MS_SpecialAirLwStart, fp->cur_anim_frame);
}

/* code+0x4744 SpecialLwStart_Trans */
static void ftTs_SpecialLwStart_Trans(Fighter_GObj* gobj)
{
    Fighter* fp = GET_FIGHTER(gobj);

    ftCommon_8007D7FC(fp);
    ftTs_SpecialLw_ChangeState(gobj, ftTs_MS_SpecialLwStart, fp->cur_anim_frame);
}

/* code+0x46C4 SpecialAirLwEnd_Trans (no airborne call on the disc) */
static void ftTs_SpecialAirLwEnd_Trans(Fighter_GObj* gobj)
{
    ftTs_SpecialLw_ChangeState(gobj, ftTs_MS_SpecialAirLwEnd, GET_FIGHTER(gobj)->cur_anim_frame);
}

/* code+0x47DC SpecialLwEnd_Trans (no grounded call on the disc) */
static void ftTs_SpecialLwEnd_Trans(Fighter_GObj* gobj)
{
    ftTs_SpecialLw_ChangeState(gobj, ftTs_MS_SpecialLwEnd, GET_FIGHTER(gobj)->cur_anim_frame);
}

/* code+0x4D88 SpecialLwRun_Trans: back to rolling (from a turn or a jump landing). */
static void ftTs_SpecialLwRun_Trans(Fighter_GObj* gobj)
{
    Fighter* fp = GET_FIGHTER(gobj);
    ftTails_DatAttrs* da = ftTs_Attrs(fp);

    ftCommon_8007D7FC(fp);
    Fighter_ChangeMotionState(gobj, ftTs_MS_SpecialLwRun, Ft_MF_None, 0.0f, 1.0f, 0.0f, NULL);
    ftAnim_8006EBA4(gobj);
    ftTs_SpawnBallEffect(gobj, ftTs_Ef_BallRoll, ftTs_Part_Ball);
    ftTs_SpecialLw_AdjustHitboxDamage(gobj, da->lw_run_damage_min, da->lw_run_damage_max);
    ftTs_SpecialLw_SetCallbacks(fp);
    ftTs_InitDashTailAnim(gobj, 0.0f);
}

/* code+0x4C20 SpecialLwRunTurn_Trans */
static void ftTs_SpecialLwRunTurn_Trans(Fighter_GObj* gobj)
{
    Fighter* fp = GET_FIGHTER(gobj);

    fp->facing_dir = -fp->facing_dir;
    ftCommon_8007D7FC(fp);
    Fighter_ChangeMotionState(gobj, ftTs_MS_SpecialLwRunTurn, Ft_MF_None, 0.0f, 1.0f, 0.0f, NULL);
    ftAnim_8006EBA4(gobj);
    ftTs_SpecialLw_SetCallbacks(fp);
    ftTs_InitDashTailAnim(gobj, 0.0f);
}

/* code+0x49BC SpecialLwRunJump_Trans: jump (or roll off an edge) out of the run. */
static void ftTs_SpecialLwRunJump_Trans(Fighter_GObj* gobj, bool rolled_off)
{
    Fighter* fp = GET_FIGHTER(gobj);
    ftTails_DatAttrs* da = ftTs_Attrs(fp);
    ftTails_SpecialLwVars* mv = ftTs_LwVars(fp);

    ftCommon_8007D5D4(fp);
    Fighter_ChangeMotionState(gobj, ftTs_MS_SpecialLwRunJump, Ft_MF_None, 0.0f, 1.0f, 0.0f, NULL);
    ftAnim_8006EBA4(gobj);
    ftTs_SpawnBallEffect(gobj, ftTs_Ef_BallJump, ftTs_Part_Ball);
    if (fp->self_vel.x < -da->lw_jump_max_vel_x) {
        fp->self_vel.x = -da->lw_jump_max_vel_x;
    } else if (fp->self_vel.x > da->lw_jump_max_vel_x) {
        fp->self_vel.x = da->lw_jump_max_vel_x;
    }
    mv->aerial_lock = da->lw_jump_aerial_lock;
    mv->rolled_off = rolled_off;
    ftTs_SpecialLw_AdjustHitboxDamage(gobj, da->lw_jump_damage_min, da->lw_jump_damage_max);
    ftTs_SpecialLw_SetCallbacks(fp);
    ftTs_InitDashTailAnim(gobj, 0.0f);
}

/* ------------------------------------------------------------------------------------------------
 * Start / End (348..351)
 * --------------------------------------------------------------------------------------------- */

/* code+0x1A38 */
void ftTs_SpecialLwStart_Anim(Fighter_GObj* gobj)
{
    if (!ftAnim_IsFramesRemaining(gobj)) {
        ftTs_SpecialLwCharge_Enter(gobj);
    }
}

void ftTs_SpecialLwStart_IASA(Fighter_GObj* gobj) {}

/* code+0x1A7C */
void ftTs_SpecialLwStart_Phys(Fighter_GObj* gobj)
{
    ft_80084F3C(gobj);
}

/* code+0x1A9C */
void ftTs_SpecialLwStart_Coll(Fighter_GObj* gobj)
{
    if (!ft_80082708(gobj)) {
        ftTs_SpecialAirLwStart_Trans(gobj);
    }
}

/* code+0x1ADC */
void ftTs_SpecialLwEnd_Anim(Fighter_GObj* gobj)
{
    if (!ftAnim_IsFramesRemaining(gobj)) {
        ft_8008A2BC(gobj);
    }
}

void ftTs_SpecialLwEnd_IASA(Fighter_GObj* gobj) {}

/* code+0x1B20 */
void ftTs_SpecialLwEnd_Phys(Fighter_GObj* gobj)
{
    ft_80084F3C(gobj);
}

/* code+0x1B40 */
void ftTs_SpecialLwEnd_Coll(Fighter_GObj* gobj)
{
    if (!ft_80082708(gobj)) {
        ftTs_SpecialAirLwEnd_Trans(gobj);
    }
}

/* code+0x1B80 */
void ftTs_SpecialAirLwStart_Anim(Fighter_GObj* gobj)
{
    if (!ftAnim_IsFramesRemaining(gobj)) {
        ftTs_SpecialLwCharge_Enter(gobj);
    }
}

void ftTs_SpecialAirLwStart_IASA(Fighter_GObj* gobj) {}

/* code+0x1BC4 */
void ftTs_SpecialAirLwStart_Phys(Fighter_GObj* gobj)
{
    ft_80084EEC(gobj);
}

/* code+0x1BE4 */
void ftTs_SpecialAirLwStart_Coll(Fighter_GObj* gobj)
{
    ftCollisionBox ecb = ftCollisionBox_FromDisc(&ftTs_Attrs(GET_FIGHTER(gobj))->lw_air_ecb);

    if (ft_800824A0(gobj, &ecb)) {
        ftTs_SpecialLwStart_Trans(gobj);
    }
}

/* code+0x1C30 */
void ftTs_SpecialAirLwEnd_Anim(Fighter_GObj* gobj)
{
    if (!ftAnim_IsFramesRemaining(gobj)) {
        ftCo_FallAerial_Enter(gobj);
    }
}

void ftTs_SpecialAirLwEnd_IASA(Fighter_GObj* gobj) {}

/* code+0x1C74 */
void ftTs_SpecialAirLwEnd_Phys(Fighter_GObj* gobj)
{
    ft_80084EEC(gobj);
}

/* code+0x1C94 */
void ftTs_SpecialAirLwEnd_Coll(Fighter_GObj* gobj)
{
    ftCollisionBox ecb = ftCollisionBox_FromDisc(&ftTs_Attrs(GET_FIGHTER(gobj))->lw_air_ecb);

    if (ft_800824A0(gobj, &ecb)) {
        ftTs_SpecialLwEnd_Trans(gobj);
    }
}

/* ------------------------------------------------------------------------------------------------
 * Charge (352)
 * --------------------------------------------------------------------------------------------- */

/* code+0x1CE0: auto-release, and the idle timeout (no B press for lw_idle_timeout frames). The
 * disc keeps checking the idle timer after an auto-release changed state; so does this. */
void ftTs_SpecialLwCharge_Anim(Fighter_GObj* gobj)
{
    Fighter* fp = GET_FIGHTER(gobj);
    ftTails_SpecialLwVars* mv = ftTs_LwVars(fp);

    if (mv->auto_release > 0) {
        mv->auto_release--;
    } else {
        ftTs_SpecialLw_EnterRun(gobj);
    }

    if (mv->idle_timer > 0) {
        mv->idle_timer--;
    } else if (fp->ground_or_air == GA_Air) {
        ftTs_SpecialAirLwEnd_Enter(gobj);
    } else {
        ftTs_SpecialLwEnd_Enter(gobj);
    }
}

/* code+0x1D80 */
void ftTs_SpecialLwCharge_IASA(Fighter_GObj* gobj)
{
    Fighter* fp = GET_FIGHTER(gobj);
    ftTails_DatAttrs* da = ftTs_Attrs(fp);
    ftTails_SpecialLwVars* mv = ftTs_LwVars(fp);

    if (fp->ground_or_air == GA_Ground && ftCo_Jump_CheckInput(gobj)) {
        /* Jump straight out of the charge. */
        mv->run_timer = da->lw_run_frames;
        ftTs_SpecialLwRunJump_Trans(gobj, false);
        fp->self_vel.y = da->lw_jump_vel_y;
        fp->x914[0].state = HitCapsule_Disabled;
        ft_80088478(fp, ftTs_Se_Jump, 0xFF, 0x40);
    } else if (fp->input.lstick[0].y > -0.5f) {
        /* Down released: go. */
        ftTs_SpecialLw_EnterRun(gobj);
    } else if (fp->input.pressed_buttons & HSD_PAD_B) {
        /* A rev. */
        mv->idle_timer = da->lw_idle_timeout;
        if (mv->cooldown == 0) {
            if (mv->level < da->lw_max_level) {
                mv->level++;
            }
            mv->cooldown = da->lw_level_cooldown;
        }
        ft_80088478(fp, ftTs_Se_Rev, 0xFF, 0x40);
        if ((u32) mv->level <= 2) {
            ftTs_SpecialLw_SpawnParticle(gobj, ftTs_Pt_ChargeLevel0 + mv->level, 2);
        }
    }

    if (mv->cooldown > 0) {
        mv->cooldown--;
    } else if (mv->level == -1) {
        mv->level = 0;
        mv->cooldown = da->lw_level_cooldown;
    }
}

/* code+0x1F1C */
void ftTs_SpecialLwCharge_Phys(Fighter_GObj* gobj)
{
    Fighter* fp = GET_FIGHTER(gobj);
    ftTails_DatAttrs* da;

    if (fp->ground_or_air != GA_Air) {
        ft_80084F3C(gobj);
        return;
    }
    da = ftTs_Attrs(fp);
    ftCommon_Fall(fp, da->lw_charge_gravity, da->lw_charge_terminal_vel);
    ftCommon_CalcSelfAccel_Deaccel(fp, da->lw_charge_x_decel);
}

/* code+0x1F80: the charge stays the same state on either ground or air. */
void ftTs_SpecialLwCharge_Coll(Fighter_GObj* gobj)
{
    Fighter* fp = GET_FIGHTER(gobj);

    if (fp->ground_or_air == GA_Ground) {
        if (!ft_80082708(gobj)) {
            ftCommon_8007D5D4(fp);
            return;
        }
    }
    if (fp->ground_or_air == GA_Air) {
        ftCollisionBox ecb = ftCollisionBox_FromDisc(&ftTs_Attrs(fp)->lw_air_ecb);

        if (ft_800824A0(gobj, &ecb)) {
            ftCommon_8007D7FC(fp);
        }
    }
}

/* ------------------------------------------------------------------------------------------------
 * Run (353), RunTurn (354)
 * --------------------------------------------------------------------------------------------- */

/* code+0x2038 */
void ftTs_SpecialLwRun_Anim(Fighter_GObj* gobj)
{
    if (ftTs_LwVars(GET_FIGHTER(gobj))->run_timer <= 0) {
        ftTs_SpecialLwRunBrake_Enter(gobj);
    }
    ftTs_SpecialLwRun_DecrementLife(gobj);
    ftTs_SpecialLw_SpawnRunningParticles(gobj);
}

/* code+0x2088: X, Y or any jump input jumps out of the roll. */
void ftTs_SpecialLwRun_IASA(Fighter_GObj* gobj)
{
    Fighter* fp = GET_FIGHTER(gobj);

    if (ftCo_Jump_GetInput(gobj) != JumpInput_None ||
        (fp->input.held_buttons[0] & HSD_PAD_XY))
    {
        ftTs_SpecialLwRunJump_Trans(gobj, false);
        fp->self_vel.y = ftTs_Attrs(fp)->lw_jump_vel_y;
        ft_80088478(fp, ftTs_Se_Jump, 0xFF, 0x40);
    }
}

/* code+0x2108 */
void ftTs_SpecialLwRun_Phys(Fighter_GObj* gobj)
{
    Fighter* fp = GET_FIGHTER(gobj);
    float facing = fp->facing_dir;

    ftCommon_CalcGroundAccel_DashRun(fp, facing * ftTs_Attrs(fp)->lw_run_accel,
                                     facing * ftTs_LwVars(fp)->run_speed,
                                     fp->co_attrs.ground_friction);
    ftCommon_SetSelfMovementFromGroundedMovement(gobj);

    if (fp->facing_dir == 1.0f) {
        if (fp->input.lstick[0].x < -0.5f) {
            ftTs_SpecialLwRunTurn_Trans(gobj);
        }
    } else if (fp->facing_dir == -1.0f) {
        if (fp->input.lstick[0].x > 0.5f) {
            ftTs_SpecialLwRunTurn_Trans(gobj);
        }
    }
}

/* code+0x21C0: ground collision that lets the roll leave edges (then it becomes a jump). */
void ftTs_SpecialLwRun_Coll(Fighter_GObj* gobj)
{
    Fighter* fp = GET_FIGHTER(gobj);
    CollData* coll = &fp->coll_data;
    bool on_ground;

    coll->last_pos = coll->cur_pos;
    coll->cur_pos = fp->cur_pos;
    on_ground = mpColl_8004B108(coll);
    fp->cur_pos = coll->cur_pos;
    if (!on_ground) {
        ftTs_SpecialLwRunJump_Trans(gobj, true);
    } else {
        ftTs_SpecialLwInterrupt_StopWall(gobj);
    }
}

void ftTs_SpecialLwRunTurn_Anim(Fighter_GObj* gobj) {}

/* code+0x2268 */
void ftTs_SpecialLwRunTurn_IASA(Fighter_GObj* gobj)
{
    ftTs_SpecialLwRun_DecrementLife(gobj);
}

/* code+0x2288 */
void ftTs_SpecialLwRunTurn_Phys(Fighter_GObj* gobj)
{
    Fighter* fp = GET_FIGHTER(gobj);
    float accel = ftTs_Attrs(fp)->lw_turn_accel * fp->facing_dir;

    fp->self_vel.x = fp->self_vel.x + accel;
}

/* code+0x22AC: once back up to speed the other way, roll on. */
void ftTs_SpecialLwRunTurn_Coll(Fighter_GObj* gobj)
{
    Fighter* fp = GET_FIGHTER(gobj);
    ftTails_DatAttrs* da = ftTs_Attrs(fp);

    if (!ft_800827A0(gobj)) {
        ftTs_SpecialLwRunJump_Trans(gobj, true);
        return;
    }
    if (fp->facing_dir == 1.0f) {
        if (fp->self_vel.x > da->lw_turn_exit_speed) {
            ftTs_SpecialLwRun_Trans(gobj);
        }
    } else if (fp->facing_dir == -1.0f) {
        if (fp->self_vel.x < -da->lw_turn_exit_speed) {
            ftTs_SpecialLwRun_Trans(gobj);
        }
    }
}

/* ------------------------------------------------------------------------------------------------
 * RunJump (355), Dive (356)
 * --------------------------------------------------------------------------------------------- */

/* code+0x235C: the roll timer only runs down while falling. */
void ftTs_SpecialLwRunJump_Anim(Fighter_GObj* gobj)
{
    Fighter* fp = GET_FIGHTER(gobj);

    if (ftTs_LwVars(fp)->run_timer <= 0) {
        ftCo_Fall_Enter(gobj);
    }
    if (fp->self_vel.y < 0.0f) {
        ftTs_SpecialLwRun_DecrementLife(gobj);
    }
}

/* code+0x23D4. The console reaches ftCo_AttackAir_CheckItemThrowInput through a pointer
 * (code+0x4E6C "Fighter_IASA_AirAttack") in both branches; while the lock runs it only ignores the
 * result, so aerials stay available during the lock exactly as on the disc. */
void ftTs_SpecialLwRunJump_IASA(Fighter_GObj* gobj)
{
    Fighter* fp = GET_FIGHTER(gobj);
    ftTails_SpecialLwVars* mv = ftTs_LwVars(fp);

    if (mv->aerial_lock > 0) {
        mv->aerial_lock--;
        ftCo_AttackAir_CheckItemThrowInput(gobj);
        return;
    }
    if (ftCo_AttackAir_CheckItemThrowInput(gobj)) {
        return;
    }
    if (ftTs_Fighter_IASACheck_UpSpecial(gobj)) {
        return;
    }
    if (mv->has_air_jump) {
        ftCo_800CB870(gobj);
    }
}

/* code+0x246C */
void ftTs_SpecialLwRunJump_Phys(Fighter_GObj* gobj)
{
    Fighter* fp = GET_FIGHTER(gobj);
    ftTails_DatAttrs* da = ftTs_Attrs(fp);
    float drift;

    ftCommon_Fall(fp, da->lw_jump_gravity, da->lw_jump_terminal_vel);
    ftCommon_CheckFallFast(fp);
    drift = da->lw_jump_drift * fp->input.lstick[0].x;
    fp->self_vel.x = drift + fp->self_vel.x;
    if (fp->self_vel.x < -da->lw_jump_max_vel_x) {
        fp->self_vel.x = -da->lw_jump_max_vel_x;
    }
    if (!(da->lw_jump_max_vel_x >= fp->self_vel.x)) {
        fp->self_vel.x = da->lw_jump_max_vel_x;
    }
}

/* code+0x2504: rolled off an edge -> keep rolling; jumped -> land (normal or special lag). */
void ftTs_SpecialLwRunJump_Coll(Fighter_GObj* gobj)
{
    Fighter* fp = GET_FIGHTER(gobj);
    ftTails_DatAttrs* da = ftTs_Attrs(fp);
    ftCollisionBox ecb = ftCollisionBox_FromDisc(&da->lw_air_ecb);

    if (!ft_800824A0(gobj, &ecb)) {
        return;
    }
    if (ftTs_LwVars(fp)->rolled_off) {
        ftTs_SpecialLwRun_Trans(gobj);
    } else if (da->lw_jump_landing_lag == 0.0f) {
        ft_80082B1C(gobj);
    } else {
        ftCo_LandingFallSpecial_Enter(gobj, false, da->lw_jump_landing_lag);
    }
}

/* code+0x259C */
void ftTs_SpecialLwDive_Anim(Fighter_GObj* gobj)
{
    Fighter* fp = GET_FIGHTER(gobj);
    ftTails_DatAttrs* da = ftTs_Attrs(fp);

    if (ftTs_LwVars(fp)->dive_timer <= 0) {
        ftCo_80096900(gobj, 0, 1, false, da->lw_jump_drift, da->lw_jump_landing_lag);
    }
    ftTs_SpecialLwRun_DecrementLife(gobj);
}

void ftTs_SpecialLwDive_IASA(Fighter_GObj* gobj) {}

/* code+0x2600 */
void ftTs_SpecialLwDive_Phys(Fighter_GObj* gobj)
{
    Fighter* fp = GET_FIGHTER(gobj);
    ftTails_DatAttrs* da = ftTs_Attrs(fp);

    ftCommon_Fall(fp, da->lw_jump_gravity, da->lw_jump_terminal_vel);
}

/* code+0x2630: land into a roll. */
void ftTs_SpecialLwDive_Coll(Fighter_GObj* gobj)
{
    Fighter* fp = GET_FIGHTER(gobj);
    ftCollisionBox ecb = ftCollisionBox_FromDisc(&ftTs_Attrs(fp)->lw_air_ecb);

    if (ft_800824A0(gobj, &ecb)) {
        ftCommon_8007D6A4(fp);
        ftTs_SpecialLwRun_Enter(gobj);
    }
}

/* ------------------------------------------------------------------------------------------------
 * RunBrake (357), StopWall (358, 359)
 * --------------------------------------------------------------------------------------------- */

/* code+0x268C */
void ftTs_SpecialLwRunBrake_Anim(Fighter_GObj* gobj)
{
    if (!ftAnim_IsFramesRemaining(gobj)) {
        ft_8008A2BC(gobj);
    }
}

void ftTs_SpecialLwRunBrake_IASA(Fighter_GObj* gobj) {}

/* code+0x26D0 */
void ftTs_SpecialLwRunBrake_Phys(Fighter_GObj* gobj)
{
    Fighter* fp = GET_FIGHTER(gobj);

    ftCommon_CalcGroundAccel_Deaccel(fp, ftTs_Attrs(fp)->lw_brake_friction);
    ftCommon_SetSelfMovementFromGroundedMovement(gobj);
}

/* code+0x2710 */
void ftTs_SpecialLwRunBrake_Coll(Fighter_GObj* gobj)
{
    ft_800827A0(gobj);
}

/* code+0x2730 */
void ftTs_SpecialLwStopWall_Anim(Fighter_GObj* gobj)
{
    if (!ftAnim_IsFramesRemaining(gobj)) {
        ft_8008A2BC(gobj);
    }
}

void ftTs_SpecialLwStopWall_IASA(Fighter_GObj* gobj) {}
void ftTs_SpecialLwStopWall_Phys(Fighter_GObj* gobj) {}

/* code+0x2778 */
void ftTs_SpecialLwStopWall_Coll(Fighter_GObj* gobj)
{
    ft_800843FC(gobj);
}
