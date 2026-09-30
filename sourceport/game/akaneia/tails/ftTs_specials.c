/* Akaneia's Tails, native: side special (tail spin).
 *
 * Start -> Loop x N -> End. Each loop plays the next of five spin animations (states 362..366
 * on the ground, 369..373 in the air), carrying the frame over modulo s_loop_frame_wrap, until
 * s_loops loops have played or B is released. On the ground the stick steers the spin; in the air
 * it drifts and falls slowly. The air end gives one upward boost per airtime if the whole spin
 * stayed in the air. While a loop state is active the spin trail is drawn (ftTs_visual.c). */
#include "ftTs.h"

#include <math.h>

#include <melee/ft/fighter.h>
#include <melee/ft/ft_081B.h>
#include <melee/ft/ft_084E.h>
#include <melee/ft/ft_0892.h>
#include <melee/ft/ftanim.h>
#include <melee/ft/ftcliffcommon.h>
#include <melee/ft/ftcommon.h>
#include <melee/ft/inlines.h>
#include <melee/ft/kinds/ftCommon/ftCo_FallSpecial.h>
#include <melee/ft/kinds/ftCommon/ftCo_Landing.h>
#include <melee/ft/types.h>
#include <melee/mp/forward.h>

/* code+0x2FF4 SpecialS_EnterAirOrGround (both the ground and the air export) */
void ftTs_SpecialS_EnterAirOrGround(Fighter_GObj* gobj)
{
    Fighter* fp = GET_FIGHTER(gobj);
    ftTails_SpecialSVars* mv = ftTs_SVars(fp);

    if (fp->ground_or_air == GA_Air) {
        Fighter_ChangeMotionState(gobj, ftTs_MS_SpecialAirSStart, Ft_MF_None, 0.0f, 1.0f, 0.0f,
                                  NULL);
    } else {
        Fighter_ChangeMotionState(gobj, ftTs_MS_SpecialSStart, Ft_MF_None, 0.0f, 1.0f, 0.0f,
                                  NULL);
    }
    fp->cmd_vars[0] = 0;
    fp->cmd_vars[1] = 0;
    mv->next_loop = 0;
    mv->loops_done = 0;
    mv->all_airborne = true;
    ftTs_Vars(fp)->trail_count = 0;
}

static inline int ftTs_SpecialS_NextLoop(int loop)
{
    loop++;
    return loop > ftTs_SpecialS_LoopCount - 1 ? 0 : loop;
}

/* code+0x4FF4 SpecialS_Loop_Enter: the next ground loop. */
static void ftTs_SpecialS_Loop_Enter(Fighter_GObj* gobj, float frame, float blend)
{
    Fighter* fp = GET_FIGHTER(gobj);
    ftTails_SpecialSVars* mv = ftTs_SVars(fp);

    mv->all_airborne = false;
    Fighter_ChangeMotionState(gobj, ftTs_MS_SpecialSLoop0 + mv->next_loop, Ft_MF_None, frame,
                              ftTs_Attrs(fp)->s_loop_anim_rate, blend, NULL);
    mv->cur_loop = mv->next_loop;
    mv->next_loop = ftTs_SpecialS_NextLoop(mv->next_loop);
}

/* code+0x5204 SpecialAirS_Loop_Enter: the next air loop. */
static void ftTs_SpecialAirS_Loop_Enter(Fighter_GObj* gobj, float frame, float blend)
{
    Fighter* fp = GET_FIGHTER(gobj);
    ftTails_SpecialSVars* mv = ftTs_SVars(fp);

    Fighter_ChangeMotionState(gobj, ftTs_MS_SpecialAirSLoop0 + mv->next_loop, Ft_MF_None, frame,
                              ftTs_Attrs(fp)->s_loop_anim_rate, blend, NULL);
    mv->cur_loop = mv->next_loop;
    mv->next_loop = ftTs_SpecialS_NextLoop(mv->next_loop);
}

/* code+0x5128 SpecialS_End_Enter */
static void ftTs_SpecialS_End_Enter(Fighter_GObj* gobj)
{
    Fighter_ChangeMotionState(gobj, ftTs_MS_SpecialSEnd, Ft_MF_None, 0.0f, 1.0f, 0.0f, NULL);
}

/* code+0x5374 SpecialAirS_End_Enter */
static void ftTs_SpecialAirS_End_Enter(Fighter_GObj* gobj)
{
    Fighter* fp = GET_FIGHTER(gobj);
    ftTails_FighterVars* fv = ftTs_Vars(fp);

    Fighter_ChangeMotionState(gobj, ftTs_MS_SpecialAirSEnd, Ft_MF_None, 0.0f, 1.0f, 0.0f, NULL);
    if (fv->airs_boost_ready && ftTs_SVars(fp)->all_airborne) {
        fp->self_vel.y = ftTs_Attrs(fp)->airs_end_boost_vel_y;
        fv->airs_boost_ready = false;
    } else {
        fp->self_vel.y = 0.0f;
    }
}

