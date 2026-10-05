/* King Dedede (Akaneia): the ability Kirby copies from him (m-ex "kbFunction" of PlKbCpDe.dat).
 *
 * Kirby's Dedede ability is Dedede's inhale, state for state his own neutral special
 * (ftDe_SpecialN.c, states 345 to 370) with these things changed:
 *   - every state of Kirby's is entered through the hat file's own state change routine
 *     (CustomKirbyStateChange, +0x17F0: m-ex's KirbyStateChange compiled into the file, which
 *     keeps the caller's flags): ftKbDe_StateChange. The animation and script come from the hat
 *     file, 26 states, motion ids 400 to 425;
 *   - every value of the move is read from the hat data (the word at +0xC points at a block
 *     with the layout of the start of Dedede's attributes), not from Kirby's attributes:
 *     ftKbDe_Attrs;
 *   - the three walk animation lengths are the constants 60, 46 and 32 where Dedede measures
 *     his own animations;
 *   - the walk with a full mouth goes through the game's walk routine with motion state 10 and
 *     then enters Kirby's state of the number that routine picked (ftKbDe_SpecialN_ASWalk);
 *   - the inhale effect sits on Kirby's part 3 where Dedede's sits on his part 6;
 *   - a spat item is Dedede's article 5 (item kind 283 on Akaneia), whose data is in the hat
 *     file and whose code is article 1's again (itDe_Articles.c);
 *   - the star a spat fighter rides in is the joint at hat data +0x18.
 * Kirby's own fields of the inhale are the ones Dedede's code uses for himself (console
 * fp+230C to fp+2320: they are Kirby's inhale fields to begin with, fp->u.kb.xE0 to xF4).
 *
 * The other side of the inhale (what happens to the fighter or item that is pulled in, held and
 * spat out: 14 routines of the hat file, from SpecialN_OnVictim to StarSpitEnd_AccessoryCB) is
 * Dedede's own code again with the values taken from the hat data. Those routines are the ones
 * of ftDe_SpecialNCapture.c, which ask who inhaled (ftDe_CaptorAttrs);
 * run-source/rel09-b1-wolf/kirby/DEDEDE.md maps each.
 *
 * The hat file has debug symbols: the names below are Akaneia's, and each function gives its
 * offset in the hat file's code block (run-source/rel09-b1-wolf/kirby/listings/
 * PlKbCpDe.listing.txt shows it at 0x81800000 plus the offset). The code has no fixed console
 * address: m-ex loads it with the hat file.
 *
 * Nothing below keeps a kind or changes a static during a match (the `logged` counters only
 * limit the log). */
#include "ftDe.h"

#include <dolphin/os.h>

#include <melee/ef/eflib.h>
#include <melee/ef/efsync.h>
#include <melee/ft/fighter.h>
#include <melee/ft/ft_081B.h>
#include <melee/ft/ft_084E.h>
#include <melee/ft/ft_0892.h>
#include <melee/ft/ftaction.h>
#include <melee/ft/ftanim.h>
#include <melee/ft/ftcamera.h>
#include <melee/ft/ftcolanim.h>
#include <melee/ft/ftcommon.h>
#include <melee/ft/ftparts.h>
#include <melee/ft/ftwalkcommon.h>
#include <melee/ft/inlines.h>
#include <melee/ft/kinds/ftCommon/forward.h>
#include <melee/ft/kinds/ftCommon/ftCo_CaptureKirby.h>
#include <melee/ft/kinds/ftCommon/ftCo_CaptureWaitKirby.h>
#include <melee/ft/kinds/ftCommon/ftCo_Fall.h>
#include <melee/ft/kinds/ftCommon/ftCo_Jump.h>
#include <melee/ft/kinds/ftCommon/ftCo_KneeBend.h>
#include <melee/ft/kinds/ftKirby/ftkirby.h>
#include <melee/ft/types.h>
#include <melee/it/kinds/it_2F28.h>
#include <melee/it/kinds/itkirby_2F23.h>
#include <melee/it/types.h>
#include <melee/lb/lb_00B0.h>
#include <sysdolphin/baselib/gobj.h>

/* common/mu_ak_kirby.c: the ftcmd table the scene loaded for a fighter kind (m-ex keeps it per
 * kind at r2 + 0x128), NULL when there is none. Declared here as well as in mu_ak_kirby.h so
 * that this file does not depend on the order the two are edited in. */
MuAkKirbyCmd* mu_ak_kirby_cmd(FighterKind kind);

/* Dedede's own callbacks that the hat file carries unchanged (ftDe_SpecialN.c; their
 * prototypes there are local to ftDe_MotionStates.c). */
void ftDe_SpecialNEnd_Anim(HSD_GObj* gobj);
void ftDe_SpecialAirNEnd_Anim(HSD_GObj* gobj);
void ftDe_SpecialNSpit_Anim(HSD_GObj* gobj);
void ftDe_SpecialAirNSpit_Anim(HSD_GObj* gobj);
void ftDe_SpecialNEatJump2_Phys(HSD_GObj* gobj);

/* Kirby's states of this ability (state numbers of the hat's state change; motion ids 400 to
 * 425), in the order of Dedede's own 345 to 370. */
enum {
    ftKbDe_State_SpecialNStart,       /*  0 */
    ftKbDe_State_SpecialNLoop,        /*  1: inhaling while B is held */
    ftKbDe_State_SpecialNEnd,         /*  2 */
    ftKbDe_State_SpecialNGrab,        /*  3: a fighter is being pulled in */
    ftKbDe_State_SpecialNGrabItem,    /*  4: an item is being pulled in */
    ftKbDe_State_SpecialNEat,         /*  5 */
    ftKbDe_State_SpecialNEatWait,     /*  6: full mouth, standing */
    ftKbDe_State_SpecialNEatTurn,     /*  7 */
    ftKbDe_State_SpecialNSpit,        /*  8 */
    ftKbDe_State_SpecialNSpitItem,    /*  9 */
    ftKbDe_State_SpecialNEatWalkSlow, /* 10 */
    ftKbDe_State_SpecialNEatWalkMiddle,
    ftKbDe_State_SpecialNEatWalkFast,
    ftKbDe_State_SpecialNEatJump1,    /* 13 */
    ftKbDe_State_SpecialNEatJump2,    /* 14 */
    ftKbDe_State_SpecialNEatLanding,  /* 15 */
    ftKbDe_State_SpecialAirNStart,    /* 16 */
    ftKbDe_State_SpecialAirNLoop,
    ftKbDe_State_SpecialAirNEnd,
    ftKbDe_State_SpecialAirNGrab,
    ftKbDe_State_SpecialAirNGrabItem, /* 20 */
    ftKbDe_State_SpecialAirNEat,
    ftKbDe_State_SpecialAirNEatWait,  /* 22: full mouth, in the air */
    ftKbDe_State_SpecialAirNEatTurn,
    ftKbDe_State_SpecialAirNSpit,
    ftKbDe_State_SpecialAirNSpitItem, /* 25 */
    ftKbDe_State_Count
};

/* The motion id of state n (+0x18D0: addi r8, r25, 0x190), as m-ex's own routine gives. */
#define FTKBDE_MOTION_BASE 400

/* The words of the hat data after the KirbyHatStruct header (hat_dynamics[n] is +0xC + 4n). */
enum {
    ftKbDe_Hat_Attrs,     /* +0xC: the values of the move */
    ftKbDe_Hat_Article4,  /* +0x10: article data, registered as Dedede's article 4 */
    ftKbDe_Hat_Article5,  /* +0x14: the spit star's article data, Dedede's article 5 */
    ftKbDe_Hat_StarJoint, /* +0x18: the star a spat fighter rides in ("star_joint" of the file) */
};

/* The flags the hat's code passes when a state carries on across a ground or air change. They
 * are the ones Dedede's own code passes (ftDe_SpecialN.c). */
#define FTKBDE_MF_CARRY_START 0x001EDA96u     /* Start and Loop falling, Start landing */
#define FTKBDE_MF_CARRY_END 0x001FFFF6u       /* End falling or landing */
#define FTKBDE_MF_CARRY 0x0C4C5092u           /* ftCommon_GroundAirColl_MF | KeepGfx | SkipModel */
#define FTKBDE_MF_CARRY_EATWAIT 0x0C4C5090u   /* ftCommon_GroundAirColl_MF | SkipModel */
#define FTKBDE_MF_CARRY_LOOP_LAND 0x0C4C5A9Au /* ftCommon_GroundAirColl_MF | 0xA1A */

