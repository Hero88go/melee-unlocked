/* ACE 2.0.0's Ninten: the ability Kirby copies from him (m-ex "kbFunction" of PlKbCpNt.dat).
 *
 * Kirby's Ninten ability is PK Hypnosis, which is Lucas's PK Freeze: the hat file's code is the
 * code of Akaneia's Lucas hat (PlKbCpLc.dat, akaneia/lucas/lucas_kirby.c) built again. Compared
 * routine by routine after relocation (run-source/rel09-ace-native/ninten/kirby/KIRBY.md):
 * 18 routines are word for word the Lucas hat's, the others do the same steps with other
 * registers or another order of independent stores, and one value differs: the projectile is
 * Ninten's article 3 (item kind 332 on ACE, data at hat data +0xC) where Lucas's hat has
 * Lucas's article 10. The parameters (hat data +0x10) have the layout and the values of the
 * Lucas hat's (the start of ftLucasAttributes). The freeze Kirby holds is kept at console
 * fp+0x2270 (ftKbLc_PKFreeze, Lucas's accessor) as in Lucas's hat, which is what the article's
 * code (Lucas's PK Freeze table, ftLc_PKFreeze_HeldSlot) reads for a Kirby holder.
 *
 * Lucas's hat C is static, so the routines are written again here; each comment says which
 * Lucas hat routine it equals and how. Where the hat routine is the same instructions as a
 * function of Lucas's own (lucas_specialn.c, lucas_it_pkfreeze.c), that function is used.
 *
 * The hat file has no debug symbols. Each function names the offset of its routine in the hat
 * file's code block (run-source/rel09-ace-native/ninten/kirby/PlKbCpNt.listing.txt shows it at
 * 0x81800000 plus the offset) and its size in words. The code has no fixed console address:
 * m-ex loads it with the hat file.
 *
 * Nothing below keeps state outside Kirby's Fighter; the `logged` counter only limits the log. */
#include "../../ace/ninten/ninten.h"

#include <dolphin/os.h>

#include <melee/ft/fighter.h>
#include <melee/ft/ft_081B.h>
#include <melee/ft/ft_084E.h>
#include <melee/ft/ft_0892.h>
#include <melee/ft/ftanim.h>
#include <melee/ft/ftcamera.h>
#include <melee/ft/ftcommon.h>
#include <melee/ft/ftparts.h>
#include <melee/ft/inlines.h>
#include <melee/ft/kinds/ftCommon/ftCo_Fall.h>
#include <melee/ft/kinds/ftCommon/ftCo_FallSpecial.h>
#include <melee/ft/kinds/ftKirby/ftkirby.h>
#include <melee/it/item.h>
#include <melee/lb/lb_00B0.h>
#include <sysdolphin/baselib/controller.h>

#include "ninten_kirby.h"

/* Kirby's states of this ability (KirbyStateChange state numbers; motion ids 400 to 405). They
 * mirror Ninten's own 342 to 347 (Lucas's ids). */
enum {
    ftKbNt_State_SpecialNStart,    /* 0 */
    ftKbNt_State_SpecialNHold,     /* 1 */
    ftKbNt_State_SpecialNEnd,      /* 2 */
    ftKbNt_State_SpecialAirNStart, /* 3 */
    ftKbNt_State_SpecialAirNHold,  /* 4 */
    ftKbNt_State_SpecialAirNEnd,   /* 5 */
    ftKbNt_State_Count
};

/* Kirby's joint the projectile appears above: a raw fp->parts index (console fp->parts + 0x40),
 * as in Lucas's hat. */
#define FTKBNT_PK_PART 4

#define FTKBNT_LOG 4 /* evidence lines that go in the log, per run */

/* The ability's parameters: the hat data's second extra word (+0x10), MEX_GetKirbyCpData(kind)
 * + 0x10. On the disc the block is the Lucas hat's, word for word (+0x00 to +0x40 read). NULL
 * when Kirby no longer holds an added ability (not an m-ex case: the console reads through
 * whatever its table holds for the kind); the readers then skip the read. */
static ftLucasAttributes* ftKbNt_Attrs(Fighter* fp)
{
    KirbyHatStruct* hat = mu_ak_kirby_hat(fp->u.kb.hat.kind);
    if (hat == NULL) {
        return NULL;
    }
    return DISC_GET(ftLucasAttributes, hat->hat_dynamics[1]);
}

