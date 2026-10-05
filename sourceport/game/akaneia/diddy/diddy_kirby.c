/* Akaneia's Diddy Kong: the ability Kirby copies from him (m-ex "kbFunction" of PlKbCpDd.dat).
 *
 * Kirby's Diddy ability is the Peanut Popgun, state for state Diddy's own neutral special
 * (ftdiddyspecialn.c) with four things changed:
 *   - every state change goes through KirbyStateChange (the animation and script come from the
 *     hat file, ten states, motion ids 400 to 409);
 *   - the two articles are Diddy's articles 3 and 4 (item kinds 260 and 261 on Akaneia), whose
 *     data is in the hat file and whose code equals articles 0 and 1 (itdiddy.c);
 *   - the thirteen values of a shot are constants of the hat's code where Diddy reads his
 *     attributes, and the length of a whole charge is the constant 100 where Diddy measures two
 *     of his animations;
 *   - the swallow puts the hat on and then touches two of Kirby's animations (see
 *     ftKbDd_OnSwallow).
 * Everything else is the same routine. Where the hat's code is the same instructions as Diddy's
 * own, Diddy's function is used and the comment says so; where it only differs by the state
 * change, it is written again below.
 *
 * The hat file has no debug symbols: the names are Diddy's own for the routine in the same
 * place, and each function gives its offset in the hat file's code block
 * (run-source/rel09-b1-wolf/kirby/listings/PlKbCpDd.listing.txt shows it at 0x81800000 plus the
 * offset). The code has no fixed console address: m-ex loads it with the hat file.
 *
 * Nothing below keeps a kind or changes a static during a match (the `logged` counters only
 * limit the log): the gun and the charge live in Kirby's motion variables (console fp+2340 and
 * fp+2344, as in Diddy's own states), the articles are registered under the kind Kirby copied
 * and looked up through it (mu_ak_article_kind). */
#include "ftdiddy.h"

#include <dolphin/os.h>

#include <melee/ft/fighter.h>
#include <melee/ft/ft_081B.h>
#include <melee/ft/ft_084E.h>
#include <melee/ft/ft_0892.h>
#include <melee/ft/ftanim.h>
#include <melee/ft/ftcamera.h>
#include <melee/ft/ftcommon.h>
#include <melee/ft/ftdata.h>
#include <melee/ft/inlines.h>
#include <melee/ft/kinds/ftCommon/ftCo_Fall.h>
#include <melee/ft/kinds/ftKirby/ftkirby.h>
#include <melee/ft/types.h>
#include <melee/it/item.h>
#include <melee/lb/lbanim.h>
#include <sysdolphin/baselib/controller.h>
#include <sysdolphin/baselib/gobj.h>

#include "../common/mu_ak_kirby.h"

/* Kirby's states of this ability (KirbyStateChange state numbers; motion ids 400 to 409). The
 * air half mirrors the ground half five states up, as Diddy's own 341 to 350 do. */
enum {
    ftKbDd_State_SpecialNStart,     /* 0: pulls the popgun */
    ftKbDd_State_SpecialNCharge,    /* 1: counts while B is held */
    ftKbDd_State_SpecialNDanger,    /* 2: the popgun overheats */
    ftKbDd_State_SpecialNBlow,      /* 3: the popgun explodes */
    ftKbDd_State_SpecialNShoot,     /* 4 */
    ftKbDd_State_SpecialAirNStart,  /* 5 */
    ftKbDd_State_SpecialAirNCharge, /* 6 */
    ftKbDd_State_SpecialAirNDanger, /* 7 */
    ftKbDd_State_SpecialAirNBlow,   /* 8: also hurts Kirby */
    ftKbDd_State_SpecialAirNShoot,  /* 9 */
    ftKbDd_State_Count
};

/* Diddy's articles that are Kirby's (indexes into Diddy's MxDt item list). */
enum {
    ftKbDd_Article_Popgun = 3, /* item kind 260 on Akaneia */
    ftKbDd_Article_Peanut = 4, /* item kind 261 on Akaneia */
};

