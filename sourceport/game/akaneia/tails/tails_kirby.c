/* Akaneia's Tails: the ability Kirby copies from him (m-ex "kbFunction" of PlKbCpTs.dat).
 *
 * Kirby's Tails ability is the energy shot, state for state Tails's own neutral special
 * (ftTs_specialn.c) with these things changed:
 *   - the two states are entered through the hat file's own state change routine (+0x38C, a
 *     copy of m-ex's KirbyStateChange compiled into the file, which keeps the caller's flags):
 *     ftKbTs_StateChange. The animation and script come from the hat file, motion ids 400 and
 *     401;
 *   - the five values of the move are read from the hat data (the word at +0xC points at a copy
 *     of the start of Tails's attribute block), not from Kirby's attributes;
 *   - the shot is Tails's article 1 (item kind 285 on Akaneia), whose data is in the hat file
 *     and whose code is article 0's again (itTs_shot.c);
 *   - the "one shot at a time" slot is not a field of the fighter: the mod's code keeps it at
 *     console fp+0x596C, past the end of Kirby's Fighter. Here it is ftKbTs_ShotSlot
 *     (ftTs_types.h, console fp+0x2270);
 *   - the hat (the two tails) is animated: while the move runs its dynamic bones are released
 *     and it plays the animation at hat data +0x18, frame for frame with Kirby's own; when the
 *     move ends, by any way out, the hat's joints get their modelled scale back and the two
 *     dynamic bones are set up again. The script can ask for the helicopter effect (5005 of
 *     Tails's effect file) on the hat's joint 2.
 *
 * Where the hat's code is the same instructions as Tails's own, Tails's function is used and
 * the state table says so.
 *
 * The hat file has no debug symbols: the names are Tails's own for the routine in the same
 * place, and each function gives its offset in the hat file's code block
 * (run-source/rel09-b1-wolf/kirby/listings/PlKbCpTs.listing.txt shows it at 0x81800000 plus the
 * offset). The code has no fixed console address: m-ex loads it with the hat file.
 *
 * Nothing below keeps a kind or changes a static during a match (the `logged` counters only
 * limit the log): the slot lives in Kirby's fighter variables, the two script flags in
 * cmd_vars, the article is registered under the kind Kirby copied and looked up through it
 * (mu_ak_article_kind). */
#include "ftTs.h"

#include <dolphin/os.h>

#include <melee/ef/eflib.h>
#include <melee/ef/efsync.h>
#include <melee/ft/fighter.h>
#include <melee/ft/ft_081B.h>
#include <melee/ft/ftaction.h>
#include <melee/ft/ftanim.h>
#include <melee/ft/ftcamera.h>
#include <melee/ft/ftcolanim.h>
#include <melee/ft/ftcommon.h>
#include <melee/ft/ftdynamics.h>
#include <melee/ft/inlines.h>
#include <melee/ft/kinds/ftCommon/forward.h>
#include <melee/ft/kinds/ftCommon/ftCo_Landing.h>
#include <melee/ft/kinds/ftKirby/ftkirby.h>
#include <melee/ft/types.h>
#include <melee/it/item.h>
#include <melee/lb/lb_00F9.h>
#include <melee/lb/lbspdisplay.h>
#include <melee/lb/types.h>
#include <sysdolphin/baselib/gobj.h>
#include <sysdolphin/baselib/jobj.h>

#include <melee/it/types.h>

#include "../common/mu_ak_kirby.h"
#include "../mu_ak_fighter.h"

/* common/mu_ak_kirby.c: the ftcmd table the scene loaded for a fighter kind (m-ex keeps it per
 * kind at r2 + 0x128), NULL when there is none. Declared here as well as in mu_ak_kirby.h so
 * that this file does not depend on the order the two are edited in. */
MuAkKirbyCmd* mu_ak_kirby_cmd(FighterKind kind);

/* Kirby's states of this ability (state numbers of the hat's state change; motion ids 400 and
 * 401), in the order of Tails's own 341 and 342. */