/* ------------------------------------------------------------------------------------------------
 * Ground/air swaps keeping the frame
 * --------------------------------------------------------------------------------------------- */

/* code+0x5268 SpecialS_Start_Trans */
static void ftTs_SpecialS_Start_Trans(Fighter_GObj* gobj)
{
    Fighter* fp = GET_FIGHTER(gobj);

    ftCommon_8007D7FC(fp);
    Fighter_ChangeMotionState(gobj, ftTs_MS_SpecialSStart, ftTs_MF_TransSStart,
                              fp->cur_anim_frame, 1.0f, 0.0f, NULL);
}

/* code+0x5060 SpecialAirS_Start_Trans */
static void ftTs_SpecialAirS_Start_Trans(Fighter_GObj* gobj)
{
    Fighter* fp = GET_FIGHTER(gobj);

    ftCommon_8007D5D4(fp);
    Fighter_ChangeMotionState(gobj, ftTs_MS_SpecialAirSStart, ftTs_MF_TransSStart,
                              fp->cur_anim_frame, 1.0f, 0.0f, NULL);
}

/* code+0x5404 SpecialS_Loop_Trans */
static void ftTs_SpecialS_Loop_Trans(Fighter_GObj* gobj)
{
    Fighter* fp = GET_FIGHTER(gobj);
    ftTails_DatAttrs* da = ftTs_Attrs(fp);
    ftTails_SpecialSVars* mv = ftTs_SVars(fp);

    mv->all_airborne = false;
    ftCommon_8007D7FC(fp);
    Fighter_ChangeMotionState(gobj, ftTs_MS_SpecialSLoop0 + mv->cur_loop, ftTs_MF_TransSLoop,
                              fp->cur_anim_frame, da->s_loop_anim_rate, da->s_loop_blend, NULL);
}

/* code+0x5198 SpecialAirS_Loop_Trans */
static void ftTs_SpecialAirS_Loop_Trans(Fighter_GObj* gobj)
{
    Fighter* fp = GET_FIGHTER(gobj);
    ftTails_DatAttrs* da = ftTs_Attrs(fp);

    ftCommon_8007D5D4(fp);
    Fighter_ChangeMotionState(gobj, ftTs_MS_SpecialAirSLoop0 + ftTs_SVars(fp)->cur_loop,
                              ftTs_MF_TransSLoop, fp->cur_anim_frame, da->s_loop_anim_rate,
                              da->s_loop_blend, NULL);
}

/* code+0x52CC SpecialS_End_Trans: landing out of the air end. */
static void ftTs_SpecialS_End_Trans(Fighter_GObj* gobj)
{
    Fighter* fp = GET_FIGHTER(gobj);

    if (fp->cmd_vars[1] != 0) {
        ftCo_LandingFallSpecial_Enter(gobj, false, ftTs_Frames(ftTs_Attrs(fp)->s_landing_lag));
        return;
    }
    ftCommon_8007D7FC(fp);
    Fighter_ChangeMotionState(gobj, ftTs_MS_SpecialSEnd, ftTs_MF_TransSEnd, fp->cur_anim_frame,
                              1.0f, 0.0f, NULL);
}

/* code+0x50C4 SpecialAirS_End_Trans */
static void ftTs_SpecialAirS_End_Trans(Fighter_GObj* gobj)
{
    Fighter* fp = GET_FIGHTER(gobj);

    ftCommon_8007D5D4(fp);
    Fighter_ChangeMotionState(gobj, ftTs_MS_SpecialAirSEnd, ftTs_MF_TransAirSEnd,
                              fp->cur_anim_frame, 1.0f, 0.0f, NULL);
}

/* ------------------------------------------------------------------------------------------------
 * Ground: Start (360), End (361), Loop (362..366)
 * --------------------------------------------------------------------------------------------- */

/* code+0x2798. The console calls Loop_Enter here without a prototype and without its two float
 * arguments (frame, blend), so they were whatever the FPU registers held; 0, 0 is what the
 * source meant (it passed two integer zeros). */
void ftTs_SpecialS_Start_Anim(Fighter_GObj* gobj)
{
    Fighter* fp = GET_FIGHTER(gobj);

    if (ftAnim_IsFramesRemaining(gobj)) {
        return;
    }
    ftTs_SpecialS_Loop_Enter(gobj, 0.0f, 0.0f);
    fp->self_vel.x = ftTs_Attrs(fp)->s_start_vel_x * fp->facing_dir;
}