/* Kirby's animations the swallow looks up: the two numbers Diddy's onload measures in his own
 * file (ftDd_SM_SpecialNCharge and ftDd_SM_SpecialNDanger), applied to Kirby's animation table. */
#define FTKBDD_SWALLOW_ANIM_0 0x128
#define FTKBDD_SWALLOW_ANIM_1 0x129

/* The length of a whole charge, in frames. +0x424 (100.0), which the swallow stores at console
 * fp+2274 and the shot reads back from there. */
#define FTKBDD_CHARGE_FRAMES 100.0F

/* The air explosion's damage to Kirby. +0x41C (5.0). */
#define FTKBDD_AIR_BLOW_DAMAGE 5.0F

#define FTKBDD_LOG 8 /* evidence lines of each kind that go in the log, per run */

/* +0x3E0 (13 words): the values of a shot, the layout of the first thirteen attributes of
 * PlDd.dat. The swallow stores a pointer to them at console fp+2270 and the shot reads them
 * through it; here the shot is handed the table itself (see ftKbDd_OnSwallow). The two angles
 * are the words 3F490FE0 and 3DFA35E4, a little above 45 and 7 degrees. */
static const ftDd_PeanutParams ftKbDd_PeanutParams = {
    0.600000024F, /* specialn_angle_charge */
    2.20000005F,  /* specialn_speed_min */
    5.69999981F,  /* specialn_speed_max */
    0.785398483F, /* specialn_angle_min_charge */
    0.122173101F, /* specialn_angle_max_charge */
    3.0F,         /* specialn_dmg_min */
    12.0F,        /* specialn_dmg_max */
    10.0F,        /* specialn_bkb_min */
    40.0F,        /* specialn_bkb_max */
    50.0F,        /* specialn_kbg_min */
    100.0F,       /* specialn_kbg_max */
    0.0F,         /* specialn_recoil_min */
    1.39999998F,  /* specialn_recoil_max */
};

/* Every state lets a hit or a death take the gun away. The callback, +0x428 (19 words), is the
 * same instructions as Diddy's own Gun_Destroy: one function serves both. */
static inline void ftKbDd_SetCallbacks(Fighter* fp)
{
    Fighter_SetDamageCallbacks(fp, ftDd_SpecialN_GunDestroy, ftDd_SpecialN_GunDestroy);
}

/* ------------------------------------------------------------------------------------------ */
/* The exports                                                                                */
/* ------------------------------------------------------------------------------------------ */

/* [OnKirbySwallow] +0x000 (61 words): put the hat on (the retail hat attach without the
 * x2225_b2 write, skipped when Kirby already wears one), then, every time:
 *   - store the address of the shot's values (+0x3E0) at console fp+2270;
 *   - look up Kirby's animations 0x128 and 0x129 and take their lengths, which are not used
 *     (the leftover of Diddy's onload, which adds the two lengths of his own file);
 *   - store 100.0 at console fp+2274.
 * fp+2270 and fp+2274 are the first two words of Kirby's texture list for the retail "parts"
 * hats (fp->u.kb.x44), not read while a hat of this kind is worn. A native pointer does not fit
 * the first one, and both values are constants of the code, so neither is stored: the shot
 * takes them as arguments (ftKbDd_Shoot_Enter). The two lookups are kept: they load the
 * animation into the fighter's second animation buffer, as on the console. */
static void ftKbDd_OnSwallow(HSD_GObj* gobj)
{
    Fighter* fp = GET_FIGHTER(gobj);

    mu_ak_kirby_attach_hat(gobj);
    lbAnim_8001E8F8(ftData_80085E50(fp, FTKBDD_SWALLOW_ANIM_0));
    lbAnim_8001E8F8(ftData_80085E50(fp, FTKBDD_SWALLOW_ANIM_1));
}

/* [OnKirbyLoseAbility] +0x0F4 (23 words): take the hat off. Instruction for instruction the
 * retail ftKb_SpecialN_800EFAF0 (remove the hat joint, free its display list array). */
static void ftKbDd_OnLose(HSD_GObj* gobj)
{
    ftKb_SpecialN_800EFAF0(gobj);
}

