/* ACE 2.0.0's Wario: the ability Kirby copies from him (m-ex "kbFunction" of PlKbCpWr.dat).
 *
 * Kirby's Wario ability is Wario's neutral special, the punch that can be turned around early
 * (ace/wario/wario_specialn.c), compiled again with these things changed:
 *   - every state change goes through KirbyStateChange (two states, motion ids 400 and 401; the
 *     animations and scripts come from the hat file);
 *   - the turn re-enters a fixed state (0 on the ground, 1 in the air) where Wario's re-enters
 *     fp->motion_id;
 *   - the four values the move reads are not Wario's attributes: the code reads them from
 *     Kirby's fighter variables at console fp+2270 to fp+227C (ftKbWr_FV), and nothing in the
 *     hat file writes them (see "mod bug" below);
 *   - the ground physics ends in ftCommon_CalcGroundAccel_Deaccel with the value at fp+227C,
 *     where Wario's ends in ft_80084F3C.
 * The hat file has no itFunction and exports no OnKirbyHurt and no InitCopyItems.
 *
 * Mod bug, kept: fp+2270 to fp+227C are the first four words of Kirby's texture list for the
 * retail "parts" hats (fp->u.kb.x44). For a Kirby wearing this hat they hold whatever was there:
 * zero for a Kirby whose block was cleared and who never wore a parts hat, a parts hat's texture
 * count and pointers after one, or what an earlier added ability stored there (Sonic's trail,
 * Lucas's held freeze, Charizard's reserves). With zeros the turn never happens (the frame test
 * is "frame < 0.0" and the physics turn is off) and the ground friction passed is 0.0.
 * The view below has the console layout (one int, three floats), as Sonic's ftKbSn_FighterVars
 * does over the same bytes, so a value an earlier ability stored as a float is read back as on
 * the console. A parts hat's native pointers do not have their console bit patterns: that case
 * reads different garbage than the console would.
 *
 * The hat file has no debug symbols: each function names the offset of its routine in the hat
 * file's code block (run-source/rel09-ace-native/wario/kirby/PlKbCpWr.listing.txt shows it at
 * 0x81800000 plus the offset) and its size in words, and KIRBY.md there maps them. The code has
 * no fixed console address: m-ex loads it with the hat file.
 *
 * Nothing below keeps state outside Kirby's Fighter. */
#include "../../ace/wario/wario.h"

#include <melee/ft/fighter.h>
#include <melee/ft/ft_081B.h>
#include <melee/ft/ft_0892.h>
#include <melee/ft/ftanim.h>
#include <melee/ft/ftcamera.h>
#include <melee/ft/ftcommon.h>
#include <melee/ft/inlines.h>
#include <melee/ft/kinds/ftCommon/ftCo_Fall.h>
#include <melee/ft/kinds/ftKirby/ftkirby.h>
#include <melee/ft/types.h>

#include "wario_kirby.h"

/* Kirby's states of this ability (KirbyStateChange state numbers; motion ids 400 and 401). They
 * mirror Wario's own 341 and 342. */
enum {
    ftKbWr_State_SpecialN,    /* 0: the punch on the ground */
    ftKbWr_State_SpecialAirN, /* 1: the punch in the air */
    ftKbWr_State_Count
};

/* What the code reads at console fp+2270 (see the file comment): the first words of Kirby's
 * texture list for the parts hats, fp->u.kb.x44. Named after the use the code makes of each. */
typedef struct ftKbWr_FighterVars {
    /* 2270 */ s32 phys_turn;      /* not 0: the physics callbacks may turn (Wario's +0C) */
    /* 2274 */ float turn_frames;  /* the turn is open while the frame is below it (Wario's +00,
                                    * an integer there; read as a float here) */
    /* 2278 */ float turn_stick;   /* stick past it, against the facing (Wario's +04, which his
                                    * ground animation callback does not use) */
    /* 227C */ float ground_decel; /* ftCommon_CalcGroundAccel_Deaccel's value (no counterpart) */
} ftKbWr_FighterVars;

static inline ftKbWr_FighterVars* ftKbWr_FV(Fighter* fp)
{
    _Static_assert(sizeof(ftKbWr_FighterVars) <= sizeof(fp->u.kb.x44),
                   "the four words must lie inside the hat texture list they overlay");
    return (ftKbWr_FighterVars*) &fp->u.kb.x44;
}

/* The stick test the four turn sites share (+0x24C on, +0x340 on, +0x4C0 on, +0x5CC on): facing
 * right, the stick is left of -threshold; facing left, right of +threshold. The code tests the
 * facing with "ble" (so a NaN facing takes the first branch) and a facing of exactly 0 never
 * turns. */
