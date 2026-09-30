/* King Dedede (Akaneia), native: Down B, Jet Hammer.
 *
 * Start, then Hold: Dedede charges while B is held and can walk, turn, jump and fall while
 * charging. Letting go swings (SpecialLw, or SpecialLwMax once fully charged). The hammer's hitbox
 * damage grows with the charge. Past full charge Dedede takes speciallw_self_damage every
 * speciallw_self_damage_interval frames. OnActionStateChange (dedede.c) keeps the charge color
 * overlay on through the Hold states.
 *
 * Source: PlDe.dat ftFunction: SpecialLw, SpecialAirLw, SpecialLw*_*, SpecialAirLw*_*,
 * SpecialLw_SetupStateVars, SpecialLw_InitCallbacks, SpecialLw_SpawnEffect,
 * SpecialLwUpdateHitboxData, SpecialLw_IncrementCharge and the Enter / ledge / landing helpers. */
#include "ftDe.h"

#include <melee/ef/efasync.h>
#include <melee/ef/eflib.h>
#include <melee/ft/fighter.h>
#include <melee/ft/ft_081B.h>
#include <melee/ft/ft_0881.h>
#include <melee/ft/ft_084E.h>
#include <melee/ft/ft_0892.h>
#include <melee/ft/ftanim.h>
#include <melee/ft/ftcoll.h>
#include <melee/ft/ftcommon.h>
#include <melee/ft/ftwalkcommon.h>
#include <melee/ft/inlines.h>
#include <melee/ft/kinds/ftCommon/ftCo_Fall.h>
#include <melee/ft/kinds/ftCommon/ftCo_Jump.h>
#include <melee/ft/kinds/ftCommon/forward.h>
#include <melee/lb/lb_013B.h>

/* Hold, Walk, Turn, JumpSquat, Jump, Fall and Landing keep the charge effect and sound. */
#define FTDE_MF_HOLD (Ft_MF_KeepGfx | Ft_MF_KeepSfx)
#define FTDE_MF_HOLD_WALK (Ft_MF_KeepGfx | Ft_MF_SkipModel | Ft_MF_SkipMatAnim | Ft_MF_KeepSfx)

/* SpecialLw_SetupStateVars: a fresh charge. */
static void SpecialLw_ResetCharge(HSD_GObj* gobj)
{
    ftDe_MotionVars* mv = ftDe_MV(GET_FIGHTER(gobj));
    mv->speciallw.charge = 0;
    mv->speciallw.damage_timer = 0;
    mv->speciallw.charged = 0;
    mv->speciallw.base_damage[0] = -1;
    mv->speciallw.base_damage[1] = -1;
    mv->speciallw.base_damage[2] = -1;
    mv->speciallw.base_damage[3] = -1;
}

/* SpecialLw_InitCallbacks: effects die with the move and pause during hitlag. */
static void SpecialLw_SetCallbacks(HSD_GObj* gobj)
{
    Fighter* fp = GET_FIGHTER(gobj);
    fp->take_dmg_cb = efLib_DestroyAll;
    fp->death2_cb = efLib_DestroyAll;
    fp->take_dmg_2_cb = efLib_DestroyAll;
    Fighter_SetEffectHitlagCallbacks(fp);
}

static void SpecialLw_Change(HSD_GObj* gobj, FtMotionId msid, MotionFlags flags)
{
    Fighter_ChangeMotionState(gobj, msid, flags, 0.0f, 1.0f, 0.0f, NULL);
    SpecialLw_SetCallbacks(gobj);
}

static bool SpecialLw_IsCharged(Fighter* fp)
{
    return ftDe_MV(fp)->speciallw.charge >= ftDe_Attrs(fp)->speciallw_charge_frames;
}

/* SpecialLw (ground) */
void ftDe_SpecialLw_Enter(HSD_GObj* gobj)
{
    Fighter_ChangeMotionState(gobj, ftDe_MS_SpecialLwStart, 0, 0.0f, 1.0f, 0.0f, NULL);
    ftAnim_8006EBA4(gobj);
    SpecialLw_ResetCharge(gobj);
    SpecialLw_SetCallbacks(gobj);
}