/* [OnKirbyHurt] +0x250 (1 word): empty. */
static void ftKbDd_OnHurt(HSD_GObj* gobj) {}

/* [InitCopyItems] +0x254 (19 words): the hat file's two articles become Diddy's articles 3 and
 * 4. The hat data is a KirbyHatStruct whose first two extra words (+0xC, +0x10) are the
 * popgun's and the peanut's article data. */
static void ftKbDd_InitCopyItems(FighterKind kind, KirbyHatStruct* hat)
{
    mu_ak_register_article(kind, DISC_GET(Article, hat->hat_dynamics[0]), ftKbDd_Article_Popgun);
    mu_ak_register_article(kind, DISC_GET(Article, hat->hat_dynamics[1]), ftKbDd_Article_Peanut);
}

/* ------------------------------------------------------------------------------------------ */
/* Entering the states                                                                        */
/* ------------------------------------------------------------------------------------------ */

/* [SpecialN] +0x150 and [SpecialAirN] +0x1D0 (32 words each), Diddy's SpecialNStart_Enter:
 * KirbyStateChange(gobj, 0 or 5, 0, NULL, 0.0, 1.0, 0.0). */
static void ftKbDd_StartInit(HSD_GObj* gobj, int state)
{
    Fighter* fp = GET_FIGHTER(gobj);

    mu_ak_kirby_state_change(gobj, state, Ft_MF_None, 0.0F, 1.0F, 0.0F);
    ftAnim_8006EBA4(gobj);
    ftDd_MV(fp)->specialn.gun = NULL;
    ftDd_MV(fp)->specialn.charge = 0;
    fp->cmd_vars[0] = 0;
    ftKbDd_SetCallbacks(fp);
}

static void ftKbDd_SpecialN_Enter(HSD_GObj* gobj)
{
    ftKbDd_StartInit(gobj, ftKbDd_State_SpecialNStart);
}

static void ftKbDd_SpecialAirN_Enter(HSD_GObj* gobj)
{
    ftKbDd_StartInit(gobj, ftKbDd_State_SpecialAirNStart);
}

/* The end of [state 0 Anim] +0x474 and [state 5 Anim] +0xB94: into Charge. */
static void ftKbDd_SpecialNCharge_Enter(HSD_GObj* gobj, int state)
{
    Fighter* fp = GET_FIGHTER(gobj);

    mu_ak_kirby_state_change(gobj, state, Ft_MF_None, 0.0F, 1.0F, 0.0F);
    ftKbDd_SetCallbacks(fp);
}

/* The end of [state 1 Anim] +0x5D0 and [state 6 Anim] +0xCF0: into Danger, the gun's second
 * look. */
static void ftKbDd_SpecialNDanger_Enter(HSD_GObj* gobj, int state)
{
    Fighter* fp = GET_FIGHTER(gobj);

    mu_ak_kirby_state_change(gobj, state, Ft_MF_None, 0.0F, 1.0F, 0.0F);
    ftDd_SpecialN_GunChangeModel(ftDd_MV(fp)->specialn.gun, 1);
    ftKbDd_SetCallbacks(fp);
}

/* The end of [state 1 IASA] +0x664 and its three twins: into Shoot, and the shot itself.
 * [Gun_Shoot] +0x13B4 (173 words) is Diddy's routine with article 4, the values of +0x3E0 and
 * the charge length 100. */
static void ftKbDd_SpecialNShoot_Enter(HSD_GObj* gobj, int state)
{
    Fighter* fp = GET_FIGHTER(gobj);
    static int logged;

    mu_ak_kirby_state_change(gobj, state, Ft_MF_None, 0.0F, 1.0F, 0.0F);
    fp->cmd_vars[1] = 0;
    if (logged < FTKBDD_LOG) {
        logged++;
        OSReport("[ak] Kirby (Diddy Kong) popgun shot: charge %d of %.0f frames, state %d, %s\n",
                 ftDd_MV(fp)->specialn.charge, FTKBDD_CHARGE_FRAMES, state,
                 ftDd_MV(fp)->specialn.gun != NULL ? "gun in hand" : "NO gun");
    }
    ftDd_SpecialN_GunShootWith(gobj, ftKbDd_Article_Peanut, &ftKbDd_PeanutParams,
                               FTKBDD_CHARGE_FRAMES);
    ftKbDd_SetCallbacks(fp);
}