static bool ftKbWr_StickAgainstFacing(Fighter* fp, float threshold)
{
    if (!(fp->facing_dir <= 0.0f)) {
        return fp->input.lstick[0].x < -threshold;
    }
    if (fp->facing_dir < 0.0f) {
        return fp->input.lstick[0].x > threshold;
    }
    return false;
}

/* [OnKirbySwallow] +0x000 (48 words): put the hat on. The retail hat attach without the x2225_b2
 * write: the words of Akaneia's Lucas and Wolf swallow, with two independent pairs of the
 * epilogue scheduled in the other order (+0x09C to +0x0AC). */
static void ftKbWr_OnSwallow(HSD_GObj* gobj)
{
    mu_ak_kirby_attach_hat(gobj);
}

/* [OnKirbyLoseAbility] +0x0C0 (23 words): take the hat off. Word for word the retail
 * ftKb_SpecialN_800EFAF0 (the same words as Akaneia's Lucas hat +0x0C0). */
static void ftKbWr_OnLose(HSD_GObj* gobj)
{
    ftKb_SpecialN_800EFAF0(gobj);
}

/* [SpecialN] +0x11C (23 words): cmd_vars[0] = 0, KirbyStateChange(gobj, 0, 0, 0, 0.0, 1.0, 0.0),
 * then ftAnim_8006EBA4. Wario's own +0254 with the state change swapped. */
static void ftKbWr_SpecialN_Enter(HSD_GObj* gobj)
{
    Fighter* fp = GET_FIGHTER(gobj);

    fp->cmd_vars[0] = 0;
    mu_ak_kirby_state_change(gobj, ftKbWr_State_SpecialN, Ft_MF_None, 0.0f, 1.0f, 0.0f);
    ftAnim_8006EBA4(gobj);
}

/* [SpecialAirN] +0x178 (23 words): the same with state 1. */
static void ftKbWr_SpecialAirN_Enter(HSD_GObj* gobj)
{
    Fighter* fp = GET_FIGHTER(gobj);

    fp->cmd_vars[0] = 0;
    mu_ak_kirby_state_change(gobj, ftKbWr_State_SpecialAirN, Ft_MF_None, 0.0f, 1.0f, 0.0f);
    ftAnim_8006EBA4(gobj);
}

/* [state 0 Anim] +0x224 (61 words): the turn, re-entering state 0 at the current frame; at the
 * end of the animation, the wait state. */
static void ftKbWr_SpecialN_Anim(HSD_GObj* gobj)
{
    Fighter* fp = GET_FIGHTER(gobj);
    ftKbWr_FighterVars* fv = ftKbWr_FV(fp);

    if (fp->cur_anim_frame < fv->turn_frames && ftKbWr_StickAgainstFacing(fp, fv->turn_stick)) {
        ftCommon_UpdateFacing(fp);
        mu_ak_kirby_state_change(gobj, ftKbWr_State_SpecialN, Ft_MF_None, fp->cur_anim_frame,
                                 1.0f, 0.0f);
    }
    if (!ftAnim_IsFramesRemaining(gobj)) {
        ft_8008A2BC(gobj);
    }
}

/* [state 0 Phys] +0x318 (60 words): the turn behind fp+2270, which also reverses the x speed;
 * then the ground friction with the value at fp+227C. */
static void ftKbWr_SpecialN_Phys(HSD_GObj* gobj)
{
    Fighter* fp = GET_FIGHTER(gobj);
    ftKbWr_FighterVars* fv = ftKbWr_FV(fp);

    if (fv->phys_turn != 0 && fp->cur_anim_frame < fv->turn_frames &&
        ftKbWr_StickAgainstFacing(fp, fv->turn_stick))
    {
        ftCommon_UpdateFacing(fp);
        fp->self_vel.x = -fp->self_vel.x;
        mu_ak_kirby_state_change(gobj, ftKbWr_State_SpecialN, Ft_MF_None, fp->cur_anim_frame,
                                 1.0f, 0.0f);
    }
    ftCommon_CalcGroundAccel_Deaccel(fp, fv->ground_decel);
}

/* [state 0 Coll] +0x408 (36 words): off the floor, the air state at the same frame. Wario's own
 * +0AB4 with the state change swapped; the flags (0x0C4C5882) are not read by KirbyStateChange. */