/* The three lengths the walk with a full mouth is given, +0x17E4 to +0x17EC. Dedede measures
 * his three EatWalk animations; the hat's code has the numbers. */
#define FTKBDE_EATWALK_SLOW_LEN 60.0F
#define FTKBDE_EATWALK_MIDDLE_LEN 46.0F
#define FTKBDE_EATWALK_FAST_LEN 32.0F

/* The motion state the game's walk routine is given (+0x243C: li r4, 0xa). It enters state
 * 10, 11 or 12 of the common table for a moment; the number is then used as Kirby's state. */
#define FTKBDE_WALK_MSID 10

/* The inhale effect: particle 0 of Dedede's effect file, on Kirby's part 3 (+0x63C). */
#define FTKBDE_EF_INHALE 0x1770
#define FTKBDE_INHALE_PART 3

/* The spat item: lifetime and slowing down per frame, +0x17DC (30.0) and +0x17D8 (0.13). */
#define FTKBDE_SPITSTAR_LIFETIME 30.0F
#define FTKBDE_SPITSTAR_DECEL 0.13F

#define FTKBDE_LOG 8 /* evidence lines of each kind that go in the log, per run */

static const MotionState ftKbDe_MotionStateTable[ftKbDe_State_Count];

static void ftKbDe_SpecialNGrab_Enter(HSD_GObj* gobj);
static void ftKbDe_SpecialAirNGrab_Enter(HSD_GObj* gobj);
static void ftKbDe_SpecialN_OnItem(HSD_GObj* gobj);
static void ftKbDe_SpecialN_ASWalk(HSD_GObj* gobj, float anim_start);

/* ------------------------------------------------------------------------------------------ */
/* The hat data                                                                               */
/* ------------------------------------------------------------------------------------------ */

/* MEX_GetKirbyCpData(fp->kb.hat.kind), for the routines of the move.
 *
 * On the console a routine that runs after Kirby lost the hat (a fighter he spat is still
 * flying as a star, and reads its thrower's values every frame) gets the table entry of
 * whatever kind Kirby holds by then. Here it gets the hat data of the Dedede in the scene, so
 * the star goes on with the same values. NULL only when the scene has no such hat. */
static KirbyHatStruct* ftKbDe_HatData(Fighter* fp)
{
    const MuAkFighter* ft = mu_ak_fighter(fp->u.kb.hat.kind);
    int kind;

    if (ft != NULL && ft->kirby == &ftKbDe_Copy) {
        return mu_ak_kirby_hat(fp->u.kb.hat.kind);
    }
    for (kind = MU_AK_KIND_BASE; kind < MU_AK_KIND_BASE + MU_AK_KIND_SLOTS; kind++) {
        ft = mu_ak_fighter(kind);
        if (ft != NULL && ft->kirby == &ftKbDe_Copy && mu_ak_kirby_hat((FighterKind) kind) != NULL) {
            return mu_ak_kirby_hat((FighterKind) kind);
        }
    }
    return NULL;
}

/* The word at hat data +0xC. In the file: 40 frames of loop at least, the mouth at (5.4, 3.4),
 * shrinking inside 7.4 by 0.45, pull 0.9 and 1.2 a frame, range 1.0, and so on: the fields of
 * ftDe_DatAttrs up to the star's collision box (+0x00 to +0x97 are read). */
ftDe_DatAttrs* ftKbDe_Attrs(Fighter* kirby)
{
    /* Not an m-ex case: no hat of Dedede's in the scene. Every value reads 0. */
    static const ftDe_DatAttrs none;
    KirbyHatStruct* hat = ftKbDe_HatData(kirby);
    static int logged;

    if (hat == NULL || DISC_NULL(hat->hat_dynamics[ftKbDe_Hat_Attrs])) {
        if (logged < FTKBDE_LOG) {
            logged++;
            OSReport("[ak] Kirby (King Dedede): no hat data for the inhale's values\n");
        }
        return (ftDe_DatAttrs*) &none;
    }
    return DISC_GET(ftDe_DatAttrs, hat->hat_dynamics[ftKbDe_Hat_Attrs]);
}

/* The word at hat data +0x18 (AS_EnterStarSpitState +0x158): the star's model. NULL without a
 * hat (not an m-ex case). */
HSD_Joint* ftKbDe_StarJoint(Fighter* kirby)
{
    KirbyHatStruct* hat = ftKbDe_HatData(kirby);

    if (hat == NULL) {
        return NULL;
    }
    return DISC_GET(HSD_Joint, hat->hat_dynamics[ftKbDe_Hat_StarJoint]);
}

/* The "is he standing or floating with a full mouth" test of the captured fighter's struggle
 * (ASID_CaputureKirbyWait_InterruptCB +0x60: the inhaler's motion id with bit 0x10 cleared is
 * compared with 6). It is Dedede's own test (351 or 367) with 345 taken off, which gives the
 * state numbers 6 and 22. Kirby's motion ids in those states are 406 and 422, so the test never
 * passes: a fighter in Kirby's mouth cannot make him hop or slide. Kept as the mod has it. */
bool ftKbDe_IsEatWait(Fighter* kirby)
{
    return ((int) kirby->motion_id & ~0x10) == ftKbDe_State_SpecialNEatWait;
}

/* ------------------------------------------------------------------------------------------ */
/* The hat file's own state change                                                            */
/* ------------------------------------------------------------------------------------------ */

/* [CustomKirbyStateChange] +0x17F0 (192 words). Next to m-ex's routine at 803D7080, which the
 * layer's mu_ak_kirby_state_change follows, it differs in two things:
 *   - the caller's flags are used: Fighter_ChangeMotionState gets flags | Ft_MF_SkipAnim
 *     (+0x18B4: oris r5, r24, 0x2000), where m-ex's passes Ft_MF_SkipAnim alone;
 *   - a start frame other than 0 ends in ftAction_8007349C (+0x1934), where m-ex's calls
 *     ftAction_80073354.
 * The rest is the same: the common state ThrownF with the animation step skipped, motion id
 * 400 + state, the animation cache key dropped, the animation, its flags and its script taken
 * from the hat file's ftcmd entry, the script timer 0, and the state's five callbacks. Every
 * caller passes speed 1.0, blend 0.0 and no argument, so those are not parameters here.
 *
 * The two native things of the layer's routine are kept: the kind bits of the animation flags
 * are set to Kirby's (see mu_ak_kirby_state_change), and where the console stops the game
 * (fighter not Kirby, no state table, no ftcmd table, no animation) one line is logged and
 * Kirby keeps his state, checked before anything is changed. */
static void ftKbDe_StateChange(Fighter_GObj* gobj, int state, MotionFlags flags, float anim_start)
{
    Fighter* fp = GET_FIGHTER(gobj);
    const MuAkFighter* ft;
    const MotionState* ms;
    MuAkKirbyCmd* table;
    MuAkKirbyCmd* cmd;
    static int logged;

    if (fp->kind != Ft_Kind_Kirby) {
        OSReport("[ak] Kirby (King Dedede) state change: fighter %d is not kirby\n", (int) fp->kind);
        return;
    }
    ft = mu_ak_fighter(fp->u.kb.hat.kind);
    if (ft == NULL || ft->kirby != &ftKbDe_Copy || state < 0 || state >= ftKbDe_State_Count) {
        OSReport("[ak] Kirby (King Dedede) state change: kind %d has no state %d of this ability\n",
                 (int) fp->u.kb.hat.kind, state);
        return;
    }
    table = mu_ak_kirby_cmd(fp->u.kb.hat.kind);
    if (table == NULL) {
        OSReport("[ak] Kirby (King Dedede) state change: kind %d has no ftcmd table\n",
                 (int) fp->u.kb.hat.kind);
        return;
    }
    ms = &ftKbDe_MotionStateTable[state];
    cmd = &table[ms->anim_id];
    if (DISC_NULL(cmd->anim)) {
        OSReport("[ak] Kirby (King Dedede) state change: state %d has no animation\n", state);
        return;
    }
    if (logged < FTKBDE_LOG * 2) {
        const char* name = DP(cmd->name);

        logged++;
        OSReport("[ak] Kirby (King Dedede) state %d: motion id %d, flags %08X, frame %.1f, "
                 "animation %d (%s)\n",
                 state, FTKBDE_MOTION_BASE + state, (unsigned) flags, anim_start,
                 (int) ms->anim_id, name != NULL ? name : "no name");
    }

    Fighter_ChangeMotionState(gobj, ftCo_MS_ThrownF, flags | Ft_MF_SkipAnim, anim_start, 1.0F,
                              0.0F, NULL);
    fp->anim_id = 0;
    fp->motion_id = FTKBDE_MOTION_BASE + state;
    fp->x5A4 = (uintptr_t) 0xFFFFFFFFu;

    fp->x590 = DP(cmd->anim);
    fp->x594_s32 = cmd->flags;
    fp->x597_bits = (u32) fp->kind;
    fp->x3E4_fighterCmdScript.u = DP(cmd->script);

    ftAnim_8006EBE8(gobj, anim_start, 1.0F, 0.0F);
    fp->x3E4_fighterCmdScript.timer = 0.0F;
    ftAnim_8006E9B4(gobj);
    if (anim_start == 0.0F) {
        ftCo_800C0408(gobj);
        ftAction_80073240(gobj);
    } else {
        ftAction_8007349C(gobj);
    }

    fp->anim_cb = ms->anim_cb;
    fp->input_cb = ms->input_cb;
    fp->phys_cb = ms->phys_cb;
    fp->coll_cb = ms->coll_cb;
    fp->cam_cb = ms->cam_cb;
}