/* [SpecialNBlow_Enter] +0x1668 (29 words): the gun's third look. */
static void ftKbDd_SpecialNBlow_Enter(HSD_GObj* gobj)
{
    Fighter* fp = GET_FIGHTER(gobj);
    static int logged;

    mu_ak_kirby_state_change(gobj, ftKbDd_State_SpecialNBlow, Ft_MF_None, 0.0F, 1.0F, 0.0F);
    fp->cmd_vars[1] = 0;
    ftDd_SpecialN_GunChangeModel(ftDd_MV(fp)->specialn.gun, 2);
    ftKbDd_SetCallbacks(fp);
    if (logged < FTKBDD_LOG) {
        logged++;
        OSReport("[ak] Kirby (Diddy Kong) popgun blew up (ground)\n");
    }
}

/* [SpecialAirNBlow_Enter] +0x16DC (33 words): the air explosion also hurts Kirby. */
static void ftKbDd_SpecialAirNBlow_Enter(HSD_GObj* gobj)
{
    Fighter* fp = GET_FIGHTER(gobj);
    static int logged;

    mu_ak_kirby_state_change(gobj, ftKbDd_State_SpecialAirNBlow, Ft_MF_None, 0.0F, 1.0F, 0.0F);
    fp->cmd_vars[1] = 0;
    ftDd_SpecialN_GunChangeModel(ftDd_MV(fp)->specialn.gun, 2);
    Fighter_TakeDamage_8006CC7C(fp, FTKBDD_AIR_BLOW_DAMAGE);
    ftKbDd_SetCallbacks(fp);
    if (logged < FTKBDD_LOG) {
        logged++;
        OSReport("[ak] Kirby (Diddy Kong) popgun blew up (air)\n");
    }
}

/* The second half of every ground collision callback but Blow's (+0x544 and its twins): the
 * same state in the air, same frame, speed and blend. The flags the code passes (0x4202, as
 * Diddy's own) are not read by KirbyStateChange. */
static void ftKbDd_GroundToAir(HSD_GObj* gobj, int state)
{
    Fighter* fp = GET_FIGHTER(gobj);

    mu_ak_kirby_state_change(gobj, state, ftDd_MF_SwapN, fp->cur_anim_frame, fp->frame_speed_mul,
                             fp->x8A4_animBlendFrames);
    ftCommon_8007D5D4(fp);
    ftKbDd_SetCallbacks(fp);
}

/* The second half of every air collision callback (+0xC64 and its twins). */
static void ftKbDd_AirToGround(HSD_GObj* gobj, int state)
{
    Fighter* fp = GET_FIGHTER(gobj);

    mu_ak_kirby_state_change(gobj, state, ftDd_MF_SwapN, fp->cur_anim_frame, fp->frame_speed_mul,
                             fp->x8A4_animBlendFrames);
    ftCommon_8007D6A4(fp);
    ftKbDd_SetCallbacks(fp);
}

/* ------------------------------------------------------------------------------------------ */
/* Shared state bodies                                                                        */
/* ------------------------------------------------------------------------------------------ */

/* Start: pull the gun when the script says so, then charge. [Gun_Spawn] +0x12BC (62 words) is
 * Diddy's routine with article 3; the hand is the same part number (0x1F), looked up in Kirby's
 * own bone table. */
