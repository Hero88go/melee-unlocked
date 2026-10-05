/* ACE's Wario: the side special (states 343 to 347), his own code.
 *
 *   343  the dash on the ground (and carried off a floor: its callbacks handle both)
 *   344  the bounce after the dash hit something, on the ground
 *   345  the dash started in the air
 *   346  the bounce in the air, steered with the stick
 *   347  the leap: a jump input during the dash, while the script allows it
 *
 * The animation script drives it through the four command variables:
 *   cmd_vars[0]  a hit may bounce him (read by the hit callbacks of 343 and 345)
 *   cmd_vars[1]  a jump input leaves the dash (343), the late gravity and ledge grab (345), the
 *                double jump (347)
 *   cmd_vars[2]  1: dash speed every frame; 2: one frame at 1.2, then the stop (343); in 345 one
 *                speed impulse. Any value starts the trail.
 *   cmd_vars[3]  the trail was started (written by the code)
 *
 * The hit callback is the slot at fp+21F4 (the decomp's hurtbox_detect_cb, called from the
 * fighter's hit pass), set by the collision callbacks every frame. Both entries put
 * efLib_DestroyAll in the slots at fp+21DC and fp+21E4 so a hit or a death removes the trail. */
#include "wario.h"

#include <melee/ef/efasync.h>
#include <melee/ef/eflib.h>
#include <melee/ft/fighter.h>
#include <melee/ft/ft_081B.h>
#include <melee/ft/ft_084E.h>
#include <melee/ft/ft_0892.h>
#include <melee/ft/ftanim.h>
#include <melee/ft/ftcliffcommon.h>
#include <melee/ft/ftcommon.h>
#include <melee/ft/inlines.h>
#include <melee/ft/kinds/ftCommon/ftCo_Fall.h>
#include <melee/ft/kinds/ftCommon/ftCo_FallSpecial.h>
#include <melee/ft/kinds/ftCommon/ftCo_Jump.h>
#include <melee/ft/kinds/ftCommon/ftCo_JumpAerial.h>
#include <melee/ft/kinds/ftCommon/ftCo_Landing.h>
#include <melee/ft/types.h>
#include <melee/lb/lb_013B.h>

/* +1FA0, a double: the one frame speed of cmd_vars[2] == 2. */
#define FTWR_SPECIALS_STOP_VEL 1.2

static void ftWr_SpecialS_OnHit(HSD_GObj* gobj);
static void ftWr_SpecialAirS_OnHit(HSD_GObj* gobj);
static void ftWr_SpecialSJump_OnHit(HSD_GObj* gobj);
static void ftWr_SpecialSJump_Landed(HSD_GObj* gobj);

/* +030C (slot 6, specials) */
void ftWr_SpecialS_Enter(HSD_GObj* gobj)
{
    Fighter* fp = GET_FIGHTER(gobj);

    fp->gr_vel = 0.0f;
    Fighter_ChangeMotionState(gobj, ftWr_MS_SpecialS, Ft_MF_None, 0.0f, 1.0f, 0.0f, NULL);
    ftAnim_8006EBA4(gobj);
    ftWr_MVars(fp)->restart = 1;
    fp->cmd_vars[0] = 0;
    fp->cmd_vars[1] = 0;
    fp->cmd_vars[2] = 0;
    fp->cmd_vars[3] = 0;
    fp->death2_cb = efLib_DestroyAll;
    fp->take_dmg_cb = efLib_DestroyAll;
}

/* +039C (slot 7, specialairs) */
void ftWr_SpecialAirS_Enter(HSD_GObj* gobj)
{
    Fighter* fp = GET_FIGHTER(gobj);

    fp->self_vel.y = 0.0f;
    fp->self_vel.x = 0.0f;
    Fighter_ChangeMotionState(gobj, ftWr_MS_SpecialAirS, Ft_MF_None, 0.0f, 1.0f, 0.0f, NULL);
    ftAnim_8006EBA4(gobj);
    fp->cmd_vars[0] = 0;
    fp->cmd_vars[1] = 0;
    fp->cmd_vars[2] = 0;
    fp->cmd_vars[3] = 0;
    ftWr_MVars(fp)->restart = 0;
    fp->death2_cb = efLib_DestroyAll;
    fp->take_dmg_cb = efLib_DestroyAll;
}