/* ------------------------------------------------------------------------------------------ */
/* The exports                                                                                */
/* ------------------------------------------------------------------------------------------ */

/* [OnKirbySwallow] +0x000 (48 words): put the hat on (the retail hat attach without the
 * x2225_b2 write, nothing when Kirby already wears one). */
static void ftKbDe_OnSwallow(HSD_GObj* gobj)
{
    mu_ak_kirby_attach_hat(gobj);
}

/* [OnKirbyLoseAbility] +0x0C0 (23 words): take the hat off. Instruction for instruction the
 * retail ftKb_SpecialN_800EFAF0 (remove the hat joint, free its display list array). */
static void ftKbDe_OnLose(HSD_GObj* gobj)
{
    ftKb_SpecialN_800EFAF0(gobj);
}

/* [InitCopyItems] +0x124 (19 words): the hat file's two articles (hat data +0x10 and +0x14)
 * become Dedede's articles 4 and 5. */
static void ftKbDe_InitCopyItems(FighterKind kind, KirbyHatStruct* hat)
{
    mu_ak_register_article(kind, DISC_GET(Article, hat->hat_dynamics[ftKbDe_Hat_Article4]),
                           ftDe_Article_KbStarModel);
    mu_ak_register_article(kind, DISC_GET(Article, hat->hat_dynamics[ftKbDe_Hat_Article5]),
                           ftDe_Article_KbSpitStar);
}

/* ------------------------------------------------------------------------------------------ */
/* Shared pieces                                                                              */
/* ------------------------------------------------------------------------------------------ */

/* Arms the grab box (the same call in +0x4B0, +0x6BC, +0x1E18, +0x2970, +0x2A10, +0x2AC0): a
 * fighter in it calls SpecialN_OnVictim (+0x1AF0, ftDe_SpecialN_OnVictim), an item
 * SpecialN_OnItem, and the grab itself enters `grab_cb`. */
static void ftKbDe_SetGrabCallbacks(Fighter* fp, HSD_GObjEvent grab_cb)
{
    ftCommon_8007E2D0(fp, 16, grab_cb, ftKbDe_SpecialN_OnItem, ftDe_SpecialN_OnVictim);
    fp->x2225_b1 = true;
}

/* The three calls that end most entries: the state from frame 0, ftAnim_8006EBA4, the grab
 * box off. */
static void ftKbDe_ChangeAndReset(HSD_GObj* gobj, int state, MotionFlags flags)
{
    Fighter* fp = GET_FIGHTER(gobj);

    ftKbDe_StateChange(gobj, state, flags, 0.0F);
    ftAnim_8006EBA4(gobj);
    ftCommon_8007E2F4(fp, 0x1FF);
}

/* A state carried on in the air (the fall callbacks) or on the ground (the landing callbacks)
 * at the frame it is on. */
static void ftKbDe_Carry(HSD_GObj* gobj, int state, MotionFlags flags, bool ground, bool reset_grab)
{
    Fighter* fp = GET_FIGHTER(gobj);

    if (ground) {
        ftCommon_8007D7FC(fp);
    } else {
        ftCommon_8007D5D4(fp);
    }
    ftKbDe_StateChange(gobj, state, flags, fp->cur_anim_frame);
    if (reset_grab) {
        ftCommon_8007E2F4(fp, 0x1FF);
    }
}

/* The air states that check for a landing with the collision box held open
 * (+0x1234, +0x12E0, +0x138C, +0x13F0). */
static void ftKbDe_CollWithFullEcb(HSD_GObj* gobj, HSD_GObjEvent on_land)
{
    Fighter* fp = GET_FIGHTER(gobj);
    u32 saved = fp->coll_data.x130_flags;

    fp->coll_data.x130_flags = saved | 0x10;
    fp->coll_data.desired_ecb.bottom.y = 0.0F;
    ft_80082C74(gobj, on_land);
    fp->coll_data.x130_flags = saved;
}

static void ftKbDe_Empty(HSD_GObj* gobj) {}

/* ------------------------------------------------------------------------------------------ */
/* Entry, Start, Loop, End                                                                    */
/* ------------------------------------------------------------------------------------------ */

/* [SpecialN] +0x11C and [SpecialAirN] +0x120 both branch to [SpecialN_EnterAirOrGround] +0x4B0
 * (79 words). */
static void ftKbDe_SpecialN_Enter(HSD_GObj* gobj)
{
    Fighter* fp = GET_FIGHTER(gobj);
    ftDe_DatAttrs* da = ftKbDe_Attrs(fp);
    static int logged;

    fp->u.kb.xE0 = 4;
    fp->throw_flags = 0;
    fp->cmd_vars[0] = 0;
    fp->cmd_vars[1] = 0;
    fp->u.kb.xE4 = da->specialn_min_loop_frames;
    fp->u.kb.xE8 = FTKBDE_EATWALK_SLOW_LEN;
    fp->u.kb.xEC = FTKBDE_EATWALK_MIDDLE_LEN;
    fp->u.kb.xF0 = FTKBDE_EATWALK_FAST_LEN;
    if (logged < FTKBDE_LOG) {
        logged++;
        OSReport("[ak] Kirby (King Dedede) inhale: %s, at least %d frames of loop, mouth at "
                 "(%.1f, %.1f)\n",
                 fp->ground_or_air != GA_Ground ? "air" : "ground", (int) fp->u.kb.xE4,
                 da->specialn_mouth_x, da->specialn_mouth_y);
    }

    ftKbDe_StateChange(gobj,
                       fp->ground_or_air != GA_Ground ? ftKbDe_State_SpecialAirNStart
                                                      : ftKbDe_State_SpecialNStart,
                       Ft_MF_None, 0.0F);
    ftAnim_8006EBA4(gobj);
    ftKbDe_SetGrabCallbacks(fp, fp->ground_or_air != GA_Ground ? ftKbDe_SpecialAirNGrab_Enter
                                                               : ftKbDe_SpecialNGrab_Enter);
}

/* The first half of [state 0 Anim] +0x5EC and [state 16 Anim] +0x1164: the inhale effect, on
 * the frame the script sets cmd_vars[0]. */
static void ftKbDe_SpawnInhaleEffect(HSD_GObj* gobj)
{
    Fighter* fp = GET_FIGHTER(gobj);

    if (fp->cmd_vars[0] != 0) {
        efSync_Spawn(FTKBDE_EF_INHALE, gobj, fp->parts[FTKBDE_INHALE_PART].joint, &fp->facing_dir);
        fp->x2219_b0 = true;
        Fighter_SetEffectHitlagCallbacks(fp);
        fp->cmd_vars[0] = 0;
    }
}

/* [SpecialNLoop_Enter] +0x1E18 and [SpecialAirNLoop_Enter] +0x2970 (40 words each) */
static void ftKbDe_Loop_Enter(HSD_GObj* gobj, int state, HSD_GObjEvent grab_cb)
{
    Fighter* fp = GET_FIGHTER(gobj);

    ftKbDe_StateChange(gobj, state, 0x212, 0.0F);
    fp->x2222_b2 = true;
    Fighter_SetEffectHitlagCallbacks(fp);
    ftKbDe_SetGrabCallbacks(fp, grab_cb);
}