static void ftKbDd_StartAnim(HSD_GObj* gobj, int charge)
{
    Fighter* fp = GET_FIGHTER(gobj);

    if (ftDd_MV(fp)->specialn.gun == NULL && fp->cmd_vars[0] != 0) {
        static int logged;

        ftDd_SpecialN_GunSpawnArticle(gobj, ftKbDd_Article_Popgun);
        fp->cmd_vars[0] = 0;
        if (logged < FTKBDD_LOG) {
            logged++;
            OSReport("[ak] Kirby (Diddy Kong) popgun: article %d is item kind %d, %s\n",
                     ftKbDd_Article_Popgun,
                     (int) mu_ak_article_kind(gobj, ftKbDd_Article_Popgun),
                     ftDd_MV(fp)->specialn.gun != NULL ? "created" : "NOT created");
        }
    }
    if (!ftAnim_IsFramesRemaining(gobj)) {
        ftKbDd_SpecialNCharge_Enter(gobj, charge);
    }
}

/* Charge and Danger: count while B is held, fire on release. */
static void ftKbDd_ChargeInput(HSD_GObj* gobj, int shoot)
{
    Fighter* fp = GET_FIGHTER(gobj);

    if (fp->input.held_buttons[0] & HSD_PAD_B) {
        ftDd_MV(fp)->specialn.charge++;
    } else {
        ftKbDd_SpecialNShoot_Enter(gobj, shoot);
    }
}

/* ------------------------------------------------------------------------------------------ */
/* Ground states                                                                              */
/* ------------------------------------------------------------------------------------------ */

/* [state 0 Anim] +0x474 (50 words) */
static void ftKbDd_SpecialNStart_Anim(HSD_GObj* gobj)
{
    ftKbDd_StartAnim(gobj, ftKbDd_State_SpecialNCharge);
}

/* [state 0 Coll] +0x544 (35 words) */
static void ftKbDd_SpecialNStart_Coll(HSD_GObj* gobj)
{
    if (!ft_80082708(gobj)) {
        ftKbDd_GroundToAir(gobj, ftKbDd_State_SpecialAirNStart);
    }
}

/* [state 1 Anim] +0x5D0 (37 words) */
static void ftKbDd_SpecialNCharge_Anim(HSD_GObj* gobj)
{
    if (!ftAnim_IsFramesRemaining(gobj)) {
        ftKbDd_SpecialNDanger_Enter(gobj, ftKbDd_State_SpecialNDanger);
    }
}

/* [state 1 IASA] +0x664 and [state 2 IASA] +0x7EC (42 words each, the same instructions) */
static void ftKbDd_SpecialNCharge_IASA(HSD_GObj* gobj)
{
    ftKbDd_ChargeInput(gobj, ftKbDd_State_SpecialNShoot);
}

/* [state 1 Coll] +0x710 (35 words) */
static void ftKbDd_SpecialNCharge_Coll(HSD_GObj* gobj)
{
    if (!ft_80082708(gobj)) {
        ftKbDd_GroundToAir(gobj, ftKbDd_State_SpecialAirNCharge);
    }
}

/* [state 2 Anim] +0x79C (20 words) */
static void ftKbDd_SpecialNDanger_Anim(HSD_GObj* gobj)
{
    if (!ftAnim_IsFramesRemaining(gobj)) {
        ftKbDd_SpecialNBlow_Enter(gobj);
    }
}

/* [state 2 Coll] +0x898 (35 words) */
static void ftKbDd_SpecialNDanger_Coll(HSD_GObj* gobj)
{
    if (!ft_80082708(gobj)) {
        ftKbDd_GroundToAir(gobj, ftKbDd_State_SpecialAirNDanger);
    }
}

/* [state 4 Coll] +0xB08 (35 words) */
static void ftKbDd_SpecialNShoot_Coll(HSD_GObj* gobj)
{
    if (!ft_80082708(gobj)) {
        ftKbDd_GroundToAir(gobj, ftKbDd_State_SpecialAirNShoot);
    }
}

/* ------------------------------------------------------------------------------------------ */
/* Air states                                                                                 */
/* ------------------------------------------------------------------------------------------ */

/* [state 5 Anim] +0xB94 (50 words) */
static void ftKbDd_SpecialAirNStart_Anim(HSD_GObj* gobj)
{
    ftKbDd_StartAnim(gobj, ftKbDd_State_SpecialAirNCharge);
}

/* [state 5 Coll] +0xC64 (35 words) */
static void ftKbDd_SpecialAirNStart_Coll(HSD_GObj* gobj)
{
    if (ft_80081D0C(gobj)) {
        ftKbDd_AirToGround(gobj, ftKbDd_State_SpecialNStart);
    }
}