enum {
    ftKbTs_State_SpecialN,    /* 0 */
    ftKbTs_State_SpecialAirN, /* 1 */
    ftKbTs_State_Count
};

/* The motion id of state n (+0x468: addi r8, r24, 0x190), as m-ex's own routine gives. */
#define FTKBTS_MOTION_BASE 400

/* Tails's article that is Kirby's (index into Tails's MxDt item list). */
#define ftKbTs_Article_Shot 1

/* The words of the hat data after the KirbyHatStruct header (hat_dynamics[n] is +0xC + 4n). */
enum {
    ftKbTs_Hat_Attrs,    /* +0xC: the values of the move */
    ftKbTs_Hat_Dynamics, /* +0x10: the hat's dynamic bones (2 in the file) */
    ftKbTs_Hat_Article,  /* +0x14: the shot's article data */
    ftKbTs_Hat_Anim,     /* +0x18: the hat's animation during the move */
};

/* The joint of the hat the helicopter effect sits on (+0xCE0: li r5, 2). */
#define FTKBTS_HAT_EFFECT_JOINT 2

/* The landing lag of a shot fired in the air, +0x388 (10.0). */
#define FTKBTS_LANDING_LAG 10.0F

#define FTKBTS_LOG 8 /* evidence lines of each kind that go in the log, per run */

static const MotionState ftKbTs_MotionStateTable[ftKbTs_State_Count];

static void ftKbTs_SpecialN_Accessory(HSD_GObj* gobj);
static void ftKbTs_SpecialN_OnExit(HSD_GObj* gobj);

/* MEX_GetKirbyCpData(fp->kb.hat.kind): NULL when Kirby no longer holds a hat of an added kind
 * (not an m-ex case: there the routine then reads the table of whatever kind he holds). */
static inline KirbyHatStruct* ftKbTs_Hat(Fighter* fp)
{
    return mu_ak_kirby_hat(fp->u.kb.hat.kind);
}

/* The word at hat data +0xC: 0.04, 0.1, 0.8, 1.0, 0.77 and the landing box (10, 0, -5, 0, 5, 0)
 * in the file, the first six fields of Tails's own attribute block in the same order. Only
 * +0x00 to +0x2B are read. */
static inline ftTails_DatAttrs* ftKbTs_DA(KirbyHatStruct* hat)
{
    return DISC_GET(ftTails_DatAttrs, hat->hat_dynamics[ftKbTs_Hat_Attrs]);
}

/* What every entry into a state installs (+0x6D0 to +0x6DC, +0x810 to +0x820, +0x990 to
 * +0x99C): the shot watcher, and the hat's reset on a hit, on a death and at the next state
 * change. */
static inline void ftKbTs_SetCallbacks(Fighter* fp)
{
    fp->accessory4_cb = ftKbTs_SpecialN_Accessory;
    fp->death2_cb = ftKbTs_SpecialN_OnExit;
    fp->take_dmg_cb = ftKbTs_SpecialN_OnExit;
    fp->x21EC = ftKbTs_SpecialN_OnExit;
}

/* ------------------------------------------------------------------------------------------ */
/* The hat file's own state change                                                            */
/* ------------------------------------------------------------------------------------------ */

/* +0x38C (186 words): the state change the hat file carries (the asserts name it
 * "kirbystatechange", file "m-ex"). Next to m-ex's routine at 803D7080, which the layer's
 * mu_ak_kirby_state_change follows, it differs in two things:
 *   - the caller's flags are used: Fighter_ChangeMotionState gets flags | Ft_MF_SkipAnim
 *     (+0x44C: oris r5, r25, 0x2000), where m-ex's passes Ft_MF_SkipAnim alone. The two
 *     changes between ground and air pass 0x4008 (keep the script variables, keep the
 *     hitboxes);
 *   - a start frame other than 0 ends in ftAction_8007349C (+0x4CC), where m-ex's calls
 *     ftAction_80073354.
 * The rest is the same: the common state ThrownF with the animation step skipped, motion id
 * 400 + state, the animation cache key dropped, the animation, its flags and its script taken
 * from the hat file's ftcmd entry, the script timer 0, and the state's five callbacks.
 *
 * The two native things of the layer's routine are kept: the kind bits of the animation flags
 * are set to Kirby's (see mu_ak_kirby_state_change), and where the console stops the game
 * (fighter not Kirby, no state table, no ftcmd table, no animation) one line is logged and
 * Kirby keeps his state, checked before anything is changed. */
