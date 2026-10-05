/* Akaneia's Lucas: the ability Kirby copies from him (m-ex "kbFunction" of PlKbCpLc.dat).
 *
 * Kirby's Lucas ability is Lucas's PK Freeze (lucas_specialn.c) compiled again with four things
 * changed: the state changes go through KirbyStateChange (the animations and scripts come from the
 * hat file), the parameters are read from the hat data (its word +0x10 points at a copy of the
 * PK Freeze part of Lucas's attributes) where Lucas reads his own attributes, the freeze Kirby
 * holds is kept at fp+0x2270 (ftKbLc_PKFreeze) where Lucas has fp+0x2240, and the projectile is
 * Lucas's article 10 (item kind 276 on Akaneia, data in the hat file) spawned above Kirby's part 4
 * where Lucas uses his part 24. The damage and death callback removes only the freeze (Lucas's own
 * removes his other articles too).
 *
 * The hat file has no debug symbols. Each function names the offset of the routine it was written
 * from in the hat file's code block (run-source/rel09-b1-wolf/kirby/listings/PlKbCpLc.listing.txt
 * shows it at 0x81800000 plus the offset) and its size in words. The code has no fixed console
 * address: m-ex loads it with the hat file.
 *
 * Nothing below keeps state outside Kirby's Fighter; the `logged` counter only limits the log. */
#include "lucas.h"

#include <dolphin/os.h>

#include <melee/ft/fighter.h>
#include <melee/ft/ft_081B.h>
#include <melee/ft/ft_084E.h>
#include <melee/ft/ft_0892.h>
#include <melee/ft/ftanim.h>
#include <melee/ft/ftcamera.h>
#include <melee/ft/ftcommon.h>
#include <melee/ft/ftparts.h>
#include <melee/ft/kinds/ftCommon/ftCo_Fall.h>
#include <melee/ft/kinds/ftCommon/ftCo_FallSpecial.h>
#include <melee/ft/kinds/ftKirby/ftkirby.h>
#include <melee/it/item.h>
#include <melee/lb/lb_00B0.h>
#include <sysdolphin/baselib/controller.h>

/* Kirby's states of this ability (KirbyStateChange state numbers; motion ids 400 to 405). They
 * mirror Lucas's own states 342 to 347. */
enum {
    ftKbLc_State_SpecialNStart,    /* 0 */
    ftKbLc_State_SpecialNHold,     /* 1 */
    ftKbLc_State_SpecialNEnd,      /* 2 */
    ftKbLc_State_SpecialAirNStart, /* 3 */
    ftKbLc_State_SpecialAirNHold,  /* 4 */
    ftKbLc_State_SpecialAirNEnd,   /* 5 */
    ftKbLc_State_Count
};

/* Kirby's joint the freeze appears above: a raw fp->parts index (console fp->parts + 0x40), as
 * Lucas's 24 is. */
#define FTKBLC_FREEZE_PART 4

#define FTKBLC_LOG 4 /* evidence lines that go in the log, per run */

/* The ability's parameters: the hat data's second extra word (+0x10). The block has the layout of
 * the start of Lucas's own attributes (ftLucasAttributes, through x20_PKFREEZE_LAND_BOX) and, on
 * the Akaneia disc, the same values. NULL when Kirby no longer holds an added ability (not an
 * m-ex case: the console reads through whatever its table holds for the kind). */
static ftLucasAttributes* ftKbLc_Attrs(Fighter* fp)
{
    KirbyHatStruct* hat = mu_ak_kirby_hat(fp->u.kb.hat.kind);
    if (hat == NULL) {
        return NULL;
    }
    return DISC_GET(ftLucasAttributes, hat->hat_dynamics[1]);
}

/* [OnKirbySwallow] +0x000 (48 words): put the hat on. The retail hat attach without the x2225_b2
 * write, the same words as Wolf's. */
static void ftKbLc_OnSwallow(HSD_GObj* gobj)
{
    mu_ak_kirby_attach_hat(gobj);
}

/* [OnKirbyLoseAbility] +0x0C0 (23 words): take the hat off. Instruction for instruction the retail
 * ftKb_SpecialN_800EFAF0. A freeze that is out is not touched here. */
static void ftKbLc_OnLose(HSD_GObj* gobj)
{
    ftKb_SpecialN_800EFAF0(gobj);
}

/* [OnKirbyHurt] +0x1E8 (1 word): empty. */
static void ftKbLc_OnHurt(HSD_GObj* gobj) {}