static void ftKbWr_SpecialN_Coll(HSD_GObj* gobj)
{
    Fighter* fp = GET_FIGHTER(gobj);

    if (!ft_80082708(gobj)) {
        ftCommon_8007D5D4(fp);
        mu_ak_kirby_state_change(gobj, ftKbWr_State_SpecialAirN, ftWr_MF_SpecialN_Coll,
                                 fp->cur_anim_frame, 1.0f, 0.0f);
        ftCommon_ClampAirDrift(fp);
    }
}

/* [state 1 Anim] +0x498 (61 words): as +0x224 with state 1, ending in the fall. */
static void ftKbWr_SpecialAirN_Anim(HSD_GObj* gobj)
{
    Fighter* fp = GET_FIGHTER(gobj);
    ftKbWr_FighterVars* fv = ftKbWr_FV(fp);

    if (fp->cur_anim_frame < fv->turn_frames && ftKbWr_StickAgainstFacing(fp, fv->turn_stick)) {
        ftCommon_UpdateFacing(fp);
        mu_ak_kirby_state_change(gobj, ftKbWr_State_SpecialAirN, Ft_MF_None, fp->cur_anim_frame,
                                 1.0f, 0.0f);
    }
    if (!ftAnim_IsFramesRemaining(gobj)) {
        ftCo_Fall_Enter(gobj);
    }
}

/* [state 1 Phys] +0x58C (59 words): the common fall, then the turn behind fp+2270. */
static void ftKbWr_SpecialAirN_Phys(HSD_GObj* gobj)
{
    Fighter* fp = GET_FIGHTER(gobj);
    ftKbWr_FighterVars* fv = ftKbWr_FV(fp);

    ftCommon_FallBasic(fp);
    if (fv->phys_turn != 0 && fp->cur_anim_frame < fv->turn_frames &&
        ftKbWr_StickAgainstFacing(fp, fv->turn_stick))
    {
        ftCommon_UpdateFacing(fp);
        fp->self_vel.x = -fp->self_vel.x;
        mu_ak_kirby_state_change(gobj, ftKbWr_State_SpecialAirN, Ft_MF_None, fp->cur_anim_frame,
                                 1.0f, 0.0f);
    }
}

/* [state 1 Coll] +0x678 (44 words): landing with command variable 0 raised goes to the wait
 * state, otherwise the ground state goes on at the same frame. Wario's own +0D78 with the state
 * change swapped. */
static void ftKbWr_SpecialAirN_Coll(HSD_GObj* gobj)
{
    Fighter* fp = GET_FIGHTER(gobj);

    if (ft_80081D0C(gobj)) {
        if (fp->cmd_vars[0] != 0) {
            ft_8008A2BC(gobj);
        } else {
            ftCommon_8007D7FC(fp);
            mu_ak_kirby_state_change(gobj, ftKbWr_State_SpecialN, ftWr_MF_SpecialN_Coll,
                                     fp->cur_anim_frame, 1.0f, 0.0f);
        }
    }
}

/* [move_logic] +0x1D4 (two entries of 0x20 bytes). Words +4 and +8 are Wario's own (his states
 * 341 and 342); KirbyStateChange does not read them. No IASA callback in either state, as on the
 * disc. Constants at +0x214: 0.0, 1.0, 0.5 (not referenced), -1.0 (not referenced). */
static const MotionState ftKbWr_MotionStateTable[ftKbWr_State_Count] = {
    {
        /* state 0, ftcmd 0 "WrSpecialN" */
        0,
        0x00340211,
        0x12 << 24,
        ftKbWr_SpecialN_Anim,
        NULL,
        ftKbWr_SpecialN_Phys,
        ftKbWr_SpecialN_Coll,
        ftCamera_UpdateCameraBox,
    },
    {
        /* state 1, ftcmd 1 "WrSpecialAirN" */
        1,
        0x00340611,
        0x12 << 24,
        ftKbWr_SpecialAirN_Anim,
        NULL,
        ftKbWr_SpecialAirN_Phys,
        ftKbWr_SpecialAirN_Coll,
        ftCamera_UpdateCameraBox,
    },
};

/* The kbFunction exports of PlKbCpWr.dat: 0, 1, 2, 3 and 6. Slots 4 (OnKirbyHurt), 5
 * (InitCopyItems) and 7 are empty m-ex slots. */
const MuAkKirbyCopy ftKbWr_Copy = {
    .on_swallow = ftKbWr_OnSwallow,
    .on_lose = ftKbWr_OnLose,
    .special_n = ftKbWr_SpecialN_Enter,
    .special_air_n = ftKbWr_SpecialAirN_Enter,
    .on_hurt = NULL,
    .init_items = NULL,
    .move_logic = ftKbWr_MotionStateTable,
    .move_logic_count = ftKbWr_State_Count,
    .on_frame = NULL,
};