static void ftKbTs_StateChange(Fighter_GObj* gobj, int state, MotionFlags flags, float anim_start,
                               float anim_speed, float anim_blend)
{
    Fighter* fp = GET_FIGHTER(gobj);
    const MuAkFighter* ft;
    const MotionState* ms;
    MuAkKirbyCmd* table;
    MuAkKirbyCmd* cmd;
    static int logged;

    if (fp->kind != Ft_Kind_Kirby) {
        OSReport("[ak] Kirby (Tails) state change: fighter %d is not kirby\n", (int) fp->kind);
        return;
    }
    ft = mu_ak_fighter(fp->u.kb.hat.kind);
    if (ft == NULL || ft->kirby != &ftKbTs_Copy || state < 0 || state >= ftKbTs_State_Count) {
        OSReport("[ak] Kirby (Tails) state change: kind %d has no state %d of this ability\n",
                 (int) fp->u.kb.hat.kind, state);
        return;
    }
    table = mu_ak_kirby_cmd(fp->u.kb.hat.kind);
    if (table == NULL) {
        OSReport("[ak] Kirby (Tails) state change: kind %d has no ftcmd table\n",
                 (int) fp->u.kb.hat.kind);
        return;
    }
    ms = &ftKbTs_MotionStateTable[state];
    cmd = &table[ms->anim_id];
    if (DISC_NULL(cmd->anim)) {
        OSReport("[ak] Kirby (Tails) state change: state %d has no animation\n", state);
        return;
    }
    if (logged < FTKBTS_LOG) {
        const char* name = DP(cmd->name);

        logged++;
        OSReport("[ak] Kirby (Tails) state %d: motion id %d, flags %08X, frame %.1f, "
                 "animation %d (%s)\n",
                 state, FTKBTS_MOTION_BASE + state, (unsigned) flags, anim_start,
                 (int) ms->anim_id, name != NULL ? name : "no name");
    }

    Fighter_ChangeMotionState(gobj, ftCo_MS_ThrownF, flags | Ft_MF_SkipAnim, anim_start,
                              anim_speed, anim_blend, NULL);
    fp->anim_id = 0;
    fp->motion_id = FTKBTS_MOTION_BASE + state;
    fp->x5A4 = (uintptr_t) 0xFFFFFFFFu;

    fp->x590 = DP(cmd->anim);
    fp->x594_s32 = cmd->flags;
    fp->x597_bits = (u32) fp->kind;
    fp->x3E4_fighterCmdScript.u = DP(cmd->script);

    ftAnim_8006EBE8(gobj, anim_start, anim_speed, anim_blend);
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
/* The hat                                                                                    */
/* ------------------------------------------------------------------------------------------ */

/* +0x274 (67 words): the hat's dynamic bones, from the ftDynamics at hat data +0x10. The same
 * routine as the one in Sonic's hat file (+0x264 there). The console stops with "fighter
 * dynamics num over!" (an assert in kb_tails.c, line 241) when the file asks for more than the
 * fighter has room for; here Kirby then gets none. Called with the hat on. */
static void ftKbTs_SetupHatDynamics(Fighter* fp, KirbyHatStruct* hat)
{
    ftDynamics* dynamics = DP(hat->hat_dynamics[ftKbTs_Hat_Dynamics]);
    int i;
    static int logged;

    fp->dynamics_num = dynamics->dynamicsNum;
    if (fp->dynamics_num >= Ft_Dynamics_NumMax) {
        OSReport("[ak] Kirby (Tails): fighter dynamics num over! (%d); the hat gets none\n",
                 fp->dynamics_num);
        fp->dynamics_num = 0;
        return;
    }
    for (i = 0; i < fp->dynamics_num; i++) {
        BoneDynamicsDescDisc* bone = &DP(dynamics->ftDynamicBones)->array[i];
        HSD_JObj* jobj = NULL;

        lb_80011E24(fp->u.kb.hat.jobj, &jobj, (int) bone->bone_id, -1);
        lb_8000FD48(jobj, &fp->dynamic_bone_sets[i].dyn_desc, bone->dyn_desc.count);
        fp->dynamic_bone_sets[i].bone_id = 0;
        lb_80011710(&bone->dyn_desc, &fp->dynamic_bone_sets[i].dyn_desc);
    }
    if (logged < FTKBTS_LOG) {
        logged++;
        OSReport("[ak] Kirby (Tails) hat: %d dynamic bones\n", fp->dynamics_num);
    }
}

/* +0xED0 (148 words, the compiler's unrolling of a walk over a joint and everything beside and
 * below it): every joint's scale is set back to the scale of its description, the model's own.
 * A joint made by HSD_JObjLoadJoint keeps its description in its id field (console jobj+0x84). */
static void ftKbTs_ResetHatScale(HSD_JObj* jobj)
{
    for (; jobj != NULL; jobj = jobj->next) {
        HSD_Joint* desc = (HSD_Joint*) jobj->id;

        if (desc != NULL) {
            jobj->scale.x = desc->scale.x;
            jobj->scale.y = desc->scale.y;
            jobj->scale.z = desc->scale.z;
        }
        ftKbTs_ResetHatScale(jobj->child);
    }
}

/* +0xBB4 (47 words): the move is over (Kirby was hit, died, or changed state). The hat's
 * animation is taken off, its joints get their own scale back, and the dynamic bones are set
 * up again. Nothing without a hat, and nothing while Kirby's motion id is 0xEF: that is what
 * Fighter_ChangeMotionState has just written when it calls this from inside the hat's own
 * state change (the change between ground and air), so the move goes on. (A Kirby thrown into
 * the real state 0xEF out of the move keeps the hat as it is, as on the console.) */
static void ftKbTs_SpecialN_OnExit(HSD_GObj* gobj)
{
    Fighter* fp = GET_FIGHTER(gobj);
    KirbyHatStruct* hat;

    if (fp->u.kb.hat.jobj == NULL || fp->motion_id == ftCo_MS_ThrownF) {
        return;
    }
    HSD_JObjRemoveAnimAll(fp->u.kb.hat.jobj);
    ftKbTs_ResetHatScale(fp->u.kb.hat.jobj);
    HSD_JObjAnimAll(fp->u.kb.hat.jobj);
    hat = ftKbTs_Hat(fp);
    /* Not an m-ex case: Kirby wears a hat of another kind by now. */
    if (hat == NULL || DISC_NULL(hat->hat_dynamics[ftKbTs_Hat_Dynamics])) {
        return;
    }
    ftKbTs_SetupHatDynamics(fp, hat);
}

/* +0xC70 (48 words): the hat follows the move. Its animation is put on Kirby's own frame and
 * applied; when the script has raised cmd_vars[3], the helicopter effect is put on the hat's
 * joint 2, and from then on a hit or a death removes Kirby's effects in place of resetting the
 * hat (the reset at the next state change stays). */
static void ftKbTs_SpecialN_HatThink(HSD_GObj* gobj)
{
    Fighter* fp = GET_FIGHTER(gobj);
    HSD_JObj* jobj = NULL;
    static int logged;

    if (fp->u.kb.hat.jobj == NULL) {
        return;
    }
    HSD_JObjReqAnimAll(fp->u.kb.hat.jobj, fp->cur_anim_frame);
    HSD_JObjAnimAll(fp->u.kb.hat.jobj);
    if (fp->cmd_vars[3] == 0) {
        return;
    }
    lb_80011E24(fp->u.kb.hat.jobj, &jobj, FTKBTS_HAT_EFFECT_JOINT, -1);
    efSync_Spawn(ftTs_Ef_HeliSpecialN, gobj, jobj);
    fp->cmd_vars[3] = 0;
    fp->death2_cb = efLib_DestroyAll;
    fp->take_dmg_cb = efLib_DestroyAll;
    if (logged < FTKBTS_LOG) {
        logged++;
        OSReport("[ak] Kirby (Tails) helicopter effect %d on hat joint %d (%s)\n",
                 ftTs_Ef_HeliSpecialN, FTKBTS_HAT_EFFECT_JOINT,
                 jobj != NULL ? "found" : "NOT found");
    }
}

/* ------------------------------------------------------------------------------------------ */
/* The exports                                                                                */
/* ------------------------------------------------------------------------------------------ */

/* [OnKirbySwallow] +0x000 (55 words): nothing at all when Kirby already wears a hat. Else put
 * the hat on (the retail hat attach without the x2225_b2 write), clear the shot slot (+0xAC
 * stores the register that held the null hat joint) and set up the hat's dynamic bones. */
static void ftKbTs_OnSwallow(HSD_GObj* gobj)
{
    Fighter* fp = GET_FIGHTER(gobj);
    KirbyHatStruct* hat;

    if (fp->u.kb.hat.jobj != NULL) {
        return;
    }
    mu_ak_kirby_attach_hat(gobj);
    *ftKbTs_ShotSlot(fp) = NULL;
    hat = ftKbTs_Hat(fp);
    /* Not an m-ex case: no hat data, or the attach made no joint. */
    if (hat == NULL || fp->u.kb.hat.jobj == NULL ||
        DISC_NULL(hat->hat_dynamics[ftKbTs_Hat_Dynamics]))
    {
        return;
    }
    ftKbTs_SetupHatDynamics(fp, hat);
}

/* [OnKirbyLoseAbility] +0x0DC (30 words): the live shot goes with the ability (Item_8026A8EC,
 * then the slot is cleared), then the hat comes off (the retail ftKb_SpecialN_800EFAF0,
 * instruction for instruction), then, hat or not, the dynamic bones are released. */
static void ftKbTs_OnLose(HSD_GObj* gobj)
{
    Fighter* fp = GET_FIGHTER(gobj);
    Item_GObj** slot = ftKbTs_ShotSlot(fp);
    static int logged;

    if (*slot != NULL) {
        if (logged < FTKBTS_LOG) {
            logged++;
            OSReport("[ak] Kirby (Tails) lost the ability with a shot alive: shot removed\n");
        }
        Item_8026A8EC(*slot);
        *slot = NULL;
    }
    ftKb_SpecialN_800EFAF0(gobj);
    ftCo_UnloadDynamicBones(fp);
}

/* [OnKirbyHurt] +0x224 (1 word): empty. */
static void ftKbTs_OnHurt(HSD_GObj* gobj) {}

/* [InitCopyItems] +0x228 (3 words): the hat file's article (hat data +0x14) becomes Tails's
 * article 1. */
static void ftKbTs_InitCopyItems(FighterKind kind, KirbyHatStruct* hat)
{
    mu_ak_register_article(kind, DISC_GET(Article, hat->hat_dynamics[ftKbTs_Hat_Article]),
                           ftKbTs_Article_Shot);
}

/* ------------------------------------------------------------------------------------------ */
/* The move                                                                                   */
/* ------------------------------------------------------------------------------------------ */

/* +0xD30 (104 words) is Tails's SpecialN_SpawnProjectile with article 1 and the slot at console
 * fp+0x596C: ftTs_SpecialN_SpawnProjectileWith.
 *
 * +0xB14 (40 words), Tails's SpecialN_Accessory: fires when the script raises cmd_vars[0], and
 * a shot fired in the air sets Kirby's vertical speed. Then, every frame, the hat (+0xC70). */
static void ftKbTs_SpecialN_Accessory(HSD_GObj* gobj)
{
    Fighter* fp = GET_FIGHTER(gobj);
    KirbyHatStruct* hat = ftKbTs_Hat(fp);

    if (fp->cmd_vars[0] == 1) {
        Item_GObj** slot = ftKbTs_ShotSlot(fp);
        static int logged;

        fp->cmd_vars[0] = 0;
        ftTs_SpecialN_SpawnProjectileWith(gobj, ftKbTs_Article_Shot, (HSD_GObj**) slot);
        if (logged < FTKBTS_LOG) {
            logged++;
            OSReport("[ak] Kirby (Tails) energy shot: article %d is item kind %d, %s (%s)\n",
                     ftKbTs_Article_Shot, (int) mu_ak_article_kind(gobj, ftKbTs_Article_Shot),
                     *slot != NULL ? "created" : "NOT created",
                     fp->ground_or_air == GA_Air ? "air" : "ground");
        }
        /* hat NULL is not an m-ex case. */
        if (fp->ground_or_air == GA_Air && hat != NULL) {
            fp->self_vel.y = ftKbTs_DA(hat)->airn_shot_vel_y;
        }
    }
    ftKbTs_SpecialN_HatThink(gobj);
}

/* +0x674 (49 words), Tails's SpecialN_OnEnter: the script flags are cleared, Kirby's vertical
 * speed is scaled, the callbacks go in; then the hat starts its animation and lets go of its
 * dynamic bones. */
static void ftKbTs_SpecialN_OnEnter(HSD_GObj* gobj)
{
    Fighter* fp = GET_FIGHTER(gobj);
    KirbyHatStruct* hat = ftKbTs_Hat(fp);

    /* hat NULL is not an m-ex case. */
    if (hat != NULL) {
        fp->self_vel.y *= ftKbTs_DA(hat)->n_enter_vel_y_mul;
    }
    fp->cmd_vars[0] = 0;
    fp->cmd_vars[1] = 0;
    ftKbTs_SetCallbacks(fp);
    if (fp->u.kb.hat.jobj != NULL && hat != NULL) {
        HSD_JObjAddAnimAll(fp->u.kb.hat.jobj,
                           DISC_GET(HSD_AnimJoint, hat->hat_dynamics[ftKbTs_Hat_Anim]), NULL,
                           NULL);
        ftCo_UnloadDynamicBones(fp);
    }
}

/* [SpecialN] +0x154 and [SpecialAirN] +0x1BC (26 words each), Tails's SpecialN_Enter: nothing
 * while the last shot is alive. */
static void ftKbTs_Enter(HSD_GObj* gobj, int state)
{
    Fighter* fp = GET_FIGHTER(gobj);

    if (*ftKbTs_ShotSlot(fp) != NULL) {
        static int logged;

        if (logged < FTKBTS_LOG) {
            logged++;
            OSReport("[ak] Kirby (Tails) energy shot refused: the last shot is still alive\n");
        }
        return;
    }
    ftKbTs_StateChange(gobj, state, Ft_MF_None, 0.0F, 1.0F, 0.0F);
    ftAnim_8006EBA4(gobj);
    ftKbTs_SpecialN_OnEnter(gobj);
}

static void ftKbTs_SpecialN_Enter(HSD_GObj* gobj)
{
    ftKbTs_Enter(gobj, ftKbTs_State_SpecialN);
}

static void ftKbTs_SpecialAirN_Enter(HSD_GObj* gobj)
{
    ftKbTs_Enter(gobj, ftKbTs_State_SpecialAirN);
}

/* [state 0 Coll] +0x78C (42 words), Tails's SpecialN_Coll with SpecialAirN_Trans inside it: off
 * the ground the move goes on in the air, on the same frame. */
static void ftKbTs_SpecialN_Coll(HSD_GObj* gobj)
{
    Fighter* fp = GET_FIGHTER(gobj);

    if (ft_80082708(gobj)) {
        return;
    }
    ftCommon_8007D5D4(fp);
    ftKbTs_StateChange(gobj, ftKbTs_State_SpecialAirN, ftTs_MF_TransN, fp->cur_anim_frame, 1.0F,
                       0.0F);
    ftKbTs_SetCallbacks(fp);
}

/* [state 1 Phys] +0x884 (21 words), Tails's SpecialAirN_Phys with the hat's values. */
static void ftKbTs_SpecialAirN_Phys(HSD_GObj* gobj)
{
    Fighter* fp = GET_FIGHTER(gobj);
    KirbyHatStruct* hat = ftKbTs_Hat(fp);
    ftTails_DatAttrs* da;

    /* Not an m-ex case. */
    if (hat == NULL) {
        return;
    }
    da = ftKbTs_DA(hat);
    ftCommon_CalcSelfAccel_Deaccel(fp, da->airn_x_decel);
    ftCommon_Fall(fp, da->airn_gravity, da->airn_terminal_vel);
}

/* [state 1 Coll] +0x8D8 (56 words), Tails's SpecialAirN_Coll with SpecialN_Trans inside it: a
 * landing after the shot left is the special landing lag, before it the move goes on on the
 * ground. */
static void ftKbTs_SpecialAirN_Coll(HSD_GObj* gobj)
{
    Fighter* fp = GET_FIGHTER(gobj);
    KirbyHatStruct* hat = ftKbTs_Hat(fp);
    ftCollisionBox ecb;

    /* Not an m-ex case. */
    if (hat == NULL) {
        return;
    }
    ecb = ftCollisionBox_FromDisc(&ftKbTs_DA(hat)->airn_ecb);
    if (!ft_800824A0(gobj, &ecb)) {
        return;
    }
    if (fp->cmd_vars[1] != 0) {
        ftCo_LandingFallSpecial_Enter(gobj, false, FTKBTS_LANDING_LAG);
        return;
    }
    ftCommon_8007D7FC(fp);
    ftKbTs_StateChange(gobj, ftKbTs_State_SpecialN, ftTs_MF_TransN, fp->cur_anim_frame, 1.0F,
                       0.0F);
    ftKbTs_SetCallbacks(fp);
}

/* ------------------------------------------------------------------------------------------ */
/* The tables                                                                                 */
/* ------------------------------------------------------------------------------------------ */

/* [move_logic] +0x234 (two entries of 0x20 bytes). Words +4 and +8 are Tails's own (0x00340111
 * and move id 0x12); the hat's state change does not read them. The animation is the entry of
 * the hat file's ftcmd table with the state's own number.
 *
 * The callbacks that are the same instructions as Tails's own are his functions:
 *   [state 0 Anim] +0x738: the end of the animation goes to Wait (ft_8008A2BC);
 *   [state 1 Anim] +0x834: the end of the animation goes to Fall;
 *   [state 0 IASA] +0x784 and [state 1 IASA] +0x880: empty;
 *   [state 0 Phys] +0x788: ft_80084F3C. */
static const MotionState ftKbTs_MotionStateTable[ftKbTs_State_Count] = {
    {
        /* state 0 */
        0,
        0x00340111,
        0x12 << 24,
        ftTs_SpecialN_Anim,
        ftTs_SpecialN_IASA,
        ftTs_SpecialN_Phys,
        ftKbTs_SpecialN_Coll,
        ftCamera_UpdateCameraBox,
    },
    {
        /* state 1 */
        1,
        0x00340111,
        0x12 << 24,
        ftTs_SpecialAirN_Anim,
        ftTs_SpecialAirN_IASA,
        ftKbTs_SpecialAirN_Phys,
        ftKbTs_SpecialAirN_Coll,
        ftCamera_UpdateCameraBox,
    },
};

/* The kbFunction exports of PlKbCpTs.dat (0 to 6; it has no export 7). */
const MuAkKirbyCopy ftKbTs_Copy = {
    .on_swallow = ftKbTs_OnSwallow,
    .on_lose = ftKbTs_OnLose,
    .special_n = ftKbTs_SpecialN_Enter,
    .special_air_n = ftKbTs_SpecialAirN_Enter,
    .on_hurt = ftKbTs_OnHurt,
    .init_items = ftKbTs_InitCopyItems,
    .move_logic = ftKbTs_MotionStateTable,
    .move_logic_count = ftKbTs_State_Count,
    .on_frame = NULL,
};