/* [state 6 Anim] +0xCF0 (37 words) */
static void ftKbDd_SpecialAirNCharge_Anim(HSD_GObj* gobj)
{
    if (!ftAnim_IsFramesRemaining(gobj)) {
        ftKbDd_SpecialNDanger_Enter(gobj, ftKbDd_State_SpecialAirNDanger);
    }
}

/* [state 6 IASA] +0xD84 and [state 7 IASA] +0xF0C (42 words each, the same instructions) */
static void ftKbDd_SpecialAirNCharge_IASA(HSD_GObj* gobj)
{
    ftKbDd_ChargeInput(gobj, ftKbDd_State_SpecialAirNShoot);
}

/* [state 6 Coll] +0xE30 (35 words) */
static void ftKbDd_SpecialAirNCharge_Coll(HSD_GObj* gobj)
{
    if (ft_80081D0C(gobj)) {
        ftKbDd_AirToGround(gobj, ftKbDd_State_SpecialNCharge);
    }
}

/* [state 7 Anim] +0xEBC (20 words) */
static void ftKbDd_SpecialAirNDanger_Anim(HSD_GObj* gobj)
{
    if (!ftAnim_IsFramesRemaining(gobj)) {
        ftKbDd_SpecialAirNBlow_Enter(gobj);
    }
}

/* [state 7 Coll] +0xFB8 (35 words) */
static void ftKbDd_SpecialAirNDanger_Coll(HSD_GObj* gobj)
{
    if (ft_80081D0C(gobj)) {
        ftKbDd_AirToGround(gobj, ftKbDd_State_SpecialNDanger);
    }
}

/* [state 8 Coll] +0x10EC (35 words) */
static void ftKbDd_SpecialAirNBlow_Coll(HSD_GObj* gobj)
{
    if (ft_80081D0C(gobj)) {
        ftKbDd_AirToGround(gobj, ftKbDd_State_SpecialNBlow);
    }
}

/* [state 9 Coll] +0x1230 (35 words) */
static void ftKbDd_SpecialAirNShoot_Coll(HSD_GObj* gobj)
{
    if (ft_80081D0C(gobj)) {
        ftKbDd_AirToGround(gobj, ftKbDd_State_SpecialNShoot);
    }
}

/* ------------------------------------------------------------------------------------------ */
/* The tables                                                                                 */
/* ------------------------------------------------------------------------------------------ */

/* [move_logic] +0x2A0 (ten entries of 0x20 bytes). Words +4 and +8 are Diddy's own (0x00340111
 * and move id 0x12); KirbyStateChange does not read them. The animation is the entry of the hat
 * file's ftcmd table with the state's own number (the names in that table are Wolf's, left over
 * from the file this one was made from; only the index counts).
 *
 * The callbacks that hold no state change are the same routines as Diddy's own and are his
 * functions:
 *   IASA of Start, Blow and Shoot (+0x53C, +0x9C4, +0xB00, +0xC5C, +0x10E4, +0x1228): empty;
 *   every Phys: ft_80084F3C on the ground (+0x540, +0x70C, +0x894, +0x9C8, +0xB04), ft_80084EEC
 *     in the air (+0xC60, +0xE2C, +0xFB4, +0x10E8, +0x122C);
 *   [state 3 Anim] +0x924, [state 4 Anim] +0xA50, [state 8 Anim] +0x1044, [state 9 Anim]
 *     +0x1178: the script's cmd_vars[1] puts the gun away, the end of the animation goes to Wait
 *     or Fall (the two Shoot ones also call m-ex's empty debugger hook, as PlDd.dat's do);
 *   [state 3 Coll] +0x9CC: off the ground the gun is dropped and Kirby falls. */