/* The trail (+0EA4, +13F0): effect 5000 of his file on the top joint and color animation 0 of
 * "ftColAnim", once per move. */
static void ftWr_SpecialS_StartTrail(HSD_GObj* gobj, Fighter* fp)
{
    ftWr_FighterVars* fv = ftWr_Vars(fp);

    fp->cmd_vars[3] = 1;
    efAsync_Spawn(gobj, &fp->x60C, 0, FTWR_EFFECT_DASH, fp->parts[0].joint);
    if (fv->colanim != NULL) {
        lb_800144C8(&fp->x488, DP(*fv->colanim), 0, 0);
    }
}

/* The special fall the side special ends in (+139C, +1680, +17C4, +195C). */
static void ftWr_SpecialS_FallSpecial(HSD_GObj* gobj, ftWr_DatAttrs* sa)
{
    ftCo_80096900(gobj, 1, 0, false, sa->specials_fall_mobility,
                  (float) sa->specials_landing_lag);
}

/* +0E28: state 343, animation.
 * In the air: with the restart mark (set by the ground entry) the state starts again at frame 13
 * with the air speed; then a landing starts it again at frame 13 on the ground. */
void ftWr_SpecialS_Anim(HSD_GObj* gobj)
{
    Fighter* fp = GET_FIGHTER(gobj);
    ftWr_DatAttrs* sa = ftWr_Attrs(fp);
    ftWr_MotionVars* mv = ftWr_MVars(fp);
    bool check_landing = false;

    if (fp->ground_or_air == GA_Air) {
        check_landing = true;
        if (mv->restart != 0) {
            /* +0FB0 */
            Fighter_ChangeMotionState(gobj, ftWr_MS_SpecialS, Ft_MF_None,
                                      sa->specials_restart_frame, 1.0f, 0.0f, NULL);
            fp->self_vel.x = fp->facing_dir * sa->specials_air_vel_x;
            mv->restart = 0;
            check_landing = fp->ground_or_air == GA_Air;
        }
    }
    if (check_landing && ft_80081D0C(gobj)) {
        /* +0F54 */
        ftCommon_8007D7FC(fp);
        Fighter_ChangeMotionState(gobj, ftWr_MS_SpecialS, Ft_MF_None,
                                  sa->specials_restart_frame, 1.0f, 0.0f, NULL);
        fp->cmd_vars[1] = 0;
        mv->restart = 0;
    }

    /* +0E58 */
    if (fp->cmd_vars[2] == 0) {
        ftCommon_8007DB24(gobj);
    } else if (fp->cmd_vars[3] == 0) {
        ftWr_SpecialS_StartTrail(gobj, fp);
        if (fp->cmd_vars[2] == 0) {
            ftCommon_8007DB24(gobj);
        }
    }

    if (!ftAnim_IsFramesRemaining(gobj)) {
        /* +0F08 */
        if (ft_80082708(gobj)) {
            ft_8008A2BC(gobj);
        } else {
            ftCo_Fall_Enter(gobj);
        }
    }
}

/* +0FFC: state 343, interrupt. A jump input while the script allows it: the leap. */
void ftWr_SpecialS_IASA(HSD_GObj* gobj)
{
    Fighter* fp = GET_FIGHTER(gobj);
    ftWr_DatAttrs* sa = ftWr_Attrs(fp);

    if (ftCo_Jump_GetInput(gobj) != 0 && fp->cmd_vars[1] != 0) {
        ftCommon_8007D5D4(fp);
        fp->self_vel.y = sa->specials_jump_vel_y;
        efLib_DestroyAll(gobj);
        efAsync_Spawn(gobj, &fp->x60C, 0, FTWR_EFFECT_DASH, fp->parts[0].joint);
        Fighter_ChangeMotionState(gobj, ftWr_MS_SpecialSJump, Ft_MF_SkipColAnim, 0.0f, 1.0f,
                                  0.0f, NULL);
    }
}