/* SpecialAirLw */
void ftDe_SpecialAirLw_Enter(HSD_GObj* gobj)
{
    Fighter_ChangeMotionState(gobj, ftDe_MS_SpecialAirLwStart, 0, 0.0f, 1.0f, 0.0f, NULL);
    ftAnim_8006EBA4(gobj);
    SpecialLw_ResetCharge(gobj);
    SpecialLw_SetCallbacks(gobj);
}

/* SpecialLw_SpawnEffect: the jet sound, and the jet flame once per use. */
static void SpecialLw_SpawnEffect(HSD_GObj* gobj)
{
    Fighter* fp = GET_FIGHTER(gobj);
    ft_80088510(fp, 0x13A1, 0x7F, 0x40);
    if (!fp->x2219_b0) {
        efAsync_Spawn(gobj, &fp->x60C, 0, 0x1777, fp->parts[47].joint);
        fp->x2219_b0 = true;
    }
}

/* SpecialLwHold_Enter: the charging stance, Max once fully charged. */
static void SpecialLwHold_Enter(HSD_GObj* gobj)
{
    Fighter* fp = GET_FIGHTER(gobj);
    ftCommon_8007D7FC(fp);
    SpecialLw_Change(gobj, SpecialLw_IsCharged(fp) ? ftDe_MS_SpecialLwHoldMax : ftDe_MS_SpecialLwHold,
                     FTDE_MF_HOLD);
}

/* SpecialLw_Enter: B released on the ground, swing. */
static void SpecialLwRelease_Enter(HSD_GObj* gobj)
{
    Fighter* fp = GET_FIGHTER(gobj);
    SpecialLw_Change(gobj, SpecialLw_IsCharged(fp) ? ftDe_MS_SpecialLwMax : ftDe_MS_SpecialLw, 0);
}

/* SpecialAirLw_Enter: B released in the air, swing. */
static void SpecialAirLwRelease_Enter(HSD_GObj* gobj)
{
    Fighter* fp = GET_FIGHTER(gobj);
    SpecialLw_Change(gobj, SpecialLw_IsCharged(fp) ? ftDe_MS_SpecialAirLwMax : ftDe_MS_SpecialAirLw, 0);
}

/* SpecialLwWalk_Enter / SpecialLwTurn_Enter */
static void SpecialLwWalk_Enter(HSD_GObj* gobj)
{
    SpecialLw_Change(gobj, ftDe_MS_SpecialLwWalk, FTDE_MF_HOLD_WALK);
}

static void SpecialLwTurn_Enter(HSD_GObj* gobj)
{
    SpecialLw_Change(gobj, ftDe_MS_SpecialLwTurn, FTDE_MF_HOLD_WALK);
}

/* SpecialLwJumpSquat_Enter */
static void SpecialLwJumpSquat_Enter(HSD_GObj* gobj)
{
    Fighter* fp = GET_FIGHTER(gobj);
    ftDe_MotionVars* mv = ftDe_MV(fp);
    Fighter_ChangeMotionState(gobj, ftDe_MS_SpecialLwJumpSquat, FTDE_MF_HOLD, 0.0f, 1.0f, 0.0f, NULL);
    mv->speciallw.x0 = 0;
    mv->speciallw.jump_input = ftCo_Jump_GetInput(gobj);
    mv->speciallw.x8 = 0;
    SpecialLw_SetCallbacks(gobj);
}

/* SpecialLwJump_Enter */
static void SpecialLwJump_Enter(HSD_GObj* gobj)
{
    Fighter_ChangeMotionState(gobj, ftDe_MS_SpecialLwJump, FTDE_MF_HOLD, 0.0f, 1.0f, 0.0f, NULL);
    ftCo_800CB110(gobj, true, 1.0f);
    SpecialLw_SetCallbacks(gobj);
}

