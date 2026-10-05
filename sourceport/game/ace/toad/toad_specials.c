/* ACE's Toad: the side special (states 345 and 346), the file's own code.
 *
 * It is Mario's DOWN special (the tornado, ft/kinds/ftMario/ftmariospeciallw.c) rewritten for
 * the side special's states. Read against Mario's routines; the differences:
 *   - no B press: while the script holds cmd_vars[2] the lift is given every frame
 *   - the start y speed is 0.6 (a double, +16E8) minus attribute +10, and plain 0.6 once the
 *     lift was used in the air (+1604)
 *   - the x speed decays by 0.83 a frame (a double, +16D8) once cmd_vars[0] is set, in both
 *     states from attribute +00
 *   - the lean is a Z rotation of part 0 by minus atan2(floor normal x, y), with no facing
 *   - the floor test of the ground state is the file's own copy (+1A30), which turns the box
 *     for a left facing by negating both x fields where the retail routine swaps them
 *   - the end of the air state is always the plain fall
 *   - effect 5001 of his own effect file where Mario spawns 1148
 * The two callbacks that undo the lean on a hit or a death are Mario's own static routine
 * (0x800E2050, ftPartSetRotX(fp, 0, 0)), named by address in the file. */
#include "toad.h"

#include <math.h>

#include <melee/ef/eflib.h>
#include <melee/ef/efsync.h>
#include <melee/ft/fighter.h>
#include <melee/ft/ft_081B.h>
#include <melee/ft/ft_0892.h>
#include <melee/ft/ftanim.h>
#include <melee/ft/ftcommon.h>
#include <melee/ft/ftparts.h>
#include <melee/ft/inlines.h>
#include <melee/ft/kinds/ftCommon/ftCo_Fall.h>
#include <melee/ft/types.h>
#include <melee/mp/mpcoll.h>
#include <sysdolphin/baselib/gobj.h>

#define FTTD_SPECIALS_START_VEL_Y 0.6 /* +16E8, a double; +1604 as a single */
#define FTTD_SPECIALS_DECAY 0.83      /* +16D8, a double */

/* 0x0C4C508A: the flags Mario's tornado keeps to itself as transition_flags. */
#define ftTd_MF_SpecialS_Swap                                                \
    (Ft_MF_KeepGfx | Ft_MF_SkipHit | Ft_MF_SkipMatAnim | Ft_MF_UpdateCmd |   \
     Ft_MF_SkipColAnim | Ft_MF_SkipItemVis | Ft_MF_Unk19 |                   \
     Ft_MF_SkipModelPartVis | Ft_MF_SkipModelFlags | Ft_MF_Unk27)

/* +1A18: the box of the floor test, Mario's tornado box. Copied to the stack at each use. */
static const ftCollisionBox ftTd_SpecialS_Box = { 12, 0, { -6, +6 }, { +6, +6 } };

/* Retail 0x800E2050 (static "updateRot" of ftmariospeciallw.c). */
static void ftTd_SpecialS_ResetRot(HSD_GObj* gobj)
{
    Fighter* fp = GET_FIGHTER(gobj);

    ftPartSetRotX(fp, 0, 0.0f);
}

/* The tail both entries share (+03FC, +050C). */
static void ftTd_SpecialS_Start(HSD_GObj* gobj, Fighter* fp)
{
    ftTd_SpecialSVars* sv = ftTd_SVars(fp);

    fp->cmd_vars[0] = 0;
    fp->cmd_vars[1] = 0;
    sv->decay = 0.0f;
    sv->on_floor = 0.0f;
    sv->x2344 = 5;
    fp->take_dmg_cb = ftTd_SpecialS_ResetRot;
    fp->death2_cb = ftTd_SpecialS_ResetRot;
    efSync_Spawn(FTTD_EFFECT_SPECIALS, gobj, gobj->hsd_obj);
    fp->x2219_b0 = true;
    fp->pre_hitlag_cb = efLib_PauseAll;
    fp->post_hitlag_cb = efLib_ResumeAll;
}

/* +0374 (slot 6, specials). The ground entry also starts the air state, as Mario's tornado. */
void ftTd_SpecialS_Enter(HSD_GObj* gobj)
{
    Fighter* fp = GET_FIGHTER(gobj);
    ftTd_DatAttrs* sa = ftTd_Attrs(fp);

    fp->cmd_vars[2] = 0;
    Fighter_ChangeMotionState(gobj, ftMr_MS_SpecialAirS, Ft_MF_None, 0.0f, 1.0f, 0.0f, NULL);
    ftAnim_8006EBA4(gobj);
    fp->self_vel.y = (float) (FTTD_SPECIALS_START_VEL_Y - (double) sa->specials_tap_y_vel);
    ftCommon_ClampSelfVelX(fp, sa->specials_momentum_x);
    ftTd_SpecialS_Start(gobj, fp);
}