/* [InitCopyItems] +0x1EC (3 words): the hat data's first extra word (+0xC) is the article data of
 * Lucas's article 10. */
static void ftKbLc_InitCopyItems(FighterKind kind, KirbyHatStruct* hat)
{
    mu_ak_register_article(kind, DISC_GET(Article, hat->hat_dynamics[0]), ftLc_Art_KirbyPKFreeze);
}

/* +0xBA0 (23 words), Lucas's "Lucas_RemoveSpecialItemGOBJ" (ftLc_RemovePKFreeze) with Kirby's
 * word: destroy the freeze and drop the callbacks. Kirby's damage and death callback while a
 * freeze is out. */
static void ftKbLc_RemovePKFreeze(HSD_GObj* gobj)
{
    Fighter* fp;
    Item_GObj** held;
    if (gobj == NULL) {
        return;
    }
    fp = GET_FIGHTER(gobj);
    if (fp == NULL) {
        return;
    }
    held = ftKbLc_PKFreeze(fp);
    if (*held != NULL) {
        Item_8026A8EC(*held);
        *held = NULL;
    }
    fp->death2_cb = NULL;
    fp->take_dmg_cb = NULL;
}

/* +0x2C4 (31 words), Lucas's "SpecialN_Init" with the hat's parameters. */
static void ftKbLc_SpecialN_Init(HSD_GObj* gobj)
{
    Fighter* fp = GET_FIGHTER(gobj);
    ftLucasAttributes* attrs = ftKbLc_Attrs(fp);
    ftLucas_MotionVars* mv = ftLc_MV(fp);

    if (attrs != NULL) {
        ftLc_X233C(fp) = attrs->x0_PKFREEZE_UNK;
        mv->specialn.hold_min = attrs->x4_PKFREEZE_HOLD_MIN;
        mv->specialn.end_delay = attrs->x8_PKFREEZE_END_DELAY;
        mv->specialn.gravity_delay = attrs->xC_PKFREEZE_GRAVITY_DELAY;
    }
    mv->specialn.release_delay = 10;
    *ftKbLc_PKFreeze(fp) = NULL;
    fp->cmd_vars[0] = 0;
    fp->cmd_vars[1] = 0;
    fp->cmd_vars[2] = 0;
    fp->cmd_vars[3] = 0;
    fp->death2_cb = NULL;
    fp->take_dmg_cb = NULL;
}

/* [SpecialN] +0x11C (22 words): KirbyStateChange(gobj, 0, 0, NULL, 0.0, 1.0, 0.0). */
static void ftKbLc_SpecialN_Enter(HSD_GObj* gobj)
{
    mu_ak_kirby_state_change(gobj, ftKbLc_State_SpecialNStart, Ft_MF_None, 0.0f, 1.0f, 0.0f);
    ftKbLc_SpecialN_Init(gobj);
    ftAnim_8006EBA4(gobj);
}

/* [SpecialAirN] +0x174 (29 words) */
static void ftKbLc_SpecialAirN_Enter(HSD_GObj* gobj)
{
    Fighter* fp = GET_FIGHTER(gobj);
    mu_ak_kirby_state_change(gobj, ftKbLc_State_SpecialAirNStart, Ft_MF_None, 0.0f, 1.0f, 0.0f);
    ftKbLc_SpecialN_Init(gobj);
    fp->self_vel.y = 0.0f;
    ftAnim_8006EBA4(gobj);
}

/* +0x970 (72 words), Lucas's "SpecialN_Start_AnimCB_Shared": at the end of the start animation,
 * go to the hold state and spawn the PK Freeze above Kirby. The spawn (+0xA90, 68 words) and the
 * projectile's setup (+0xBFC, 65 words) are the same instructions as Lucas's "ItemSpawn_PKFreeze"
 * and "Init_PKFreeze": ftLc_PKFreeze_Spawn serves both fighters. */