/* [state 0 Anim] +0x5EC (50 words) */
static void ftKbDe_SpecialNStart_Anim(HSD_GObj* gobj)
{
    ftKbDe_SpawnInhaleEffect(gobj);
    if (!ftAnim_IsFramesRemaining(gobj)) {
        ftKbDe_Loop_Enter(gobj, ftKbDe_State_SpecialNLoop, ftKbDe_SpecialNGrab_Enter);
    }
}

/* [state 16 Anim] +0x1164 (50 words) */
static void ftKbDe_SpecialAirNStart_Anim(HSD_GObj* gobj)
{
    ftKbDe_SpawnInhaleEffect(gobj);
    if (!ftAnim_IsFramesRemaining(gobj)) {
        ftKbDe_Loop_Enter(gobj, ftKbDe_State_SpecialAirNLoop, ftKbDe_SpecialAirNGrab_Enter);
    }
}

/* [state 0 Coll] +0x6BC (53 words), which is [state 1 Coll] +0x7E4 too (a branch to it): off
 * the ground both Start and Loop go on in SpecialAirNStart at the frame they are on, and the
 * grab box is armed again with the ground grab callback, as in Dedede's own code. */
static void ftKbDe_SpecialNStart_Coll(HSD_GObj* gobj)
{
    Fighter* fp = GET_FIGHTER(gobj);

    if (ft_80082708(gobj)) {
        return;
    }
    ftCommon_8007D5D4(fp);
    ftKbDe_StateChange(gobj, ftKbDe_State_SpecialAirNStart, FTKBDE_MF_CARRY_START, fp->cur_anim_frame);
    Fighter_SetEffectHitlagCallbacks(fp);
    ftKbDe_SetGrabCallbacks(fp, ftKbDe_SpecialNGrab_Enter);
}

/* [SpecialAirNStart_CollPassLedge] +0x2A10 (44 words) */
static void ftKbDe_SpecialAirNStart_Land(HSD_GObj* gobj)
{
    Fighter* fp = GET_FIGHTER(gobj);

    ftCommon_8007D7FC(fp);
    ftKbDe_StateChange(gobj, ftKbDe_State_SpecialNStart, FTKBDE_MF_CARRY_START, fp->cur_anim_frame);
    Fighter_SetEffectHitlagCallbacks(fp);
    ftKbDe_SetGrabCallbacks(fp, ftKbDe_SpecialNGrab_Enter);
}

/* [state 16 Coll] +0x1234 (22 words) */
static void ftKbDe_SpecialAirNStart_Coll(HSD_GObj* gobj)
{
    ftKbDe_CollWithFullEcb(gobj, ftKbDe_SpecialAirNStart_Land);
}

/* [state 1 IASA] +0x794 and [state 17 IASA] +0x1290 (19 words each): the loop runs at least
 * the frames of the hat's first value, then ends as soon as B is let go. */
static void ftKbDe_Loop_IASA(HSD_GObj* gobj, int end_state)
{
    Fighter* fp = GET_FIGHTER(gobj);

    if (fp->u.kb.xE4 != 0) {
        fp->u.kb.xE4--;
        return;
    }
    if ((fp->input.held_buttons[0] & 0x200) == 0) {
        ftKbDe_StateChange(gobj, end_state, Ft_MF_None, 0.0F);
    }
}

static void ftKbDe_SpecialNLoop_IASA(HSD_GObj* gobj)
{
    ftKbDe_Loop_IASA(gobj, ftKbDe_State_SpecialNEnd);
}

static void ftKbDe_SpecialAirNLoop_IASA(HSD_GObj* gobj)
{
    ftKbDe_Loop_IASA(gobj, ftKbDe_State_SpecialAirNEnd);
}

/* [SpecialAirNLoop_CollPassLedge] +0x2AC0 (44 words) */
static void ftKbDe_SpecialAirNLoop_Land(HSD_GObj* gobj)
{
    Fighter* fp = GET_FIGHTER(gobj);

    ftCommon_8007D7FC(fp);
    ftKbDe_StateChange(gobj, ftKbDe_State_SpecialNLoop, FTKBDE_MF_CARRY_LOOP_LAND, fp->cur_anim_frame);
    Fighter_SetEffectHitlagCallbacks(fp);
    ftKbDe_SetGrabCallbacks(fp, ftKbDe_SpecialNGrab_Enter);
}

/* [state 17 Coll] +0x12E0 (22 words) */
static void ftKbDe_SpecialAirNLoop_Coll(HSD_GObj* gobj)
{
    ftKbDe_CollWithFullEcb(gobj, ftKbDe_SpecialAirNLoop_Land);
}

/* [state 2 Coll] +0x83C (34 words) */
static void ftKbDe_SpecialNEnd_Coll(HSD_GObj* gobj)
{
    if (!ft_80082708(gobj)) {
        ftKbDe_Carry(gobj, ftKbDe_State_SpecialAirNEnd, FTKBDE_MF_CARRY_END, false, false);
    }
}

/* [SpecialAirNEnd_CollPassLedge] +0x2B70 (25 words) */
static void ftKbDe_SpecialAirNEnd_Land(HSD_GObj* gobj)
{
    ftKbDe_Carry(gobj, ftKbDe_State_SpecialNEnd, FTKBDE_MF_CARRY_END, true, false);
}

/* [state 18 Coll] +0x138C (22 words) */
static void ftKbDe_SpecialAirNEnd_Coll(HSD_GObj* gobj)
{
    ftKbDe_CollWithFullEcb(gobj, ftKbDe_SpecialAirNEnd_Land);
}

/* ------------------------------------------------------------------------------------------ */
/* Grab: pulling in                                                                           */
/* ------------------------------------------------------------------------------------------ */

/* [SpecialNGrab_Enter] +0x1DAC and [SpecialAirNGrab_Enter] +0x1D40 (27 words each): the grab
 * box touched a fighter. */
static void ftKbDe_Grab_Enter(HSD_GObj* gobj, int state)
{
    Fighter* fp = GET_FIGHTER(gobj);
    static int logged;

    ftKbDe_StateChange(gobj, state, 0x212, 0.0F);
    fp->x2222_b2 = true;
    fp->u.kb.xF4_b0 = false;
    ftCommon_8007E2F4(fp, 0x1FF);
    if (logged < FTKBDE_LOG) {
        logged++;
        OSReport("[ak] Kirby (King Dedede) inhale caught a fighter (state %d)\n", state);
    }
}

static void ftKbDe_SpecialNGrab_Enter(HSD_GObj* gobj)
{
    ftKbDe_Grab_Enter(gobj, ftKbDe_State_SpecialNGrab);
}

static void ftKbDe_SpecialAirNGrab_Enter(HSD_GObj* gobj)
{
    ftKbDe_Grab_Enter(gobj, ftKbDe_State_SpecialAirNGrab);
}

/* [SpecialN_OnItem] +0x1C9C (41 words): the grab box touched an item. AS_ItemGrabbed (+0x30F0)
 * is ftDe_SpecialN_ItemCaptured. */
static void ftKbDe_SpecialN_OnItem(HSD_GObj* gobj)
{
    Fighter* fp = GET_FIGHTER(gobj);
    static int logged;

    ftDe_SpecialN_ItemCaptured(fp->target_item_gobj, gobj, -fp->facing_dir);
    ftKbDe_StateChange(gobj,
                       fp->ground_or_air == GA_Air ? ftKbDe_State_SpecialAirNGrabItem
                                                   : ftKbDe_State_SpecialNGrabItem,
                       0x212, 0.0F);
    fp->x2222_b2 = true;
    fp->u.kb.xF4_b0 = true;
    ftCommon_8007E2F4(fp, 0x1FF);
    if (logged < FTKBDE_LOG) {
        logged++;
        OSReport("[ak] Kirby (King Dedede) inhale caught an item\n");
    }
}

/* The end of [state 3 Anim] and [state 4 Anim]: what was pulled in is close enough, Kirby eats
 * it. */
static void ftKbDe_EnterEat(HSD_GObj* gobj)
{
    Fighter* fp = GET_FIGHTER(gobj);

    ftKbDe_StateChange(gobj,
                       fp->ground_or_air == GA_Air ? ftKbDe_State_SpecialAirNEat
                                                   : ftKbDe_State_SpecialNEat,
                       0x10, 0.0F);
    ftCommon_8007E2F4(fp, 0x1FF);
}