static const MotionState ftKbDd_MotionStateTable[ftKbDd_State_Count] = {
    {
        /* state 0 */
        0,
        0x00340111,
        0x12 << 24,
        ftKbDd_SpecialNStart_Anim,
        ftDd_SpecialNStart_IASA,
        ftDd_SpecialNStart_Phys,
        ftKbDd_SpecialNStart_Coll,
        ftCamera_UpdateCameraBox,
    },
    {
        /* state 1 */
        1,
        0x00340111,
        0x12 << 24,
        ftKbDd_SpecialNCharge_Anim,
        ftKbDd_SpecialNCharge_IASA,
        ftDd_SpecialNCharge_Phys,
        ftKbDd_SpecialNCharge_Coll,
        ftCamera_UpdateCameraBox,
    },
    {
        /* state 2 */
        2,
        0x00340111,
        0x12 << 24,
        ftKbDd_SpecialNDanger_Anim,
        ftKbDd_SpecialNCharge_IASA,
        ftDd_SpecialNDanger_Phys,
        ftKbDd_SpecialNDanger_Coll,
        ftCamera_UpdateCameraBox,
    },
    {
        /* state 3 */
        3,
        0x00340111,
        0x12 << 24,
        ftDd_SpecialNBlow_Anim,
        ftDd_SpecialNBlow_IASA,
        ftDd_SpecialNBlow_Phys,
        ftDd_SpecialNBlow_Coll,
        ftCamera_UpdateCameraBox,
    },
    {
        /* state 4 */
        4,
        0x00340111,
        0x12 << 24,
        ftDd_SpecialNShoot_Anim,
        ftDd_SpecialNShoot_IASA,
        ftDd_SpecialNShoot_Phys,
        ftKbDd_SpecialNShoot_Coll,
        ftCamera_UpdateCameraBox,
    },
    {
        /* state 5 */
        5,
        0x00340111,
        0x12 << 24,
        ftKbDd_SpecialAirNStart_Anim,
        ftDd_SpecialAirNStart_IASA,
        ftDd_SpecialAirNStart_Phys,
        ftKbDd_SpecialAirNStart_Coll,
        ftCamera_UpdateCameraBox,
    },
    {
        /* state 6 */
        6,
        0x00340111,
        0x12 << 24,
        ftKbDd_SpecialAirNCharge_Anim,
        ftKbDd_SpecialAirNCharge_IASA,
        ftDd_SpecialAirNCharge_Phys,
        ftKbDd_SpecialAirNCharge_Coll,
        ftCamera_UpdateCameraBox,
    },
    {
        /* state 7 */
        7,
        0x00340111,
        0x12 << 24,
        ftKbDd_SpecialAirNDanger_Anim,
        ftKbDd_SpecialAirNCharge_IASA,
        ftDd_SpecialAirNDanger_Phys,
        ftKbDd_SpecialAirNDanger_Coll,
        ftCamera_UpdateCameraBox,
    },
    {
        /* state 8 */
        8,
        0x00340111,
        0x12 << 24,
        ftDd_SpecialAirNBlow_Anim,
        ftDd_SpecialAirNBlow_IASA,
        ftDd_SpecialAirNBlow_Phys,
        ftKbDd_SpecialAirNBlow_Coll,
        ftCamera_UpdateCameraBox,
    },
    {
        /* state 9 */
        9,
        0x00340111,
        0x12 << 24,
        ftDd_SpecialAirNShoot_Anim,
        ftDd_SpecialAirNShoot_IASA,
        ftDd_SpecialAirNShoot_Phys,
        ftKbDd_SpecialAirNShoot_Coll,
        ftCamera_UpdateCameraBox,
    },
};

/* The kbFunction exports of PlKbCpDd.dat (0 to 6; it has no export 7). */
const MuAkKirbyCopy ftKbDd_Copy = {
    .on_swallow = ftKbDd_OnSwallow,
    .on_lose = ftKbDd_OnLose,
    .special_n = ftKbDd_SpecialN_Enter,
    .special_air_n = ftKbDd_SpecialAirN_Enter,
    .on_hurt = ftKbDd_OnHurt,
    .init_items = ftKbDd_InitCopyItems,
    .move_logic = ftKbDd_MotionStateTable,
    .move_logic_count = ftKbDd_State_Count,
    .on_frame = NULL,
};