static void ftKbLc_SpecialNStart_AnimShared(HSD_GObj* gobj, int next)
{
    Fighter* fp = GET_FIGHTER(gobj);
    Item_GObj** held = ftKbLc_PKFreeze(fp);

    if (ftAnim_IsFramesRemaining(gobj)) {
        return;
    }
    mu_ak_kirby_state_change(gobj, next, Ft_MF_None, 0.0f, 1.0f, 0.0f);
    if (*held == NULL) {
        Vec3 pos;
        ItemKind kind;
        lb_8000B1CC(fp->parts[FTKBLC_FREEZE_PART].joint, NULL, &pos);
        pos.y += fp->x34_scale.y * 3.0f;
        pos.z = 0.0f;
        kind = mu_ak_article_kind(gobj, ftLc_Art_KirbyPKFreeze);
        if ((int) kind < 0) {
            /* Not an m-ex case: no item kind for the article. */
            OSReport("[ak] Kirby (Lucas): no item kind for the PK Freeze article (copied kind %d)\n",
                     (int) fp->u.kb.hat.kind);
        } else {
            *held = ftLc_PKFreeze_Spawn(gobj, &pos, kind, fp->facing_dir);
        }
        if (*held != NULL) {
            static int logged;
            fp->death2_cb = ftKbLc_RemovePKFreeze;
            fp->take_dmg_cb = ftKbLc_RemovePKFreeze;
            if (logged < FTKBLC_LOG) {
                logged++;
                OSReport("[ak] Kirby (Lucas): PK Freeze, item kind %d, spawned at (%.2f, %.2f) in "
                         "state %d\n",
                         (int) kind, pos.x, pos.y, next);
            }
        }
    }
    /* No midair jump after PK Freeze. */
    fp->x1968_jumpsUsed = fp->co_attrs.max_jumps;
}

/* +0x3E8 and +0x6E0 (29 words each, the same code with the next state 2 or 5), Lucas's
 * "SpecialN_Hold_AnimCB_Shared": hold until the minimum time is over and the freeze has left its
 * flying state (or is gone), then wait out the end delay. */
static void ftKbLc_SpecialNHold_AnimShared(HSD_GObj* gobj, int next)
{
    Fighter* fp = GET_FIGHTER(gobj);
    ftLucas_MotionVars* mv = ftLc_MV(fp);
    Item_GObj* freeze;

    if (mv->specialn.hold_min != 0) {
        mv->specialn.hold_min--;
        return;
    }
    freeze = *ftKbLc_PKFreeze(fp);
    if (freeze != NULL && GET_ITEM(freeze)->msid == 0) {
        return;
    }
    if (mv->specialn.end_delay > 0) {
        mv->specialn.end_delay--;
        return;
    }
    mu_ak_kirby_state_change(gobj, next, Ft_MF_None, 0.0f, 1.0f, 0.0f);
}

/* The head of +0x52C and +0x7F4, Lucas's "SpecialN_End_AnimCB_Shared": let go of the freeze. */
static void ftKbLc_SpecialNEnd_AnimShared(HSD_GObj* gobj)
{
    Fighter* fp = GET_FIGHTER(gobj);
    *ftKbLc_PKFreeze(fp) = NULL;
    fp->death2_cb = NULL;
    fp->take_dmg_cb = NULL;
}

/* +0x360, +0x4A4 (34 words each): leaving the ground keeps the frame and goes to the air state. */
static void ftKbLc_SpecialN_GroundToAir(HSD_GObj* gobj, int state)
{
    Fighter* fp = GET_FIGHTER(gobj);
    if (!ft_80082708(gobj)) {
        ftCommon_8007D5D4(fp);
        mu_ak_kirby_state_change(gobj, state, ftLc_MF_Switch, fp->cur_anim_frame, 1.0f, 0.0f);
    }
}

/* +0x644, +0x758, +0x8D4 (39 words each): landing, tested with the box of the parameters (+0x20). */
static void ftKbLc_SpecialN_AirToGround(HSD_GObj* gobj, int state)
{
    Fighter* fp = GET_FIGHTER(gobj);
    ftLucasAttributes* attrs = ftKbLc_Attrs(fp);
    ftCollisionBox box;
    if (attrs == NULL) {
        return;
    }
    box = ftCollisionBox_FromDisc(&attrs->x20_PKFREEZE_LAND_BOX);
    if (ft_800824A0(gobj, &box)) {
        ftCommon_8007D7FC(fp);
        mu_ak_kirby_state_change(gobj, state, ftLc_MF_Switch, fp->cur_anim_frame, 1.0f, 0.0f);
    }
}

/* ---- grounded states ---- */

/* [state 0 Anim] +0x340 (2 words) */
static void ftKbLc_SpecialNStart_Anim(HSD_GObj* gobj)
{
    ftKbLc_SpecialNStart_AnimShared(gobj, ftKbLc_State_SpecialNHold);
}