/* +10CC: state 343, physics. The file writes the x speed at fp+80 on the ground too, and calls
 * no ground physics routine here. */
void ftWr_SpecialS_Phys(HSD_GObj* gobj)
{
    Fighter* fp = GET_FIGHTER(gobj);
    ftWr_DatAttrs* sa = ftWr_Attrs(fp);

    if (fp->ground_or_air == GA_Air) {
        /* +1130 */
        ftCommon_8007D5D4(fp);
        ft_80084EEC(gobj);
        ftCommon_Fall(fp, fp->co_attrs.gravity, sa->specials_fall_terminal);
        return;
    }
    if (fp->cmd_vars[2] == 1) {
        fp->self_vel.x = fp->facing_dir * sa->specials_vel_x;
    } else if (fp->cmd_vars[2] == 2) {
        /* +1178: a double product, rounded to single */
        fp->self_vel.x = (float) ((double) fp->facing_dir * FTWR_SPECIALS_STOP_VEL);
        fp->cmd_vars[2] = 0;
        ftCommon_CalcGroundAccel_Deaccel(fp, sa->specials_stop_decel);
    }
}

/* +11B0: state 343, collision. */
void ftWr_SpecialS_Coll(HSD_GObj* gobj)
{
    Fighter* fp = GET_FIGHTER(gobj);

    fp->hurtbox_detect_cb = ftWr_SpecialS_OnHit;
    ft_CheckGroundAndLedge(gobj, (int) fp->facing_dir);
    if (ftCo_Jump_GetInput(gobj) != 0 && fp->cmd_vars[1] != 0) {
        /* +1278 */
        fp->cmd_vars[1] = 0;
        ftCommon_8007D5D4(fp);
    }
    /* +120C */
    if (fp->cmd_vars[2] == 0) {
        ft_800827A0(gobj);
        return;
    }
    if (!ft_80082708(gobj)) {
        /* +1258 */
        fp->cmd_vars[1] = 0;
        ftCommon_8007D5D4(fp);
    }
}

/* +1FA8: the hit callback of state 343. With cmd_vars[0] raised: on the ground state 344, in
 * the air the bounce (346) with its speed. The blend argument is 1.0 here (+1F50 in f3). */
static void ftWr_SpecialS_OnHit(HSD_GObj* gobj)
{
    Fighter* fp = GET_FIGHTER(gobj);
    ftWr_DatAttrs* sa;

    if (fp->cmd_vars[0] == 0) {
        return;
    }
    if (fp->ground_or_air == GA_Air) {
        /* +1FE8 */
        sa = ftWr_Attrs(fp);
        fp->self_vel.y = sa->specials_bound_vel_y;
        fp->self_vel.x = fp->facing_dir * sa->specials_bound_vel_x;
        Fighter_ChangeMotionState(gobj, ftWr_MS_SpecialSBound, Ft_MF_None, 0.0f, 1.0f, 1.0f,
                                  NULL);
        return;
    }
    Fighter_ChangeMotionState(gobj, ftWr_MS_SpecialSHit, Ft_MF_None, 0.0f, 1.0f, 1.0f, NULL);
}

/* +128C: state 344, animation. */
void ftWr_SpecialSHit_Anim(HSD_GObj* gobj)
{
    if (!ftAnim_IsFramesRemaining(gobj)) {
        ft_8008A2BC(gobj);
    }
    ftCommon_8007DB24(gobj);
}

/* +12E4: state 344, physics: a branch to the retail routine. */
void ftWr_SpecialSHit_Phys(HSD_GObj* gobj)
{
    ft_80085134(gobj);
}

/* +12E8: state 344, collision. */
void ftWr_SpecialSHit_Coll(HSD_GObj* gobj)
{
    if (!ft_80082708(gobj)) {
        ftCo_Fall_Enter(gobj);
    }
}