/* SpecialLwFall_Enter: also the fall-off callback of every grounded charging state. */
static void SpecialLwFall_Enter(HSD_GObj* gobj)
{
    Fighter* fp = GET_FIGHTER(gobj);
    ftCommon_8007D5D4(fp);
    SpecialLw_Change(gobj, ftDe_MS_SpecialLwFall, FTDE_MF_HOLD);
}

/* SpecialLanding_Enter: landing while charging in the air. */
static void SpecialLwLanding_Enter(HSD_GObj* gobj)
{
    Fighter_ChangeMotionState(gobj, ftDe_MS_SpecialLwLanding, FTDE_MF_HOLD, 0.0f, 1.0f, 0.0f, NULL);
    ftCommon_8007E2FC(gobj);
    SpecialLw_SetCallbacks(gobj);
}

/* SpecialLw_IncrementCharge: one more frame of charge; at full charge, the effect and overlay
 * once, then periodic self damage. */
static void SpecialLw_Charge(HSD_GObj* gobj)
{
    Fighter* fp = GET_FIGHTER(gobj);
    ftDe_DatAttrs* da = ftDe_Attrs(fp);
    ftDe_MotionVars* mv = ftDe_MV(fp);

    if (mv->speciallw.charge < da->speciallw_charge_frames) {
        mv->speciallw.charge++;
        return;
    }

    if (mv->speciallw.damage_timer > 0) {
        mv->speciallw.damage_timer--;
    } else {
        mv->speciallw.damage_timer = da->speciallw_self_damage_interval;
        Fighter_TakeDamage_8006CC7C(fp, da->speciallw_self_damage);
    }

    if (mv->speciallw.charged == 0) {
        DISC_PTR(void)* list = (void*) DP(fp->ft_data->x48_items);
        ftDe_ColAnims* colanims = (ftDe_ColAnims*) DP(list[4]);
        ftDe_OverlaySlot* overlays = DP(colanims->overlays);

        mv->speciallw.charged = 1;
        ft_80088510(fp, 0x13A1, 0x7F, 0x40);
        efLib_DestroyAll(gobj);
        efAsync_Spawn(gobj, &fp->x60C, 0, 0x1778, fp->parts[47].joint);
        lb_800144C8(&fp->x488, DP(overlays[1]), 0, 0);
    }
}

/* SpecialLwUpdateHitboxData: before full charge, each live hammer hitbox deals its script damage
 * plus speciallw_damage_per_frame per frame charged. */
static void SpecialLw_UpdateHitboxes(HSD_GObj* gobj)
{
    Fighter* fp = GET_FIGHTER(gobj);
    ftDe_DatAttrs* da = ftDe_Attrs(fp);
    ftDe_MotionVars* mv = ftDe_MV(fp);
    int i;

    if (mv->speciallw.charge >= da->speciallw_charge_frames) {
        return;
    }
    for (i = 0; i < 4; i++) {
        HitCapsule* hit = &fp->x914[i];
        if (hit->state == HitCapsule_Disabled) {
            continue;
        }
        if (mv->speciallw.base_damage[i] == -1) {
            mv->speciallw.base_damage[i] = (s32) hit->damage;
        }
        ftColl_8007ABD0(hit,
                        (s32) ((float) mv->speciallw.charge * da->speciallw_damage_per_frame +
                               (float) mv->speciallw.base_damage[i]),
                        gobj);
    }
}

/* ---------------------------------------------------------------- Start */

void ftDe_SpecialLwStart_Anim(HSD_GObj* gobj)
{
    if (!ftAnim_IsFramesRemaining(gobj)) {
        SpecialLw_SpawnEffect(gobj);
        SpecialLwHold_Enter(gobj);
    }
}

void ftDe_SpecialAirLwStart_Anim(HSD_GObj* gobj)
{
    if (!ftAnim_IsFramesRemaining(gobj)) {
        SpecialLw_SpawnEffect(gobj);
        SpecialLwFall_Enter(gobj);
    }
}