/* +0480 (slot 7, specialairs) */
void ftTd_SpecialAirS_Enter(HSD_GObj* gobj)
{
    Fighter* fp = GET_FIGHTER(gobj);
    ftTd_DatAttrs* sa = ftTd_Attrs(fp);

    fp->cmd_vars[2] = 0;
    Fighter_ChangeMotionState(gobj, ftMr_MS_SpecialAirS, Ft_MF_None, 0.0f, 1.0f, 0.0f, NULL);
    ftAnim_8006EBA4(gobj);
    if (fp->u.mr.x2234_tornadoCharge) {
        fp->self_vel.y = (float) FTTD_SPECIALS_START_VEL_Y;
    } else {
        fp->self_vel.y =
            (float) (FTTD_SPECIALS_START_VEL_Y - (double) sa->specials_tap_y_vel);
    }
    ftCommon_ClampSelfVelX(fp, sa->specials_momentum_x);
    ftTd_SpecialS_Start(gobj, fp);
}

/* +0AB4: state 345, animation. */
void ftTd_SpecialS_Anim(HSD_GObj* gobj)
{
    Fighter* fp = GET_FIGHTER(gobj);

    if (!ftAnim_IsFramesRemaining(gobj)) {
        fp->take_dmg_cb = NULL;
        fp->death2_cb = NULL;
        ft_8008A2BC(gobj);
    }
}

/* The x speed limit of both physics callbacks (+0B50, +0ED8): attribute +00, less the decay once
 * cmd_vars[0] is set, never under 0. */
static float ftTd_SpecialS_SpeedLimit(Fighter* fp, ftTd_DatAttrs* sa)
{
    ftTd_SpecialSVars* sv = ftTd_SVars(fp);
    float limit = sa->specials_momentum_x;

    if (fp->cmd_vars[0] != 0) {
        sv->decay = (float) ((double) sv->decay - FTTD_SPECIALS_DECAY);
        limit += sv->decay;
        if (limit < 0.0f) {
            limit = 0.0f;
        }
    }
    return limit;
}

/* The ground state leaves the floor (+0BF4 without its first three lines, and +0D64). */
static void ftTd_SpecialS_ToAir(HSD_GObj* gobj, Fighter* fp, ftTd_DatAttrs* sa)
{
    ftCommon_8007D5D4(fp);
    Fighter_ChangeMotionState(gobj, ftMr_MS_SpecialAirS, ftTd_MF_SpecialS_Swap,
                              fp->cur_anim_frame, 1.0f, 0.0f, NULL);
    ftCommon_ClampFallSpeed(fp, sa->specials_tap_grav);
    ftCommon_ClampSelfVelX(fp, sa->specials_air_momentum_x);
    fp->pre_hitlag_cb = efLib_PauseAll;
    fp->post_hitlag_cb = efLib_ResumeAll;
}

/* +0B1C: state 345, physics. */
void ftTd_SpecialS_Phys(HSD_GObj* gobj)
{
    Fighter* fp = GET_FIGHTER(gobj);
    ftTd_DatAttrs* sa = ftTd_Attrs(fp);
    float limit = ftTd_SpecialS_SpeedLimit(fp, sa);

    ftCommon_CalcGroundAccel_AccelToLStickX(fp, 0.0f, sa->specials_momentum_x_mul, limit);
    ftCommon_SetSelfMovementFromGroundedMovement(gobj);
    if (fp->cmd_vars[2] != 0) {
        /* +0BF4 */
        fp->cmd_vars[2] = 0;
        fp->self_vel.y = fp->self_vel.y + sa->specials_tap_y_vel;
        ftTd_SpecialS_ToAir(gobj, fp, sa);
    }
}

/* +0D24, +0DD4, +0FEC, +10A0: the lean. */
static void ftTd_SpecialS_Lean(Fighter* fp, bool lean)
{
    if (lean) {
        ftPartSetRotZ(fp, 0,
                      -atan2f(fp->coll_data.floor.normal.x, fp->coll_data.floor.normal.y));
    } else {
        ftPartSetRotZ(fp, 0, 0.0f);
    }
}

/* +1A30: the file's own floor test for the ground state. Retail ft_80082888 but for the box of
 * a left facing: both x fields negated in place (the caller's stack copy), not swapped. */