/* [OnKirbySwallow] +0x000 (44 words): put the hat on. The same calls with the same arguments as
 * the Lucas hat's swallow (the retail hat attach without the x2225_b2 write); this build keeps
 * the gobj and the joint on the stack where that one keeps them in registers. */
static void ftKbNt_OnSwallow(HSD_GObj* gobj)
{
    mu_ak_kirby_attach_hat(gobj);
}

/* [OnKirbyLoseAbility] +0x0B0 (22 words): take the hat off. The steps of the retail
 * ftKb_SpecialN_800EFAF0 (remove the joint, clear it, free the display list array, clear it),
 * with the array's pointer loaded before the joint word is cleared. A projectile that is out is
 * not touched. */
static void ftKbNt_OnLose(HSD_GObj* gobj)
{
    ftKb_SpecialN_800EFAF0(gobj);
}

/* [OnKirbyHurt] +0x1D0 (1 word): empty. */
static void ftKbNt_OnHurt(HSD_GObj* gobj) {}

/* [InitCopyItems] +0x1D4 (3 words): MEX_IndexFighterItem(kind, hat +0xC, 3). The Lucas hat's
 * with 3 where it has 10. */
static void ftKbNt_InitCopyItems(FighterKind kind, KirbyHatStruct* hat)
{
    mu_ak_register_article(kind, DISC_GET(Article, hat->hat_dynamics[0]),
                           ftKbNt_Article_PKHypnosis);
}

/* +0xB88 (27 words): destroy the projectile and drop the callbacks. Kirby's damage and death
 * callback while it is out. The Lucas hat's +0xBA0 (ftKbLc_RemovePKFreeze) with the Fighter
 * kept in another register; the same tests and stores. */
static void ftKbNt_RemovePKHypnosis(HSD_GObj* gobj)
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

/* +0x2AC (31 words): the move's setup. The Lucas hat's +0x2C4 (ftKbLc_SpecialN_Init) with the
 * same stores in another order (none of them overlaps another). */
static void ftKbNt_SpecialN_Init(HSD_GObj* gobj)
{
    Fighter* fp = GET_FIGHTER(gobj);
    ftLucasAttributes* attrs = ftKbNt_Attrs(fp);
    ftLucas_MotionVars* mv = ftLc_MV(fp);

    fp->cmd_vars[0] = 0;
    fp->cmd_vars[1] = 0;
    fp->cmd_vars[2] = 0;
    fp->cmd_vars[3] = 0;
    *ftKbLc_PKFreeze(fp) = NULL;
    if (attrs != NULL) {
        ftLc_X233C(fp) = attrs->x0_PKFREEZE_UNK;
        mv->specialn.hold_min = attrs->x4_PKFREEZE_HOLD_MIN;
        mv->specialn.end_delay = attrs->x8_PKFREEZE_END_DELAY;
        mv->specialn.gravity_delay = attrs->xC_PKFREEZE_GRAVITY_DELAY;
    }
    fp->death2_cb = NULL;
    fp->take_dmg_cb = NULL;
    mv->specialn.release_delay = 10;
}

/* [SpecialN] +0x108 (22 words): KirbyStateChange(gobj, 0, 0, 0, 0.0, 1.0, 0.0), the setup,
 * ftAnim_8006EBA4. The Lucas hat's +0x11C word for word but the call distance. */
static void ftKbNt_SpecialN_Enter(HSD_GObj* gobj)
{
    mu_ak_kirby_state_change(gobj, ftKbNt_State_SpecialNStart, Ft_MF_None, 0.0f, 1.0f, 0.0f);
    ftKbNt_SpecialN_Init(gobj);
    ftAnim_8006EBA4(gobj);
}

/* [SpecialAirN] +0x160 (28 words): state 3, the setup, then self_vel.y = 0.0 and
 * ftAnim_8006EBA4. The Lucas hat's +0x174 with the Fighter loaded before the state change. */
static void ftKbNt_SpecialAirN_Enter(HSD_GObj* gobj)
{
    Fighter* fp = GET_FIGHTER(gobj);
    mu_ak_kirby_state_change(gobj, ftKbNt_State_SpecialAirNStart, Ft_MF_None, 0.0f, 1.0f, 0.0f);
    ftKbNt_SpecialN_Init(gobj);
    fp->self_vel.y = 0.0f;
    ftAnim_8006EBA4(gobj);
}