void ftDe_SpecialLwStart_IASA(HSD_GObj* gobj) {}
void ftDe_SpecialAirLwStart_IASA(HSD_GObj* gobj) {}

void ftDe_SpecialLwStart_Phys(HSD_GObj* gobj)
{
    ft_80084F3C(gobj);
}

void ftDe_SpecialAirLwStart_Phys(HSD_GObj* gobj)
{
    ft_80084EEC(gobj);
}

/* SpecialLwStart_PassLedge */
static void SpecialLwStart_Fall(HSD_GObj* gobj)
{
    Fighter* fp = GET_FIGHTER(gobj);
    ftCommon_8007D5D4(fp);
    Fighter_ChangeMotionState(gobj, ftDe_MS_SpecialAirLwStart, Ft_MF_UpdateCmd, fp->cur_anim_frame, 1.0f, 0.0f,
                              NULL);
    SpecialLw_SetCallbacks(gobj);
}

/* SpecialAirLwStart_TouchGround */
static void SpecialAirLwStart_Land(HSD_GObj* gobj)
{
    Fighter* fp = GET_FIGHTER(gobj);
    ftCommon_8007D7FC(fp);
    Fighter_ChangeMotionState(gobj, ftDe_MS_SpecialLwStart, Ft_MF_UpdateCmd, fp->cur_anim_frame, 1.0f, 0.0f,
                              NULL);
    SpecialLw_SetCallbacks(gobj);
}

void ftDe_SpecialLwStart_Coll(HSD_GObj* gobj)
{
    ft_8008403C(gobj, SpecialLwStart_Fall);
}

void ftDe_SpecialAirLwStart_Coll(HSD_GObj* gobj)
{
    ft_80082C74(gobj, SpecialAirLwStart_Land);
}

/* ---------------------------------------------------------------- the swing */

void ftDe_SpecialLw_Anim(HSD_GObj* gobj)
{
    SpecialLw_UpdateHitboxes(gobj);
    if (!ftAnim_IsFramesRemaining(gobj)) {
        ft_8008A2BC(gobj);
    }
}

void ftDe_SpecialAirLw_Anim(HSD_GObj* gobj)
{
    SpecialLw_UpdateHitboxes(gobj);
    if (!ftAnim_IsFramesRemaining(gobj)) {
        ftCo_Fall_Enter(gobj);
    }
}

void ftDe_SpecialLw_IASA(HSD_GObj* gobj) {}
void ftDe_SpecialAirLw_IASA(HSD_GObj* gobj) {}

void ftDe_SpecialLw_Phys(HSD_GObj* gobj)
{
    ft_80084F3C(gobj);
}

void ftDe_SpecialAirLw_Phys(HSD_GObj* gobj)
{
    ft_80084EEC(gobj);
}

/* SpecialLw_PassLedge: swinging off an edge keeps the swing (Max stays Max). */
static void SpecialLw_Fall(HSD_GObj* gobj)
{
    Fighter* fp = GET_FIGHTER(gobj);
    FtMotionId msid = fp->motion_id == ftDe_MS_SpecialLw ? ftDe_MS_SpecialAirLw : ftDe_MS_SpecialAirLwMax;
    ftCommon_8007D5D4(fp);
    Fighter_ChangeMotionState(gobj, msid, ftCommon_GroundAirColl_MF, fp->cur_anim_frame, 1.0f, 0.0f, NULL);
    SpecialLw_SetCallbacks(gobj);
}

/* SpecialAirLw_TouchGround */
static void SpecialAirLw_Land(HSD_GObj* gobj)
{
    Fighter* fp = GET_FIGHTER(gobj);
    FtMotionId msid = fp->motion_id == ftDe_MS_SpecialAirLw ? ftDe_MS_SpecialLw : ftDe_MS_SpecialLwMax;
    ftCommon_8007D7FC(fp);
    Fighter_ChangeMotionState(gobj, msid, ftCommon_GroundAirColl_MF | Ft_MF_SkipHit, fp->cur_anim_frame, 1.0f, 0.0f,
                              NULL);
    SpecialLw_SetCallbacks(gobj);
}