/* [state 0 Coll] +0x360 */
static void ftKbLc_SpecialNStart_Coll(HSD_GObj* gobj)
{
    ftKbLc_SpecialN_GroundToAir(gobj, ftKbLc_State_SpecialAirNStart);
}

/* [state 1 Anim] +0x3E8 */
static void ftKbLc_SpecialNHold_Anim(HSD_GObj* gobj)
{
    ftKbLc_SpecialNHold_AnimShared(gobj, ftKbLc_State_SpecialNEnd);
}

/* [state 1 IASA, state 4 IASA] +0x45C (17 words), Lucas's "SpecialN_Hold_IASACB_Shared" with
 * Kirby's word: after a short delay, releasing B lets go of the freeze (it sees its holder's word
 * cleared and bursts). */
static void ftKbLc_SpecialNHold_IASA(HSD_GObj* gobj)
{
    Fighter* fp = GET_FIGHTER(gobj);
    ftLucas_MotionVars* mv = ftLc_MV(fp);
    Item_GObj** held = ftKbLc_PKFreeze(fp);

    if (mv->specialn.release_delay > 0) {
        mv->specialn.release_delay--;
        return;
    }
    if (fp->input.held_buttons[0] & HSD_PAD_B) {
        return;
    }
    if (*held != NULL) {
        *held = NULL;
        fp->death2_cb = NULL;
        fp->take_dmg_cb = NULL;
    }
}

/* [state 1 Coll] +0x4A4 */
static void ftKbLc_SpecialNHold_Coll(HSD_GObj* gobj)
{
    ftKbLc_SpecialN_GroundToAir(gobj, ftKbLc_State_SpecialAirNHold);
}

/* [state 2 Anim] +0x52C (35 words). The code also asks for the hat data here and does not use it. */
static void ftKbLc_SpecialNEnd_Anim(HSD_GObj* gobj)
{
    Fighter* fp = GET_FIGHTER(gobj);
    ftKbLc_SpecialNEnd_AnimShared(gobj);
    ftPartSetRotX(fp, 0, 0.0f);
    if (!ftAnim_IsFramesRemaining(gobj)) {
        ft_8008A2BC(gobj);
    }
}

/* ---- aerial states ---- */

/* [state 3 Anim] +0x5BC (2 words) */
static void ftKbLc_SpecialAirNStart_Anim(HSD_GObj* gobj)
{
    ftKbLc_SpecialNStart_AnimShared(gobj, ftKbLc_State_SpecialAirNHold);
}

/* [state 3 Phys] +0x5C8 (31 words); [state 4 Phys] +0x754 and [state 5 Phys] +0x8D0 are branches
 * to it. Lucas's aerial physics with the hat's fall acceleration. */
static void ftKbLc_SpecialAirN_Phys(HSD_GObj* gobj)
{
    Fighter* fp = GET_FIGHTER(gobj);
    ftLucasAttributes* attrs = ftKbLc_Attrs(fp);
    ftLucas_MotionVars* mv = ftLc_MV(fp);
    if (mv->specialn.gravity_delay != 0) {
        mv->specialn.gravity_delay--;
    } else if (attrs != NULL) {
        ftCommon_Fall(fp, attrs->x14_PKFREEZE_FALL_ACCEL, fp->co_attrs.terminal_velocity);
    }
    ftCommon_CalcSelfAccel_Deaccel(fp, fp->co_attrs.aerial_friction);
}

/* [state 3 Coll] +0x644 */
static void ftKbLc_SpecialAirNStart_Coll(HSD_GObj* gobj)
{
    ftKbLc_SpecialN_AirToGround(gobj, ftKbLc_State_SpecialNStart);
}

/* [state 4 Anim] +0x6E0 */
static void ftKbLc_SpecialAirNHold_Anim(HSD_GObj* gobj)
{
    ftKbLc_SpecialNHold_AnimShared(gobj, ftKbLc_State_SpecialAirNEnd);
}

/* [state 4 Coll] +0x758 */
static void ftKbLc_SpecialAirNHold_Coll(HSD_GObj* gobj)
{
    ftKbLc_SpecialN_AirToGround(gobj, ftKbLc_State_SpecialNHold);
}

/* [state 5 Anim] +0x7F4 (55 words): the landing lag is read before the animation test, as in
 * Lucas's own. */