/* +0x960 (71 words): at the end of the start animation, go to the hold state and spawn the
 * projectile above Kirby. The Lucas hat's +0x970 (ftKbLc_SpecialNStart_AnimShared) with
 * article 3 where it has 10; the 0.0 constant is reloaded where that one keeps it in f31. The
 * height is one fmadds (scale.y * 3.0 + y). The spawn (+0xA7C, 67 words) does the stores and
 * calls of the Lucas hat's +0xA90, which LUCAS.md proves equal to Lucas's ItemSpawn_PKFreeze,
 * with the facing spilled to the stack where that one keeps it in f30 (KIRBY.md lists them);
 * the setup it calls (+0xBF4, 65 words) is the Lucas hat's +0xBFC word for word. So
 * ftLc_PKFreeze_Spawn serves. */
static void ftKbNt_SpecialNStart_AnimShared(HSD_GObj* gobj, int next)
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
        lb_8000B1CC(fp->parts[FTKBNT_PK_PART].joint, NULL, &pos);
        pos.y = MU_FMADDS(fp->x34_scale.y, 3.0f, pos.y);
        pos.z = 0.0f;
        kind = mu_ak_article_kind(gobj, ftKbNt_Article_PKHypnosis);
        if ((int) kind < 0) {
            /* Not an m-ex case: no item kind for the article. */
            OSReport("[ak] Kirby (Ninten): no item kind for the PK Hypnosis article (copied kind "
                     "%d)\n",
                     (int) fp->u.kb.hat.kind);
        } else {
            *held = ftLc_PKFreeze_Spawn(gobj, &pos, kind, fp->facing_dir);
        }
        if (*held != NULL) {
            static int logged;
            fp->death2_cb = ftKbNt_RemovePKHypnosis;
            fp->take_dmg_cb = ftKbNt_RemovePKHypnosis;
            if (logged < FTKBNT_LOG) {
                logged++;
                OSReport("[ak] Kirby (Ninten): PK Hypnosis, item kind %d, spawned at (%.2f, "
                         "%.2f) in state %d\n",
                         (int) kind, pos.x, pos.y, next);
            }
        }
    }
    /* No midair jump after the move. */
    fp->x1968_jumpsUsed = fp->co_attrs.max_jumps;
}

/* +0x3D4 and +0x6D0 (29 words each, the same code with the next state 2 or 5): the Lucas hat's
 * +0x3E8 and +0x6E0 word for word. */
static void ftKbNt_SpecialNHold_AnimShared(HSD_GObj* gobj, int next)
{
    Fighter* fp = GET_FIGHTER(gobj);
    ftLucas_MotionVars* mv = ftLc_MV(fp);
    Item_GObj* held;

    if (mv->specialn.hold_min != 0) {
        mv->specialn.hold_min--;
        return;
    }
    held = *ftKbLc_PKFreeze(fp);
    if (held != NULL && GET_ITEM(held)->msid == 0) {
        return;
    }
    if (mv->specialn.end_delay > 0) {
        mv->specialn.end_delay--;
        return;
    }
    mu_ak_kirby_state_change(gobj, next, Ft_MF_None, 0.0f, 1.0f, 0.0f);
}

/* The head of +0x51C and +0x7E4: let go of the projectile. */
static void ftKbNt_SpecialNEnd_AnimShared(HSD_GObj* gobj)
{
    Fighter* fp = GET_FIGHTER(gobj);
    *ftKbLc_PKFreeze(fp) = NULL;
    fp->death2_cb = NULL;
    fp->take_dmg_cb = NULL;
}

/* +0x348, +0x490 (35 words each): leaving the ground keeps the frame and goes to the air state
 * (flags 0x0C4C5080, not read by KirbyStateChange). The Lucas hat's +0x360 and +0x4A4 with the
 * Fighter loaded before the floor test. */
static void ftKbNt_SpecialN_GroundToAir(HSD_GObj* gobj, int state)
{
    Fighter* fp = GET_FIGHTER(gobj);
    if (!ft_80082708(gobj)) {
        ftCommon_8007D5D4(fp);
        mu_ak_kirby_state_change(gobj, state, ftLc_MF_Switch, fp->cur_anim_frame, 1.0f, 0.0f);
    }
}

/* +0x634, +0x748, +0x8C4 (39 words each): landing, tested with the box of the parameters
 * (+0x20). The Lucas hat's +0x644, +0x758, +0x8D4 word for word. */