/* +1334: state 345, animation. */
void ftWr_SpecialAirS_Anim(HSD_GObj* gobj)
{
    Fighter* fp = GET_FIGHTER(gobj);
    ftWr_DatAttrs* sa = ftWr_Attrs(fp);

    if (fp->cmd_vars[2] != 0 && fp->cmd_vars[3] == 0) {
        /* +13F0 */
        ftWr_SpecialS_StartTrail(gobj, fp);
    }
    if (!ftAnim_IsFramesRemaining(gobj)) {
        ftWr_SpecialS_FallSpecial(gobj, sa);
    }
}

/* +1438: state 345, physics. */
void ftWr_SpecialAirS_Phys(HSD_GObj* gobj)
{
    Fighter* fp = GET_FIGHTER(gobj);
    ftWr_DatAttrs* sa = ftWr_Attrs(fp);

    if (fp->cmd_vars[2] != 0) {
        fp->cmd_vars[2] = 0;
        fp->self_vel.x = fp->facing_dir * sa->specials_air_vel_x;
    }
    if (fp->cmd_vars[1] == 0) {
        ftCommon_Fall(fp, sa->specialairs_gravity, sa->specialairs_terminal);
    } else {
        /* +1498 */
        ft_80084EEC(gobj);
        ftCommon_Fall(fp, sa->specialairs_gravity_late, sa->specialairs_terminal);
    }
}

/* +155C, +15BC: the air dash ends on the floor with half the landing lag (an unsigned shift). */
static void ftWr_SpecialAirS_Land(HSD_GObj* gobj)
{
    ftWr_DatAttrs* sa = ftWr_Attrs(GET_FIGHTER(gobj));

    efLib_DestroyAll(gobj);
    ftCo_LandingFallSpecial_Enter(gobj, false, (float) (sa->specials_landing_lag >> 1));
}

/* +14D0: state 345, collision. The ground and ledge test of the ground states is called first
 * (as the file has it), then the ledge grab while cmd_vars[1] is set, then the landing. */
void ftWr_SpecialAirS_Coll(HSD_GObj* gobj)
{
    Fighter* fp = GET_FIGHTER(gobj);

    fp->hurtbox_detect_cb = ftWr_SpecialAirS_OnHit;
    if (ft_CheckGroundAndLedge(gobj, (int) fp->facing_dir)) {
        /* +15B4 */
        ftCommon_8007D7FC(fp);
        ftWr_SpecialAirS_Land(gobj);
        return;
    }
    if (fp->cmd_vars[1] != 0 && ftCliffCommon_80081298(gobj)) {
        /* +1614 */
        ftCliffCommon_80081370(gobj);
        efLib_DestroyAll(gobj);
        return;
    }
    if (ft_80081D0C(gobj)) {
        ftWr_SpecialAirS_Land(gobj);
    }
}

/* +2028: the hit callback of state 345. The blend argument is 0.0 here. */
static void ftWr_SpecialAirS_OnHit(HSD_GObj* gobj)
{
    Fighter* fp = GET_FIGHTER(gobj);
    ftWr_DatAttrs* sa;

    if (fp->cmd_vars[0] == 0) {
        return;
    }
    sa = ftWr_Attrs(fp);
    fp->self_vel.y = sa->specialairs_bound_vel_y;
    fp->self_vel.x = fp->facing_dir * sa->specialairs_bound_vel_x;
    Fighter_ChangeMotionState(gobj, ftWr_MS_SpecialSBound, Ft_MF_None, 0.0f, 1.0f, 0.0f, NULL);
}

/* +1638: state 346, animation. */
void ftWr_SpecialSBound_Anim(HSD_GObj* gobj)
{
    Fighter* fp = GET_FIGHTER(gobj);
    ftWr_DatAttrs* sa = ftWr_Attrs(fp);

    if (!ftAnim_IsFramesRemaining(gobj)) {
        ftWr_SpecialS_FallSpecial(gobj, sa);
    }
    ftCommon_8007DB24(gobj);
}

/* +16D8: state 346, physics. His own gravity and fall speed, the x speed clamped to +7C, then
 * the stick added (a fused multiply-add, fmadds). */