void ftDe_SpecialLw_Coll(HSD_GObj* gobj)
{
    /* m-ex uses the air collision here (ft_80082C74), which calls the "fall" callback only when
     * the swing leaves the ground. */
    ft_80082C74(gobj, SpecialLw_Fall);
}

void ftDe_SpecialAirLw_Coll(HSD_GObj* gobj)
{
    ft_80082C74(gobj, SpecialAirLw_Land);
}

/* ---------------------------------------------------------------- Hold */

void ftDe_SpecialLwHold_Anim(HSD_GObj* gobj) {}

void ftDe_SpecialLwHold_IASA(HSD_GObj* gobj)
{
    Fighter* fp = GET_FIGHTER(gobj);
    ftDe_DatAttrs* da = ftDe_Attrs(fp);
    float stick_x;

    if ((fp->input.held_buttons[0] & 0x200) == 0) {
        SpecialLwRelease_Enter(gobj);
        return;
    }

    stick_x = fp->input.lstick[0].x;
    if ((stick_x < 0.0f && -stick_x >= da->speciallw_turn_stick && fp->facing_dir == 1.0f) ||
        (stick_x >= 0.0f && stick_x >= da->speciallw_turn_stick && stick_x > 0.0f && fp->facing_dir == -1.0f))
    {
        SpecialLwTurn_Enter(gobj);
        return;
    }

    if (ftCo_Jump_GetInput(gobj) != 0) {
        SpecialLwJumpSquat_Enter(gobj);
    } else if (ftWalkCommon_800DFC70(gobj)) {
        SpecialLwWalk_Enter(gobj);
    }
    SpecialLw_Charge(gobj);

    /* Reaching full charge while standing switches to the charged stance. */
    if (fp->motion_id == ftDe_MS_SpecialLwHold && SpecialLw_IsCharged(fp)) {
        SpecialLwHold_Enter(gobj);
    }
}

void ftDe_SpecialLwHold_Phys(HSD_GObj* gobj)
{
    ft_80084F3C(gobj);
}

void ftDe_SpecialLwHold_Coll(HSD_GObj* gobj)
{
    ft_8008403C(gobj, SpecialLwFall_Enter);
}

/* ---------------------------------------------------------------- Turn */

void ftDe_SpecialLwTurn_Anim(HSD_GObj* gobj)
{
    Fighter* fp = GET_FIGHTER(gobj);
    if (!ftAnim_IsFramesRemaining(gobj)) {
        fp->facing_dir = -fp->facing_dir;
        SpecialLwHold_Enter(gobj);
    }
}

void ftDe_SpecialLwTurn_IASA(HSD_GObj* gobj)
{
    SpecialLw_Charge(gobj);
}

void ftDe_SpecialLwTurn_Phys(HSD_GObj* gobj)
{
    ft_80084F3C(gobj);
}

void ftDe_SpecialLwTurn_Coll(HSD_GObj* gobj)
{
    ft_8008403C(gobj, SpecialLwFall_Enter);
}

/* ---------------------------------------------------------------- Walk */

void ftDe_SpecialLwWalk_Anim(HSD_GObj* gobj) {}

void ftDe_SpecialLwWalk_IASA(HSD_GObj* gobj)
{
    Fighter* fp = GET_FIGHTER(gobj);
    if (ftCo_Jump_GetInput(gobj) != 0) {
        SpecialLwJumpSquat_Enter(gobj);
    } else if (ft_8008A1FC(gobj)) {
        SpecialLwHold_Enter(gobj);
    } else if ((fp->input.held_buttons[0] & 0x200) == 0) {
        SpecialLwRelease_Enter(gobj);
    }
    SpecialLw_Charge(gobj);
}