/* [state 3 Anim] +0x8C4 (78 words), which is [state 19 Anim] +0x13E4 too (a branch to it). The
 * fighter in the mouth gets ASID_CaputureKirbyWait_InterruptCB (+0x1EB8) as its input
 * callback: ftDe_CaptureWaitKirby_IASA. */
static void ftKbDe_SpecialNGrab_Anim(HSD_GObj* gobj)
{
    Fighter* fp = GET_FIGHTER(gobj);
    float range = ftKbDe_Attrs(fp)->specialn_inhale_range;
    Vec3 mouth;

    ftDe_SpecialN_GetMouthPos(gobj, &mouth);
    if (ftCo_800BD19C(fp->victim_gobj, &mouth) < range * range) {
        ftCo_800BD620(fp->victim_gobj);
        GET_FIGHTER(fp->victim_gobj)->input_cb = ftDe_CaptureWaitKirby_IASA;
        ftKbDe_EnterEat(gobj);
    }
}

/* [state 4 Anim] +0xA10 (73 words), which is [state 20 Anim] +0x1448 too (a branch to it) */
static void ftKbDe_SpecialNGrabItem_Anim(HSD_GObj* gobj)
{
    Fighter* fp = GET_FIGHTER(gobj);
    float range = ftKbDe_Attrs(fp)->specialn_inhale_range;
    Vec3 mouth;

    ftDe_SpecialN_GetMouthPos(gobj, &mouth);
    if (it_802F23AC(fp->target_item_gobj, &mouth) < range * range) {
        it_802F2810(fp->target_item_gobj);
        ftKbDe_EnterEat(gobj);
    }
}

/* [SpecialNGrab_PassLedgeCB] +0x2124 (28 words) */
static void ftKbDe_SpecialNGrab_Fall(HSD_GObj* gobj)
{
    ftKbDe_Carry(gobj, ftKbDe_State_SpecialAirNGrab, FTKBDE_MF_CARRY, false, true);
}

/* [SpecialNGrabItem_PassLedgeCB] +0x2194 (28 words) */
static void ftKbDe_SpecialNGrabItem_Fall(HSD_GObj* gobj)
{
    ftKbDe_Carry(gobj, ftKbDe_State_SpecialAirNGrabItem, FTKBDE_MF_CARRY, false, true);
}

/* [SpecialAirNGrab_CollPassLedge] +0x2BD4 (28 words) */
static void ftKbDe_SpecialAirNGrab_Land(HSD_GObj* gobj)
{
    ftKbDe_Carry(gobj, ftKbDe_State_SpecialNGrab, FTKBDE_MF_CARRY, true, true);
}

/* [SpecialAirNGrabItem_CollPassLedge] +0x2C44 (28 words) */
static void ftKbDe_SpecialAirNGrabItem_Land(HSD_GObj* gobj)
{
    ftKbDe_Carry(gobj, ftKbDe_State_SpecialNGrabItem, FTKBDE_MF_CARRY, true, true);
}

/* [state 3 Coll] +0xA04 (3 words) */
static void ftKbDe_SpecialNGrab_Coll(HSD_GObj* gobj)
{
    ft_8008403C(gobj, ftKbDe_SpecialNGrab_Fall);
}

/* [state 4 Coll] +0xB3C (3 words) */
static void ftKbDe_SpecialNGrabItem_Coll(HSD_GObj* gobj)
{
    ft_8008403C(gobj, ftKbDe_SpecialNGrabItem_Fall);
}

/* [state 19 Coll] +0x13F0 (22 words) */
static void ftKbDe_SpecialAirNGrab_Coll(HSD_GObj* gobj)
{
    ftKbDe_CollWithFullEcb(gobj, ftKbDe_SpecialAirNGrab_Land);
}

/* [state 20 Coll] +0x1454 (3 words): the item grab does not hold the box open. */
static void ftKbDe_SpecialAirNGrabItem_Coll(HSD_GObj* gobj)
{
    ft_80082C74(gobj, ftKbDe_SpecialAirNGrabItem_Land);
}

/* ------------------------------------------------------------------------------------------ */
/* Eat, EatWait, EatTurn                                                                      */
/* ------------------------------------------------------------------------------------------ */

/* The end of [state 5 Anim], [state 7 Anim], [state 15 Anim] and of the walk's stop: standing
 * with a full mouth. */
static void ftKbDe_SpecialNEatWait_Enter(HSD_GObj* gobj)
{
    ftKbDe_ChangeAndReset(gobj, ftKbDe_State_SpecialNEatWait, 0x10);
}

/* [SpecialAirNEatWait_Enter] +0x280C (29 words): also the fall callback of the walk, the jump
 * squat and the landing. */
static void ftKbDe_SpecialAirNEatWait_Enter(HSD_GObj* gobj)
{
    Fighter* fp = GET_FIGHTER(gobj);

    ftCommon_8007D5D4(fp);
    ftKbDe_ChangeAndReset(gobj, ftKbDe_State_SpecialAirNEatWait, 0x90);
}

/* [SpecialNEatWait_CollPassLedge] +0x24A8 (28 words): walking off an edge with a full mouth.
 * [Dedede_CheckEnterSpecialAirN_EatWait] +0x38D4 (62 words) does the same to a Kirby on the
 * ground before it gives him the struggle's upward speed: that routine is Dedede_StruggleHop of
 * ftDe_SpecialNCapture.c, which calls this. */
void ftKbDe_EatWait_Fall(HSD_GObj* gobj)
{
    ftKbDe_Carry(gobj, ftKbDe_State_SpecialAirNEatWait, FTKBDE_MF_CARRY_EATWAIT, false, true);
}

/* [SpecialNEatLanding_Enter] +0x2914 (23 words) */
static void ftKbDe_SpecialNEatLanding_Enter(HSD_GObj* gobj)
{
    Fighter* fp = GET_FIGHTER(gobj);

    ftCommon_8007D7FC(fp);
    ftKbDe_StateChange(gobj, ftKbDe_State_SpecialNEatLanding, 0x12, 0.0F);
    ftAnim_8006EBA4(gobj);
}

/* [state 5 Anim] +0xB48 (35 words) */
static void ftKbDe_SpecialNEat_Anim(HSD_GObj* gobj)
{
    if (!ftAnim_IsFramesRemaining(gobj)) {
        static int logged;

        ftKbDe_SpecialNEatWait_Enter(gobj);
        if (logged < FTKBDE_LOG) {
            logged++;
            OSReport("[ak] Kirby (King Dedede) has a full mouth (ground)\n");
        }
    }
}

/* [state 21 Anim] +0x1460 (38 words) */
static void ftKbDe_SpecialAirNEat_Anim(HSD_GObj* gobj)
{
    Fighter* fp = GET_FIGHTER(gobj);

    if (!ftAnim_IsFramesRemaining(gobj)) {
        static int logged;

        ftCommon_8007D5D4(fp);
        ftKbDe_ChangeAndReset(gobj, ftKbDe_State_SpecialAirNEatWait, 0x10);
        if (logged < FTKBDE_LOG) {
            logged++;
            OSReport("[ak] Kirby (King Dedede) has a full mouth (air)\n");
        }
    }
}

/* [SpecialNEat_CollTransition] +0x2204 (28 words) */
static void ftKbDe_SpecialNEat_Fall(HSD_GObj* gobj)
{
    ftKbDe_Carry(gobj, ftKbDe_State_SpecialAirNEat, FTKBDE_MF_CARRY, false, true);
}

/* [SpecialAirNEat_CollTransition] +0x2CB4 (28 words) */
static void ftKbDe_SpecialAirNEat_Land(HSD_GObj* gobj)
{
    ftKbDe_Carry(gobj, ftKbDe_State_SpecialNEat, FTKBDE_MF_CARRY, true, true);
}

/* [state 5 Coll] +0xBDC (3 words) */
static void ftKbDe_SpecialNEat_Coll(HSD_GObj* gobj)
{
    ft_8008403C(gobj, ftKbDe_SpecialNEat_Fall);
}

/* [state 21 Coll] +0x1500 (3 words) */
static void ftKbDe_SpecialAirNEat_Coll(HSD_GObj* gobj)
{
    ft_80082C74(gobj, ftKbDe_SpecialAirNEat_Land);
}