void ftWr_SpecialSBound_Phys(HSD_GObj* gobj)
{
    Fighter* fp = GET_FIGHTER(gobj);
    ftWr_DatAttrs* sa = ftWr_Attrs(fp);

    ftCommon_Fall(fp, fp->co_attrs.gravity, fp->co_attrs.terminal_velocity);
    ftCommon_ClampSelfVelX(fp, sa->bound_max_vel_x);
    fp->self_vel.x = MU_FMADDS(fp->input.lstick[0].x, sa->bound_drift, fp->self_vel.x);
}

/* +173C: state 346, collision. */
void ftWr_SpecialSBound_Coll(HSD_GObj* gobj)
{
    if (ft_80081D0C(gobj)) {
        ft_8008A2BC(gobj);
    }
}

/* +1788: state 347, animation. At the end of the animation the file takes the plain fall when
 * he is in the air and the special fall when he is on the ground (read twice: +17BC compares
 * fp+E0 with 1 and branches to the ftCo_Fall_Enter call). From frame 35 (+1F64) the trail and
 * the color animation are removed, every frame, also in the state just entered. */
void ftWr_SpecialSJump_Anim(HSD_GObj* gobj)
{
    Fighter* fp = GET_FIGHTER(gobj);
    ftWr_DatAttrs* sa = ftWr_Attrs(fp);

    if (!ftAnim_IsFramesRemaining(gobj)) {
        if (fp->ground_or_air == GA_Air) {
            ftCo_Fall_Enter(gobj);
        } else {
            ftWr_SpecialS_FallSpecial(gobj, sa);
        }
    }
    if (fp->cur_anim_frame >= 35.0f) {
        ftCommon_8007DB24(gobj);
        lb_80014498(&fp->x488);
    }
}

/* +1868: state 347, interrupt: the double jump while the script allows it. */
void ftWr_SpecialSJump_IASA(HSD_GObj* gobj)
{
    Fighter* fp = GET_FIGHTER(gobj);

    if (ftCo_Jump_GetInput(gobj) != 0 && fp->cmd_vars[1] != 0) {
        ftCo_800CB870(gobj);
    }
}

/* +18D0: state 347, physics. */
void ftWr_SpecialSJump_Phys(HSD_GObj* gobj)
{
    Fighter* fp = GET_FIGHTER(gobj);
    ftWr_DatAttrs* sa = ftWr_Attrs(fp);

    fp->self_vel.x = sa->specials_jump_vel_x * fp->facing_dir;
    ft_80084EEC(gobj);
}

/* +18EC: state 347, collision. The air collision with wall jump and ledge grab; the landing
 * callback (+2078) only makes him grounded, and this routine then sees the ground and enters the
 * special fall from there. */
void ftWr_SpecialSJump_Coll(HSD_GObj* gobj)
{
    Fighter* fp = GET_FIGHTER(gobj);
    ftWr_DatAttrs* sa = ftWr_Attrs(fp);

    ft_800831CC(gobj, NULL, ftWr_SpecialSJump_Landed);
    fp->hurtbox_detect_cb = ftWr_SpecialSJump_OnHit;
    if (fp->ground_or_air == GA_Ground) {
        /* +1954 */
        ftCommon_8007D6A4(GET_FIGHTER(gobj));
        ftWr_SpecialS_FallSpecial(gobj, sa);
    }
}

/* +2078 */
static void ftWr_SpecialSJump_Landed(HSD_GObj* gobj)
{
    ftCommon_8007D6A4(GET_FIGHTER(gobj));
}

/* +2080: the hit callback of state 347. No test of cmd_vars[0]; the speed of the ground dash's
 * bounce and blend 1.0. */
static void ftWr_SpecialSJump_OnHit(HSD_GObj* gobj)
{
    Fighter* fp = GET_FIGHTER(gobj);
    ftWr_DatAttrs* sa = ftWr_Attrs(fp);

    fp->self_vel.y = sa->specials_bound_vel_y;
    fp->self_vel.x = fp->facing_dir * sa->specials_bound_vel_x;
    Fighter_ChangeMotionState(gobj, ftWr_MS_SpecialSBound, Ft_MF_None, 0.0f, 1.0f, 1.0f, NULL);
}