static void ftKbLc_SpecialAirNEnd_Anim(HSD_GObj* gobj)
{
    Fighter* fp = GET_FIGHTER(gobj);
    ftLucasAttributes* attrs = ftKbLc_Attrs(fp);
    float landing_lag = attrs != NULL ? attrs->x1C_PKFREEZE_LANDING_LAG : 0.0f;
    ftKbLc_SpecialNEnd_AnimShared(gobj);
    if (!ftAnim_IsFramesRemaining(gobj)) {
        if (landing_lag == 0.0f) {
            ftCo_Fall_Enter(gobj);
        } else {
            ftCo_80096900(gobj, 1, 0, true, 1.0f, landing_lag);
        }
    }
}

/* [state 5 Coll] +0x8D4 */
static void ftKbLc_SpecialAirNEnd_Coll(HSD_GObj* gobj)
{
    ftKbLc_SpecialN_AirToGround(gobj, ftKbLc_State_SpecialNEnd);
}

/* [move_logic] +0x1F8 (six entries of 0x20 bytes). Words +4 and +8 are Lucas's own (move id
 * 0x12); KirbyStateChange does not read them. States 2 and 5 have no IASA callback and state 2 has
 * no collision callback, in the hat file and in Lucas's own table alike. The callbacks that are
 * the same instructions as Lucas's own are Lucas's functions:
 *   [state 0 IASA] +0x348 and [state 3 IASA] +0x5C4, a lone return;
 *   [state 0 Phys] +0x34C, the gravity delay counting down on the ground too;
 *   [state 1 Phys] +0x4A0 and [state 2 Phys] +0x5B8, a branch to ft_80084F3C. */
static const MotionState ftKbLc_MotionStateTable[ftKbLc_State_Count] = {
    {
        /* state 0, ftcmd 0 "LcSpecialNStart" */
        0,
        0x00340111,
        0x12 << 24,
        ftKbLc_SpecialNStart_Anim,
        ftLc_SpecialNStart_IASA,
        ftLc_SpecialNStart_Phys,
        ftKbLc_SpecialNStart_Coll,
        ftCamera_UpdateCameraBox,
    },
    {
        /* state 1, ftcmd 1 "LcSpecialNHold" */
        1,
        0x00340111,
        0x12 << 24,
        ftKbLc_SpecialNHold_Anim,
        ftKbLc_SpecialNHold_IASA,
        ftLc_SpecialNHold_Phys,
        ftKbLc_SpecialNHold_Coll,
        ftCamera_UpdateCameraBox,
    },
    {
        /* state 2, ftcmd 3 "LcSpecialNFire" */
        3,
        0x00340111,
        0x12 << 24,
        ftKbLc_SpecialNEnd_Anim,
        NULL,
        ftLc_SpecialNEnd_Phys,
        NULL,
        ftCamera_UpdateCameraBox,
    },
    {
        /* state 3, ftcmd 4 "LcSpecialAirNStart" */
        4,
        0x00340511,
        0x12 << 24,
        ftKbLc_SpecialAirNStart_Anim,
        ftLc_SpecialAirNStart_IASA,
        ftKbLc_SpecialAirN_Phys,
        ftKbLc_SpecialAirNStart_Coll,
        ftCamera_UpdateCameraBox,
    },
    {
        /* state 4, ftcmd 5 "LcSpecialAirNHold" */
        5,
        0x00340511,
        0x12 << 24,
        ftKbLc_SpecialAirNHold_Anim,
        ftKbLc_SpecialNHold_IASA,
        ftKbLc_SpecialAirN_Phys,
        ftKbLc_SpecialAirNHold_Coll,
        ftCamera_UpdateCameraBox,
    },
    {
        /* state 5, ftcmd 7 "LcSpecialAirNFire" */
        7,
        0x00340511,
        0x12 << 24,
        ftKbLc_SpecialAirNEnd_Anim,
        NULL,
        ftKbLc_SpecialAirN_Phys,
        ftKbLc_SpecialAirNEnd_Coll,
        ftCamera_UpdateCameraBox,
    },
};

/* The kbFunction exports of PlKbCpLc.dat (0 to 6; it has no export 7). */
const MuAkKirbyCopy ftKbLc_Copy = {
    .on_swallow = ftKbLc_OnSwallow,
    .on_lose = ftKbLc_OnLose,
    .special_n = ftKbLc_SpecialN_Enter,
    .special_air_n = ftKbLc_SpecialAirN_Enter,
    .on_hurt = ftKbLc_OnHurt,
    .init_items = ftKbLc_InitCopyItems,
    .move_logic = ftKbLc_MotionStateTable,
    .move_logic_count = ftKbLc_State_Count,
    .on_frame = NULL,
};