/* [SpecialNEatWait_CheckSpit] +0x2274 (61 words): A or B spits what is in the mouth. Returns
 * true when it did. */
static bool ftKbDe_SpecialNEatWait_CheckSpit(HSD_GObj* gobj)
{
    Fighter* fp = GET_FIGHTER(gobj);
    bool air = fp->ground_or_air == GA_Air;
    int state;
    static int logged;

    if ((fp->input.pressed_buttons & 0x300) == 0) {
        return false;
    }
    if (!fp->u.kb.xF4_b0) {
        if (fp->victim_gobj == NULL) {
            return false;
        }
        state = air ? ftKbDe_State_SpecialAirNSpit : ftKbDe_State_SpecialNSpit;
    } else {
        if (fp->target_item_gobj == NULL) {
            return false;
        }
        state = air ? ftKbDe_State_SpecialAirNSpitItem : ftKbDe_State_SpecialNSpitItem;
    }
    ftKbDe_StateChange(gobj, state, 0x12, 0.0F);
    fp->x2222_b2 = true;
    ftAnim_8006EBA4(gobj);
    ftCommon_8007E2F4(fp, 0x1FF);
    if (logged < FTKBDE_LOG) {
        logged++;
        OSReport("[ak] Kirby (King Dedede) spits %s (state %d)\n",
                 fp->u.kb.xF4_b0 ? "an item" : "a fighter", state);
    }
    return true;
}

/* The stick test inside [state 6 IASA] and [state 22 IASA]: past the hat's turn value against
 * the facing direction. */
static bool ftKbDe_WantsTurn(Fighter* fp)
{
    float stick_x = fp->input.lstick[0].x;
    float abs_x = stick_x < 0.0F ? -stick_x : stick_x;

    if (!(abs_x >= ftKbDe_Attrs(fp)->specialn_turn_stick)) {
        return false;
    }
    return (stick_x < 0.0F && fp->facing_dir == 1.0F) || (stick_x > 0.0F && fp->facing_dir == -1.0F);
}

/* [SpecialNEatJump1_Enter] +0x2368 (32 words) */
static void ftKbDe_SpecialNEatJump1_Enter(HSD_GObj* gobj)
{
    Fighter* fp = GET_FIGHTER(gobj);
    ftDe_MotionVars* mv = ftDe_MV(fp);

    mv->specialn.x0 = 0;
    mv->specialn.jump_input = ftCo_Jump_GetInput(gobj);
    ftKbDe_ChangeAndReset(gobj, ftKbDe_State_SpecialNEatJump1, 0x92);
}

/* [state 6 IASA] +0xBEC (102 words) */
static void ftKbDe_SpecialNEatWait_IASA(HSD_GObj* gobj)
{
    Fighter* fp = GET_FIGHTER(gobj);

    if (ftKbDe_SpecialNEatWait_CheckSpit(gobj)) {
        return;
    }
    if (ftKbDe_WantsTurn(fp)) {
        ftKbDe_ChangeAndReset(gobj, ftKbDe_State_SpecialNEatTurn, 0x92);
        return;
    }
    if (ftCo_Jump_GetInput(gobj) != 0) {
        ftKbDe_SpecialNEatJump1_Enter(gobj);
        return;
    }
    if (ftWalkCommon_800DFC70(gobj)) {
        ftKbDe_SpecialN_ASWalk(gobj, 0.0F);
    }
}

/* [state 22 IASA] +0x1510 (70 words) */
static void ftKbDe_SpecialAirNEatWait_IASA(HSD_GObj* gobj)
{
    Fighter* fp = GET_FIGHTER(gobj);

    if (ftKbDe_SpecialNEatWait_CheckSpit(gobj)) {
        return;
    }
    if (ftKbDe_WantsTurn(fp)) {
        ftKbDe_ChangeAndReset(gobj, ftKbDe_State_SpecialAirNEatTurn, 0x92);
    }
}

/* [state 6 Coll] +0xD88 (3 words) */
static void ftKbDe_SpecialNEatWait_Coll(HSD_GObj* gobj)
{
    ft_8008403C(gobj, ftKbDe_EatWait_Fall);
}

/* [state 22 Coll] +0x162C and [state 14 Coll] +0x10B8 (3 words each) */
static void ftKbDe_SpecialAirNEatWait_Coll(HSD_GObj* gobj)
{
    ft_80082C74(gobj, ftKbDe_SpecialNEatLanding_Enter);
}

/* [state 7 Anim] +0xD94 (42 words) */
static void ftKbDe_SpecialNEatTurn_Anim(HSD_GObj* gobj)
{
    Fighter* fp = GET_FIGHTER(gobj);

    if (!ftAnim_IsFramesRemaining(gobj)) {
        fp->facing_dir = -fp->facing_dir;
        ftKbDe_SpecialNEatWait_Enter(gobj);
    }
}

/* [state 23 Anim] +0x1638 (44 words) */
static void ftKbDe_SpecialAirNEatTurn_Anim(HSD_GObj* gobj)
{
    Fighter* fp = GET_FIGHTER(gobj);

    if (!ftAnim_IsFramesRemaining(gobj)) {
        fp->facing_dir = -fp->facing_dir;
        ftKbDe_SpecialAirNEatWait_Enter(gobj);
    }
}

/* [SpecialAirNEatTurn_Trans] +0x2518 (25 words): falling during the turn (the grab box is not
 * turned off here, as in Dedede's own code). */
static void ftKbDe_SpecialNEatTurn_Fall(HSD_GObj* gobj)
{
    ftKbDe_Carry(gobj, ftKbDe_State_SpecialAirNEatTurn, FTKBDE_MF_CARRY, false, false);
}

/* [SpecialNEatTurn_Trans] +0x2D24 (28 words): landing during the turn in the air. */
static void ftKbDe_SpecialAirNEatTurn_Land(HSD_GObj* gobj)
{
    ftKbDe_Carry(gobj, ftKbDe_State_SpecialNEatTurn, FTKBDE_MF_CARRY, true, true);
}

/* [state 7 Coll] +0xE44 (3 words) */
static void ftKbDe_SpecialNEatTurn_Coll(HSD_GObj* gobj)
{
    ft_8008403C(gobj, ftKbDe_SpecialNEatTurn_Fall);
}

/* [state 23 Coll] +0x16F0 (3 words) */
static void ftKbDe_SpecialAirNEatTurn_Coll(HSD_GObj* gobj)
{
    ft_80082C74(gobj, ftKbDe_SpecialAirNEatTurn_Land);
}

/* ------------------------------------------------------------------------------------------ */
/* EatWalk, EatJump, EatLanding                                                               */
/* ------------------------------------------------------------------------------------------ */

/* [SpecialN_ASWalk] +0x23E8 (48 words): the walk with a full mouth, and the callback the
 * game's walk routine calls when the walk speed changes.
 *
 * The hat's code calls the game's walk entry (ftWalkCommon_800DFCA4, through the pointer at
 * +0x33AC) with motion state 10: that routine picks slow, middle or fast, enters common state
 * 10, 11 or 12 and fills the walk's motion variables. Then the code reads the motion id it has
 * just been given and enters Kirby's state of that number, which is EatWalkSlow, Middle or
 * Fast, at the same frame.
 *
 * What follows from it, on the console as here: Kirby's motion id is then 410 to 412 while the
 * walk's variables say 10, so the game's walk callbacks never find the speed they look for.
 * The interrupt callback therefore calls this routine again on every frame of the walk, and
 * the animation callback sets an animation rate from a value it never assigned, which the next
 * call of this routine replaces with 1.0 before it is used. */
static void ftKbDe_SpecialN_ASWalk(HSD_GObj* gobj, float anim_start)
{
    Fighter* fp = GET_FIGHTER(gobj);
    ftDe_DatAttrs* da = ftKbDe_Attrs(fp);

    ftWalkCommon_800DFCA4(gobj, FTKBDE_WALK_MSID, 0x10, anim_start, fp->u.kb.xE8, fp->u.kb.xEC,
                          fp->u.kb.xF0, fp->co_attrs.slow_walk_max, fp->co_attrs.mid_walk_point,
                          fp->co_attrs.fast_walk_min, da->specialn_walk_speed);
    ftKbDe_StateChange(gobj, (int) fp->motion_id, 0x10, anim_start);
    ftAnim_8006EBA4(gobj);
    ftCommon_8007E2F4(fp, 0x1FF);
}