void ftTs_SpecialS_Start_IASA(Fighter_GObj* gobj) {}

/* code+0x2810 */
void ftTs_SpecialS_Start_Phys(Fighter_GObj* gobj)
{
    ft_80084F3C(gobj);
}

/* code+0x2830 */
void ftTs_SpecialS_Start_Coll(Fighter_GObj* gobj)
{
    if (!ft_80082708(gobj)) {
        ftTs_SpecialAirS_Start_Trans(gobj);
    }
}

/* code+0x2874 */
void ftTs_SpecialS_End_Anim(Fighter_GObj* gobj)
{
    if (!ftAnim_IsFramesRemaining(gobj)) {
        ft_8008A2BC(gobj);
    }
}

void ftTs_SpecialS_End_IASA(Fighter_GObj* gobj) {}

/* code+0x28B8 */
void ftTs_SpecialS_End_Phys(Fighter_GObj* gobj)
{
    Fighter* fp = GET_FIGHTER(gobj);

    ftCommon_CalcGroundAccel_Deaccel(fp, ftTs_Attrs(fp)->s_end_friction);
    ftCommon_SetSelfMovementFromGroundedMovement(gobj);
}

/* code+0x28F8 */
void ftTs_SpecialS_End_Coll(Fighter_GObj* gobj)
{
    if (!ft_800827A0(gobj)) {
        ftTs_SpecialAirS_End_Trans(gobj);
    }
}

/* code+0x293C */
void ftTs_SpecialS_Loop_Anim(Fighter_GObj* gobj)
{
    Fighter* fp = GET_FIGHTER(gobj);
    ftTails_DatAttrs* da = ftTs_Attrs(fp);
    ftTails_SpecialSVars* mv = ftTs_SVars(fp);

    if (ftAnim_IsFramesRemaining(gobj)) {
        return;
    }
    mv->loops_done++;
    if (mv->loops_done >= da->s_loops) {
        ftTs_SpecialS_End_Enter(gobj);
    } else {
        ftTs_SpecialS_Loop_Enter(gobj, fmodf(fp->cur_anim_frame, da->s_loop_frame_wrap), 4.0f);
    }
}

/* code+0x29D0: letting go of B ends the spin. */
void ftTs_SpecialS_Loop_IASA(Fighter_GObj* gobj)
{
    if (!(GET_FIGHTER(gobj)->input.held_buttons[0] & HSD_PAD_B)) {
        ftTs_SpecialS_End_Enter(gobj);
    }
}

/* code+0x2A04: the stick steers the spin; a centred stick brakes it. */
void ftTs_SpecialS_Loop_Phys(Fighter_GObj* gobj)
{
    Fighter* fp = GET_FIGHTER(gobj);
    ftTails_DatAttrs* da = ftTs_Attrs(fp);
    float stick = fp->input.lstick[0].x;
    float dir;
    float stick_speed;
    float base_speed;

    if ((double) (stick < 0.0f ? -stick : stick) < 0.1) {
        ftCommon_CalcGroundAccel_Deaccel(fp, da->s_loop_friction);
    } else {
        /* Separate statements: the console multiplies and adds without fusing. */
        dir = stick >= 0.0f ? 1.0f : -1.0f;
        stick_speed = stick * da->s_loop_stick_speed;
        base_speed = dir * da->s_loop_base_speed;
        ftCommon_CalcGroundAccel_DashRun(fp, base_speed + stick_speed, dir * da->s_loop_accel,
                                         fp->co_attrs.ground_friction);
    }
    ftCommon_SetSelfMovementFromGroundedMovement(gobj);
}

/* code+0x2AE8 */
void ftTs_SpecialS_Loop_Coll(Fighter_GObj* gobj)
{
    ftCollisionBox ecb = ftCollisionBox_FromDisc(&ftTs_Attrs(GET_FIGHTER(gobj))->s_ecb);

    if (!ft_80082888(gobj, &ecb)) {
        ftTs_SpecialAirS_Loop_Trans(gobj);
    }
}

/* ------------------------------------------------------------------------------------------------
 * Air: Start (367), End (368), Loop (369..373)
 * --------------------------------------------------------------------------------------------- */

/* code+0x2B38 (same missing-argument call as the ground start, see above) */
void ftTs_SpecialAirS_Start_Anim(Fighter_GObj* gobj)
{
    Fighter* fp = GET_FIGHTER(gobj);
    ftTails_DatAttrs* da = ftTs_Attrs(fp);

    if (ftAnim_IsFramesRemaining(gobj)) {
        return;
    }
    ftTs_SpecialAirS_Loop_Enter(gobj, 0.0f, 0.0f);
    fp->self_vel.x *= da->airs_start_vel_x_mul;
    fp->self_vel.y = da->airs_start_vel_y;
}