static bool ftTd_SpecialS_GroundTest(HSD_GObj* gobj, ftCollisionBox* box)
{
    Fighter* fp = GET_FIGHTER(gobj);
    CollData* coll = &fp->coll_data;
    bool result;

    coll->last_pos = coll->cur_pos;
    coll->cur_pos = fp->cur_pos;
    if (fp->facing_dir < 0.0f) {
        box->right.x = -box->right.x;
        box->left.x = -box->left.x;
    }
    result = mpColl_8004B21C(coll, box);
    fp->cur_pos = coll->cur_pos;
    return result;
}

/* +0C8C: state 345, collision. */
void ftTd_SpecialS_Coll(HSD_GObj* gobj)
{
    Fighter* fp = GET_FIGHTER(gobj);
    ftTd_DatAttrs* sa = ftTd_Attrs(fp);
    ftTd_SpecialSVars* sv = ftTd_SVars(fp);
    ftCollisionBox box = ftTd_SpecialS_Box;
    bool on_floor;

    if (fp->ground_or_air == GA_Ground) {
        on_floor = ftTd_SpecialS_GroundTest(gobj, &box);
    } else {
        on_floor = ft_800824A0(gobj, &box);
    }
    if (on_floor) {
        /* +0D08 */
        sv->on_floor = 1.0f;
        ftTd_SpecialS_Lean(fp, fp->cmd_vars[3] != 0);
        return;
    }
    /* +0D60 */
    fp->cmd_vars[2] = 0;
    ftTd_SpecialS_ToAir(gobj, fp, sa);
    sv->on_floor = 0.0f;
    ftTd_SpecialS_Lean(fp, false);
}

/* +0E10: state 346, animation. The script's cmd_vars[1] marks the lift as used until he lands
 * (fp+2234, Mario's tornado mark). */
void ftTd_SpecialAirS_Anim(HSD_GObj* gobj)
{
    Fighter* fp = GET_FIGHTER(gobj);

    if (fp->cmd_vars[1] != 0) {
        fp->cmd_vars[1] = 0;
        fp->u.mr.x2234_tornadoCharge = true;
    }
    if (!ftAnim_IsFramesRemaining(gobj)) {
        fp->take_dmg_cb = NULL;
        fp->death2_cb = NULL;
        ftCo_Fall_Enter(gobj);
    }
}

/* +0E98: state 346, physics. */
void ftTd_SpecialAirS_Phys(HSD_GObj* gobj)
{
    Fighter* fp = GET_FIGHTER(gobj);
    ftTd_DatAttrs* sa = ftTd_Attrs(fp);
    float limit;

    if (!fp->u.mr.x2234_tornadoCharge && fp->cmd_vars[2] != 0) {
        /* +0F5C */
        ftCommon_Ascend(fp, sa->specials_tap_y_vel, sa->specials_tap_grav);
    }
    ftCommon_FallBasic(fp);
    limit = ftTd_SpecialS_SpeedLimit(fp, sa);
    ftCommon_CalcSelfAccel_DriftSimple_NoFriction(fp, 0.0f, sa->specials_air_momentum_x_mul,
                                                  limit);
}

/* +0F70: state 346, collision. Landing puts him in the ground state at the same frame. */
void ftTd_SpecialAirS_Coll(HSD_GObj* gobj)
{
    Fighter* fp = GET_FIGHTER(gobj);
    ftTd_DatAttrs* sa = ftTd_Attrs(fp);
    ftTd_SpecialSVars* sv = ftTd_SVars(fp);
    ftCollisionBox box = ftTd_SpecialS_Box;

    if (!ft_800824A0(gobj, &box)) {
        sv->on_floor = 0.0f;
        ftTd_SpecialS_Lean(fp, false);
        return;
    }
    /* +101C */
    fp->cmd_vars[2] = 0;
    ftCommon_8007D7FC(fp);
    fp->self_vel.y = 0.0f;
    fp->u.mr.x2234_tornadoCharge = false;
    Fighter_ChangeMotionState(gobj, ftMr_MS_SpecialS, ftTd_MF_SpecialS_Swap,
                              fp->cur_anim_frame, 1.0f, 0.0f, NULL);
    ftCommon_ClampGroundVel(fp, sa->specials_momentum_x);
    sv->on_floor = 1.0f;
    fp->pre_hitlag_cb = efLib_PauseAll;
    fp->post_hitlag_cb = efLib_ResumeAll;
    ftTd_SpecialS_Lean(fp, fp->cmd_vars[3] != 0);
}