/* [state 10, 11, 12 IASA] +0xF30 (53 words). The pointer at +0x2808 is ftWalkCommon_800DFEC8. */
static void ftKbDe_SpecialNEatWalk_IASA(HSD_GObj* gobj)
{
    if (ftCo_Jump_GetInput(gobj) != 0) {
        ftKbDe_SpecialNEatJump1_Enter(gobj);
    } else if (!ft_8008A1FC(gobj)) {
        ftWalkCommon_800DFEC8(gobj, ftKbDe_SpecialN_ASWalk);
    } else {
        ftKbDe_SpecialNEatWait_Enter(gobj);
    }
}

/* [state 10, 11, 12 Coll] +0x1014, [state 13 Coll] +0x1084, [state 15 Coll] +0x1158 (3 words
 * each) */
static void ftKbDe_SpecialNEatWalk_Coll(HSD_GObj* gobj)
{
    ft_8008403C(gobj, ftKbDe_SpecialAirNEatWait_Enter);
}

/* [SpecialNEatJump2_Enter] +0x2880 (37 words). The pointer at +0x371C is ftCo_800CB110. */
static void ftKbDe_SpecialNEatJump2_Enter(HSD_GObj* gobj)
{
    Fighter* fp = GET_FIGHTER(gobj);
    ftDe_DatAttrs* da = ftKbDe_Attrs(fp);

    ftCommon_8007D5D4(fp);
    ftKbDe_StateChange(gobj, ftKbDe_State_SpecialNEatJump2, 0x92, 0.0F);
    ftAnim_8006EBA4(gobj);
    ftCo_800CB110(gobj, true, da->specialn_jump_height);
}

/* [state 13 Anim] +0x1020 (20 words) */
static void ftKbDe_SpecialNEatJump1_Anim(HSD_GObj* gobj)
{
    if (!ftAnim_IsFramesRemaining(gobj)) {
        ftKbDe_SpecialNEatJump2_Enter(gobj);
    }
}

/* [state 14 IASA] +0x1094 (1 word): a branch to SpecialNEatWait_CheckSpit. */
static void ftKbDe_SpecialNEatJump2_IASA(HSD_GObj* gobj)
{
    ftKbDe_SpecialNEatWait_CheckSpit(gobj);
}

/* [state 15 Anim] +0x10C4 (35 words) */
static void ftKbDe_SpecialNEatLanding_Anim(HSD_GObj* gobj)
{
    if (!ftAnim_IsFramesRemaining(gobj)) {
        ftKbDe_SpecialNEatWait_Enter(gobj);
    }
}

/* ------------------------------------------------------------------------------------------ */
/* Spit                                                                                       */
/* ------------------------------------------------------------------------------------------ */

/* [SpecialNSpitItem_CheckToSpawnStarSpit] +0x26A0 (62 words): on the script's frame an eaten
 * item comes back out as a star, Dedede's article 5. ItemSpawn_StarSpit (+0x35E4) is
 * ftDe_SpecialN_SpawnSpitStarAs. */
static void ftKbDe_SpecialNSpitItem_Release(HSD_GObj* gobj)
{
    Fighter* fp = GET_FIGHTER(gobj);
    ftDe_DatAttrs* da;
    Vec3 pos, vel;

    if (fp->cmd_vars[0] == 0 || fp->target_item_gobj == NULL) {
        return;
    }
    da = ftKbDe_Attrs(fp);
    ftCommon_8007E2F4(fp, 0);
    lb_8000B1CC(fp->parts[ftParts_GetBoneIndex(fp, 52)].joint, NULL, &pos);
    vel.x = fp->facing_dir * da->star_speed;
    vel.y = 0.0F;
    vel.z = 0.0F;
    ftDe_SpecialN_SpawnSpitStarAs(gobj, ftDe_Article_KbSpitStar, &pos, &vel, FTKBDE_SPITSTAR_LIFETIME,
                                  FTKBDE_SPITSTAR_DECEL);
    it_802F28C8(fp->target_item_gobj, 0, 0.0F);
    fp->x1A64 = NULL;
    fp->target_item_gobj = NULL;
    fp->cmd_vars[0] = 0;
}

/* [state 9 Anim] +0xEB8 (21 words) */
static void ftKbDe_SpecialNSpitItem_Anim(HSD_GObj* gobj)
{
    ftKbDe_SpecialNSpitItem_Release(gobj);
    if (!ftAnim_IsFramesRemaining(gobj)) {
        ft_8008A2BC(gobj);
    }
}

/* [state 25 Anim] +0x1764 (21 words) */
static void ftKbDe_SpecialAirNSpitItem_Anim(HSD_GObj* gobj)
{
    ftKbDe_SpecialNSpitItem_Release(gobj);
    if (!ftAnim_IsFramesRemaining(gobj)) {
        ftCo_Fall_Enter(gobj);
    }
}

/* [SpecialNSpit_PassLedgeCB] +0x2630 (28 words) */
static void ftKbDe_SpecialNSpit_Fall(HSD_GObj* gobj)
{
    ftKbDe_Carry(gobj, ftKbDe_State_SpecialAirNSpit, FTKBDE_MF_CARRY, false, true);
}

/* [SpecialNSpitItem_PassLedgeCB] +0x2798 (28 words) */
static void ftKbDe_SpecialNSpitItem_Fall(HSD_GObj* gobj)
{
    ftKbDe_Carry(gobj, ftKbDe_State_SpecialAirNSpitItem, FTKBDE_MF_CARRY, false, true);
}

/* [SpecialAirNSpit_PassLedgeCB] +0x2D94 (28 words) */
static void ftKbDe_SpecialAirNSpit_Land(HSD_GObj* gobj)
{
    ftKbDe_Carry(gobj, ftKbDe_State_SpecialNSpit, FTKBDE_MF_CARRY, true, true);
}

/* [SpecialAirNSpitItem_PassLedgeCB] +0x2E04 (28 words): the landing names state 25, the item
 * spit's own air state (+0x2E44: li r4, 0x19), so Kirby stays in it on the ground. The same
 * slip is in Dedede's own code. Kept as found. */
static void ftKbDe_SpecialAirNSpitItem_Land(HSD_GObj* gobj)
{
    ftKbDe_Carry(gobj, ftKbDe_State_SpecialAirNSpitItem, FTKBDE_MF_CARRY, true, true);
}

/* [state 8 Coll] +0xEAC (3 words) */
static void ftKbDe_SpecialNSpit_Coll(HSD_GObj* gobj)
{
    ft_8008403C(gobj, ftKbDe_SpecialNSpit_Fall);
}

/* [state 9 Coll] +0xF14 (3 words) */
static void ftKbDe_SpecialNSpitItem_Coll(HSD_GObj* gobj)
{
    ft_8008403C(gobj, ftKbDe_SpecialNSpitItem_Fall);
}

/* [state 24 Coll] +0x1758 (3 words) */
static void ftKbDe_SpecialAirNSpit_Coll(HSD_GObj* gobj)
{
    ft_80082C74(gobj, ftKbDe_SpecialAirNSpit_Land);
}

/* [state 25 Coll] +0x17C0 (3 words) */
static void ftKbDe_SpecialAirNSpitItem_Coll(HSD_GObj* gobj)
{
    ft_80082C74(gobj, ftKbDe_SpecialAirNSpitItem_Land);
}

/* ------------------------------------------------------------------------------------------ */
/* The tables                                                                                 */
/* ------------------------------------------------------------------------------------------ */

/* [move_logic] +0x170 (26 entries of 0x20 bytes). Words +4 and +8 are written as on the disc;
 * the hat's state change does not read them. The first number is the entry of the hat file's
 * ftcmd table (17 animations: the ground and the air state of a pair share one, and the two
 * Grab states play the Loop's).
 *
 * The callbacks that hold nothing of Kirby's are the game's functions or Dedede's own:
 *   every empty one (IASA of most states, Anim of Loop, EatWait, EatJump2, air Grab);
 *   every Phys: ft_80084F3C on the ground, ft_80084EEC in the air, except
 *     [state 10, 11, 12 Phys] +0x1004: ftWalkCommon_800E0060 (a jump through a pointer), and
 *     [state 14 Phys] +0x1098: Dedede's own (the first frame of the rise skips ft_80084DB0);
 *   [state 2 Anim] +0x7E8 and [state 18 Anim] +0x1338: Dedede's own (the end goes to Wait or
 *     Fall);
 *   [state 8 Anim] +0xE50 and [state 24 Anim] +0x16FC: Dedede's own. On the script's frame the
 *     held fighter becomes a star (SpecialNSpit_CheckToSpawnStarSpit +0x257C, the same
 *     instructions as Dedede's; AS_EnterStarSpitState +0x33B0 is ftDe_SpecialN_EnterStarSpit);
 *   [state 10, 11, 12 Anim] +0xF20: ftWalkCommon_800DFDDC (a jump through a pointer);
 *   [state 13 IASA] +0x1070: ftCo_KneeBend_Check_ShortHop (a jump through a pointer). */