void ftTs_SpecialAirS_Start_IASA(Fighter_GObj* gobj) {}

/* code+0x2BB8 */
void ftTs_SpecialAirS_Start_Phys(Fighter_GObj* gobj)
{
    ft_80084EEC(gobj);
}

/* code+0x2BD8 */
void ftTs_SpecialAirS_Start_Coll(Fighter_GObj* gobj)
{
    if (ft_80081D0C(gobj)) {
        ftTs_SpecialS_Start_Trans(gobj);
    }
}

/* code+0x2C18 */
void ftTs_SpecialAirS_End_Anim(Fighter_GObj* gobj)
{
    Fighter* fp = GET_FIGHTER(gobj);
    ftTails_DatAttrs* da = ftTs_Attrs(fp);

    if (!ftAnim_IsFramesRemaining(gobj)) {
        ftCo_80096900(gobj, 1, 1, false, da->airs_end_fall_mobility,
                      ftTs_Frames(da->s_landing_lag));
    }
}

void ftTs_SpecialAirS_End_IASA(Fighter_GObj* gobj) {}

/* code+0x2CA4 */
void ftTs_SpecialAirS_End_Phys(Fighter_GObj* gobj)
{
    Fighter* fp = GET_FIGHTER(gobj);
    ftTails_DatAttrs* da = ftTs_Attrs(fp);

    ftCommon_Fall(fp, da->airs_end_gravity, fp->co_attrs.terminal_velocity);
    ftCommon_CalcSelfAccel_DriftSimple(fp, 0.0f, da->airs_end_drift_accel, da->airs_end_drift_max);
}

/* code+0x2D00: lands (keeping the ECB bottom locked at 0 for the check) or grabs a ledge once the
 * script allows it (cmd_vars[0]). */
void ftTs_SpecialAirS_End_Coll(Fighter_GObj* gobj)
{
    Fighter* fp = GET_FIGHTER(gobj);
    CollData* coll = &fp->coll_data;
    u32 saved_flags = coll->x130_flags;

    coll->x130_flags |= CollData_X130_Locked;
    coll->desired_ecb.bottom.y = 0.0f;
    if (ft_CheckGroundAndLedge(gobj, ftGetFacingDirInt(fp))) {
        ftTs_SpecialS_End_Trans(gobj);
    } else if (fp->cmd_vars[0] != 0) {
        if (ftCliffCommon_80081298(gobj)) {
            ftCliffCommon_80081370(gobj);
        }
    }
    coll->x130_flags = saved_flags;
}

/* code+0x2DAC */
void ftTs_SpecialAirS_Loop_Anim(Fighter_GObj* gobj)
{
    Fighter* fp = GET_FIGHTER(gobj);
    ftTails_DatAttrs* da = ftTs_Attrs(fp);
    ftTails_SpecialSVars* mv = ftTs_SVars(fp);

    if (ftAnim_IsFramesRemaining(gobj)) {
        return;
    }
    mv->loops_done++;
    if (mv->loops_done >= da->s_loops) {
        ftTs_SpecialAirS_End_Enter(gobj);
    } else {
        ftTs_SpecialAirS_Loop_Enter(gobj, fmodf(fp->cur_anim_frame, da->s_loop_frame_wrap),
                                    4.0f);
    }
}

/* code+0x2E40 */
void ftTs_SpecialAirS_Loop_IASA(Fighter_GObj* gobj)
{
    if (!(GET_FIGHTER(gobj)->input.held_buttons[0] & HSD_PAD_B)) {
        ftTs_SpecialAirS_End_Enter(gobj);
    }
}

/* code+0x2E74 */
void ftTs_SpecialAirS_Loop_Phys(Fighter_GObj* gobj)
{
    Fighter* fp = GET_FIGHTER(gobj);
    ftTails_DatAttrs* da = ftTs_Attrs(fp);

    ftCommon_Fall(fp, da->airs_gravity, da->airs_terminal_vel);
    ftCommon_CalcSelfAccel_DriftSimple(fp, 0.0f, da->airs_drift_accel, da->airs_drift_max);
}

/* code+0x2ED0 */
void ftTs_SpecialAirS_Loop_Coll(Fighter_GObj* gobj)
{
    ftCollisionBox ecb = ftCollisionBox_FromDisc(&ftTs_Attrs(GET_FIGHTER(gobj))->s_ecb);

    if (ft_800824A0(gobj, &ecb)) {
        ftTs_SpecialS_Loop_Trans(gobj);
    }
}