void ftDe_SpecialLwWalk_Phys(HSD_GObj* gobj)
{
    Fighter* fp = GET_FIGHTER(gobj);
    ftDe_DatAttrs* da = ftDe_Attrs(fp);
    float stick_x = fp->input.lstick[0].x;
    ftCommon_CalcGroundAccel_DashRun(fp, stick_x * da->speciallw_walk_accel, stick_x * da->speciallw_walk_max,
                                     fp->co_attrs.ground_friction);
    ftCommon_SetSelfMovementFromGroundedMovement(gobj);
}

void ftDe_SpecialLwWalk_Coll(HSD_GObj* gobj)
{
    ft_8008403C(gobj, SpecialLwFall_Enter);
}

/* ---------------------------------------------------------------- JumpSquat / Jump / Fall */

void ftDe_SpecialLwJumpSquat_Anim(HSD_GObj* gobj)
{
    if (!ftAnim_IsFramesRemaining(gobj)) {
        SpecialLwJump_Enter(gobj);
    }
}

void ftDe_SpecialLwJumpSquat_IASA(HSD_GObj* gobj)
{
    SpecialLw_Charge(gobj);
}

void ftDe_SpecialLwJumpSquat_Phys(HSD_GObj* gobj)
{
    ft_80084F3C(gobj);
}

void ftDe_SpecialLwJumpSquat_Coll(HSD_GObj* gobj)
{
    ft_8008403C(gobj, SpecialLwFall_Enter);
}

void ftDe_SpecialLwFall_Anim(HSD_GObj* gobj) {}

void ftDe_SpecialLwFall_IASA(HSD_GObj* gobj)
{
    Fighter* fp = GET_FIGHTER(gobj);
    if ((fp->input.held_buttons[0] & 0x200) != 0) {
        SpecialLw_Charge(gobj);
    } else {
        SpecialAirLwRelease_Enter(gobj);
    }
}

void ftDe_SpecialLwFall_Phys(HSD_GObj* gobj)
{
    Fighter* fp = GET_FIGHTER(gobj);
    ftDe_DatAttrs* da = ftDe_Attrs(fp);
    float stick_x = fp->input.lstick[0].x;
    float sign = stick_x >= 0.0f ? 1.0f : -1.0f;

    ftCommon_Fall(fp, fp->co_attrs.gravity, fp->co_attrs.terminal_velocity);
    ftCommon_CalcSelfAccel_AccelToVelClampedFrom(fp, fp->self_vel.x,
                                                 sign * da->speciallw_air_accel_base +
                                                     stick_x * da->speciallw_air_accel_stick,
                                                 stick_x * da->speciallw_air_max_stick, da->speciallw_air_friction);
}

void ftDe_SpecialLwFall_Coll(HSD_GObj* gobj)
{
    ft_80082C74(gobj, SpecialLwLanding_Enter);
}

void ftDe_SpecialLwJump_Anim(HSD_GObj* gobj)
{
    if (!ftAnim_IsFramesRemaining(gobj)) {
        SpecialLwFall_Enter(gobj);
    }
}

void ftDe_SpecialLwJump_IASA(HSD_GObj* gobj)
{
    ftDe_SpecialLwFall_IASA(gobj);
}

void ftDe_SpecialLwJump_Phys(HSD_GObj* gobj)
{
    ftDe_SpecialLwFall_Phys(gobj);
}

void ftDe_SpecialLwJump_Coll(HSD_GObj* gobj)
{
    ftDe_SpecialLwFall_Coll(gobj);
}

/* ---------------------------------------------------------------- Landing */

void ftDe_SpecialLwLanding_Anim(HSD_GObj* gobj)
{
    if (!ftAnim_IsFramesRemaining(gobj)) {
        SpecialLwHold_Enter(gobj);
    }
}

void ftDe_SpecialLwLanding_IASA(HSD_GObj* gobj)
{
    SpecialLw_Charge(gobj);
}

void ftDe_SpecialLwLanding_Phys(HSD_GObj* gobj)
{
    ft_80084F3C(gobj);
}

void ftDe_SpecialLwLanding_Coll(HSD_GObj* gobj)
{
    ft_8008403C(gobj, SpecialLwFall_Enter);
}