#define FTKBDE_STATE(anim, flags, move, a, i, p, c)                                              \
    {                                                                                            \
        (anim), (flags), (move), (a), (i), (p), (c), ftCamera_UpdateCameraBox,                   \
    }

static const MotionState ftKbDe_MotionStateTable[ftKbDe_State_Count] = {
    /*  0 */ FTKBDE_STATE(0, 0x00340111, 0x12000000, ftKbDe_SpecialNStart_Anim, ftKbDe_Empty,
                          ft_80084F3C, ftKbDe_SpecialNStart_Coll),
    /*  1 */ FTKBDE_STATE(1, 0x003C0011, 0x12000000, ftKbDe_Empty, ftKbDe_SpecialNLoop_IASA,
                          ft_80084F3C, ftKbDe_SpecialNStart_Coll),
    /*  2 */ FTKBDE_STATE(2, 0x00340011, 0x12000000, ftDe_SpecialNEnd_Anim, ftKbDe_Empty,
                          ft_80084F3C, ftKbDe_SpecialNEnd_Coll),
    /*  3 */ FTKBDE_STATE(3, 0x00340011, 0x12000000, ftKbDe_SpecialNGrab_Anim, ftKbDe_Empty,
                          ft_80084F3C, ftKbDe_SpecialNGrab_Coll),
    /*  4 */ FTKBDE_STATE(3, 0x00340011, 0x12000000, ftKbDe_SpecialNGrabItem_Anim, ftKbDe_Empty,
                          ft_80084F3C, ftKbDe_SpecialNGrabItem_Coll),
    /*  5 */ FTKBDE_STATE(4, 0x00340011, 0x12000000, ftKbDe_SpecialNEat_Anim, ftKbDe_Empty,
                          ft_80084F3C, ftKbDe_SpecialNEat_Coll),
    /*  6 */ FTKBDE_STATE(5, 0x00340011, 0x12400000, ftKbDe_Empty, ftKbDe_SpecialNEatWait_IASA,
                          ft_80084F3C, ftKbDe_SpecialNEatWait_Coll),
    /*  7 */ FTKBDE_STATE(12, 0x00340011, 0x12000000, ftKbDe_SpecialNEatTurn_Anim, ftKbDe_Empty,
                          ft_80084F3C, ftKbDe_SpecialNEatTurn_Coll),
    /*  8 */ FTKBDE_STATE(13, 0x00340011, 0x12000000, ftDe_SpecialNSpit_Anim, ftKbDe_Empty,
                          ft_80084F3C, ftKbDe_SpecialNSpit_Coll),
    /*  9 */ FTKBDE_STATE(13, 0x00340011, 0x12000000, ftKbDe_SpecialNSpitItem_Anim, ftKbDe_Empty,
                          ft_80084F3C, ftKbDe_SpecialNSpitItem_Coll),
    /* 10 */ FTKBDE_STATE(6, 0x00344011, 0x12000000, ftWalkCommon_800DFDDC,
                          ftKbDe_SpecialNEatWalk_IASA, ftWalkCommon_800E0060,
                          ftKbDe_SpecialNEatWalk_Coll),
    /* 11 */ FTKBDE_STATE(7, 0x00344011, 0x12000000, ftWalkCommon_800DFDDC,
                          ftKbDe_SpecialNEatWalk_IASA, ftWalkCommon_800E0060,
                          ftKbDe_SpecialNEatWalk_Coll),
    /* 12 */ FTKBDE_STATE(8, 0x00344011, 0x12000000, ftWalkCommon_800DFDDC,
                          ftKbDe_SpecialNEatWalk_IASA, ftWalkCommon_800E0060,
                          ftKbDe_SpecialNEatWalk_Coll),
    /* 13 */ FTKBDE_STATE(9, 0x00348011, 0x12000000, ftKbDe_SpecialNEatJump1_Anim,
                          ftCo_KneeBend_Check_ShortHop, ft_80084F3C, ftKbDe_SpecialNEatWalk_Coll),
    /* 14 */ FTKBDE_STATE(10, 0x00340011, 0x12000000, ftKbDe_Empty, ftKbDe_SpecialNEatJump2_IASA,
                          ftDe_SpecialNEatJump2_Phys, ftKbDe_SpecialAirNEatWait_Coll),
    /* 15 */ FTKBDE_STATE(11, 0x00340011, 0x12400000, ftKbDe_SpecialNEatLanding_Anim, ftKbDe_Empty,
                          ft_80084F3C, ftKbDe_SpecialNEatWalk_Coll),
    /* 16 */ FTKBDE_STATE(0, 0x00340411, 0x12000000, ftKbDe_SpecialAirNStart_Anim, ftKbDe_Empty,
                          ft_80084EEC, ftKbDe_SpecialAirNStart_Coll),
    /* 17 */ FTKBDE_STATE(1, 0x00340411, 0x12000000, ftKbDe_Empty, ftKbDe_SpecialAirNLoop_IASA,
                          ft_80084EEC, ftKbDe_SpecialAirNLoop_Coll),
    /* 18 */ FTKBDE_STATE(2, 0x00340411, 0x12000000, ftDe_SpecialAirNEnd_Anim, ftKbDe_Empty,
                          ft_80084EEC, ftKbDe_SpecialAirNEnd_Coll),
    /* 19 */ FTKBDE_STATE(3, 0x00340411, 0x12000000, ftKbDe_SpecialNGrab_Anim, ftKbDe_Empty,
                          ft_80084EEC, ftKbDe_SpecialAirNGrab_Coll),
    /* 20 */ FTKBDE_STATE(3, 0x00340411, 0x12000000, ftKbDe_SpecialNGrabItem_Anim, ftKbDe_Empty,
                          ft_80084EEC, ftKbDe_SpecialAirNGrabItem_Coll),
    /* 21 */ FTKBDE_STATE(4, 0x00340411, 0x12000000, ftKbDe_SpecialAirNEat_Anim, ftKbDe_Empty,
                          ft_80084EEC, ftKbDe_SpecialAirNEat_Coll),
    /* 22 */ FTKBDE_STATE(5, 0x00340411, 0x12400000, ftKbDe_Empty, ftKbDe_SpecialAirNEatWait_IASA,
                          ft_80084EEC, ftKbDe_SpecialAirNEatWait_Coll),
    /* 23 */ FTKBDE_STATE(12, 0x00340411, 0x12000000, ftKbDe_SpecialAirNEatTurn_Anim, ftKbDe_Empty,
                          ft_80084EEC, ftKbDe_SpecialAirNEatTurn_Coll),
    /* 24 */ FTKBDE_STATE(13, 0x00340411, 0x12000000, ftDe_SpecialAirNSpit_Anim, ftKbDe_Empty,
                          ft_80084EEC, ftKbDe_SpecialAirNSpit_Coll),
    /* 25 */ FTKBDE_STATE(13, 0x00340411, 0x12000000, ftKbDe_SpecialAirNSpitItem_Anim, ftKbDe_Empty,
                          ft_80084EEC, ftKbDe_SpecialAirNSpitItem_Coll),
};

/* The kbFunction exports of PlKbCpDe.dat (0, 1, 2, 3, 5 and 6: it has no OnKirbyHurt and no
 * export 7). */
const MuAkKirbyCopy ftKbDe_Copy = {
    .on_swallow = ftKbDe_OnSwallow,
    .on_lose = ftKbDe_OnLose,
    .special_n = ftKbDe_SpecialN_Enter,
    .special_air_n = ftKbDe_SpecialN_Enter,
    .on_hurt = NULL,
    .init_items = ftKbDe_InitCopyItems,
    .move_logic = ftKbDe_MotionStateTable,
    .move_logic_count = ftKbDe_State_Count,
    .on_frame = NULL,
};