static void ftKbNt_SpecialN_AirToGround(HSD_GObj* gobj, int state)
{
    Fighter* fp = GET_FIGHTER(gobj);
    ftLucasAttributes* attrs = ftKbNt_Attrs(fp);
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

/* [state 0 Anim] +0x328 (2 words) */
static void ftKbNt_SpecialNStart_Anim(HSD_GObj* gobj)
{
    ftKbNt_SpecialNStart_AnimShared(gobj, ftKbNt_State_SpecialNHold);
}

/* [state 0 Coll] +0x348 */
static void ftKbNt_SpecialNStart_Coll(HSD_GObj* gobj)
{
    ftKbNt_SpecialN_GroundToAir(gobj, ftKbNt_State_SpecialAirNStart);
}

/* [state 1 Anim] +0x3D4 */
static void ftKbNt_SpecialNHold_Anim(HSD_GObj* gobj)
{
    ftKbNt_SpecialNHold_AnimShared(gobj, ftKbNt_State_SpecialNEnd);
}

/* [state 1 IASA, state 4 IASA] +0x448 (17 words): the Lucas hat's +0x45C word for word. After a
 * short delay, releasing B lets go of the projectile. */
static void ftKbNt_SpecialNHold_IASA(HSD_GObj* gobj)
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

/* [state 1 Coll] +0x490 */
static void ftKbNt_SpecialNHold_Coll(HSD_GObj* gobj)
{
    ftKbNt_SpecialN_GroundToAir(gobj, ftKbNt_State_SpecialAirNHold);
}

/* [state 2 Anim] +0x51C (35 words): the Lucas hat's +0x52C word for word. The code also asks
 * for the hat data here and does not use it. */
static void ftKbNt_SpecialNEnd_Anim(HSD_GObj* gobj)
{
    Fighter* fp = GET_FIGHTER(gobj);
    ftKbNt_SpecialNEnd_AnimShared(gobj);
    ftPartSetRotX(fp, 0, 0.0f);
    if (!ftAnim_IsFramesRemaining(gobj)) {
        ft_8008A2BC(gobj);
    }
}

/* ---- aerial states ---- */

/* [state 3 Anim] +0x5AC (2 words) */
static void ftKbNt_SpecialAirNStart_Anim(HSD_GObj* gobj)
{
    ftKbNt_SpecialNStart_AnimShared(gobj, ftKbNt_State_SpecialAirNHold);
}

/* [state 3 Phys] +0x5B8 (31 words); [state 4 Phys] +0x744 and [state 5 Phys] +0x8C0 are branches
 * to it. The Lucas hat's +0x5C8 with the parameter pointer loaded inside the branch. */
static void ftKbNt_SpecialAirN_Phys(HSD_GObj* gobj)
{
    Fighter* fp = GET_FIGHTER(gobj);
    ftLucasAttributes* attrs = ftKbNt_Attrs(fp);
    ftLucas_MotionVars* mv = ftLc_MV(fp);
    if (mv->specialn.gravity_delay != 0) {
        mv->specialn.gravity_delay--;
    } else if (attrs != NULL) {
        ftCommon_Fall(fp, attrs->x14_PKFREEZE_FALL_ACCEL, fp->co_attrs.terminal_velocity);
    }
    ftCommon_CalcSelfAccel_Deaccel(fp, fp->co_attrs.aerial_friction);
}

/* [state 3 Coll] +0x634 */
static void ftKbNt_SpecialAirNStart_Coll(HSD_GObj* gobj)
{
    ftKbNt_SpecialN_AirToGround(gobj, ftKbNt_State_SpecialNStart);
}

/* [state 4 Anim] +0x6D0 */
static void ftKbNt_SpecialAirNHold_Anim(HSD_GObj* gobj)
{
    ftKbNt_SpecialNHold_AnimShared(gobj, ftKbNt_State_SpecialAirNEnd);
}

/* [state 4 Coll] +0x748 */
static void ftKbNt_SpecialAirNHold_Coll(HSD_GObj* gobj)
{
    ftKbNt_SpecialN_AirToGround(gobj, ftKbNt_State_SpecialNHold);
}

/* [state 5 Anim] +0x7E4 (55 words): the Lucas hat's +0x7F4 word for word. The landing lag is
 * read before the animation test. */
static void ftKbNt_SpecialAirNEnd_Anim(HSD_GObj* gobj)
{
    Fighter* fp = GET_FIGHTER(gobj);
    ftLucasAttributes* attrs = ftKbNt_Attrs(fp);
    float landing_lag = attrs != NULL ? attrs->x1C_PKFREEZE_LANDING_LAG : 0.0f;
    ftKbNt_SpecialNEnd_AnimShared(gobj);
    if (!ftAnim_IsFramesRemaining(gobj)) {
        if (landing_lag == 0.0f) {
            ftCo_Fall_Enter(gobj);
        } else {
            ftCo_80096900(gobj, 1, 0, true, 1.0f, landing_lag);
        }
    }
}

/* [state 5 Coll] +0x8C4 */
static void ftKbNt_SpecialAirNEnd_Coll(HSD_GObj* gobj)
{
    ftKbNt_SpecialN_AirToGround(gobj, ftKbNt_State_SpecialNEnd);
}

/* [move_logic] +0x1E0 (six entries of 0x20 bytes): the Lucas hat's table, the same ftcmd
 * indexes and words +4 and +8. KirbyStateChange does not read words +4 and +8. The callbacks
 * that are word for word the Lucas hat's and, through it, Lucas's own are Lucas's functions:
 *   [state 0 IASA] +0x330 and [state 3 IASA] +0x5B4, a lone return;
 *   [state 0 Phys] +0x334 (the Lucas hat's +0x34C);
 *   [state 1 Phys] +0x48C and [state 2 Phys] +0x5A8, a branch to ft_80084F3C. */
static const MotionState ftKbNt_MotionStateTable[ftKbNt_State_Count] = {
    {
        /* state 0, ftcmd 0 "LcSpecialNStart" */
        0,
        0x00340111,
        0x12 << 24,
        ftKbNt_SpecialNStart_Anim,
        ftLc_SpecialNStart_IASA,
        ftLc_SpecialNStart_Phys,
        ftKbNt_SpecialNStart_Coll,
        ftCamera_UpdateCameraBox,
    },
    {
        /* state 1, ftcmd 1 "LcSpecialNHold" */
        1,
        0x00340111,
        0x12 << 24,
        ftKbNt_SpecialNHold_Anim,
        ftKbNt_SpecialNHold_IASA,
        ftLc_SpecialNHold_Phys,
        ftKbNt_SpecialNHold_Coll,
        ftCamera_UpdateCameraBox,
    },
    {
        /* state 2, ftcmd 3 "LcSpecialNFire" */
        3,
        0x00340111,
        0x12 << 24,
        ftKbNt_SpecialNEnd_Anim,
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
        ftKbNt_SpecialAirNStart_Anim,
        ftLc_SpecialAirNStart_IASA,
        ftKbNt_SpecialAirN_Phys,
        ftKbNt_SpecialAirNStart_Coll,
        ftCamera_UpdateCameraBox,
    },
    {
        /* state 4, ftcmd 5 "LcSpecialAirNHold" */
        5,
        0x00340511,
        0x12 << 24,
        ftKbNt_SpecialAirNHold_Anim,
        ftKbNt_SpecialNHold_IASA,
        ftKbNt_SpecialAirN_Phys,
        ftKbNt_SpecialAirNHold_Coll,
        ftCamera_UpdateCameraBox,
    },
    {
        /* state 5, ftcmd 7 "LcSpecialAirNFire" */
        7,
        0x00340511,
        0x12 << 24,
        ftKbNt_SpecialAirNEnd_Anim,
        NULL,
        ftKbNt_SpecialAirN_Phys,
        ftKbNt_SpecialAirNEnd_Coll,
        ftCamera_UpdateCameraBox,
    },
};

/* The kbFunction exports of PlKbCpNt.dat (0 to 6; it has no export 7). */
const MuAkKirbyCopy ftKbNt_Copy = {
    .on_swallow = ftKbNt_OnSwallow,
    .on_lose = ftKbNt_OnLose,
    .special_n = ftKbNt_SpecialN_Enter,
    .special_air_n = ftKbNt_SpecialAirN_Enter,
    .on_hurt = ftKbNt_OnHurt,
    .init_items = ftKbNt_InitCopyItems,
    .move_logic = ftKbNt_MotionStateTable,
    .move_logic_count = ftKbNt_State_Count,
    .on_frame = NULL,
};
