/* Akaneia's Sonic: the ability Kirby copies from him (m-ex "kbFunction" of PlKbCpSn.dat).
 *
 * Kirby's Sonic ability is the homing attack, state for state Sonic's own neutral special
 * (sonic_specialn.c) with these things changed:
 *   - every state change goes through KirbyStateChange (the animation and script come from the
 *     hat file, nine states, motion ids 400 to 408);
 *   - every value of the move is read from the hat data, not from Kirby's attributes: the word
 *     at hat data +0xC points at a copy of Sonic's attribute block (the same layout,
 *     ftSonic_DatAttrs, and on this disc the same values);
 *   - the trail's position and angle are kept in Kirby's fighter variables (console fp+2270 to
 *     fp+227C) where Sonic keeps them in his own;
 *   - the spin ball is another effect of Sonic's file (5002 on Kirby's hip joint, where Sonic
 *     has 5000 on his TransN joint), and it is given Kirby's own body textures, so the ball has
 *     the color of the Kirby that curls up;
 *   - the trail is always blue, where Sonic's takes the color of his costume;
 *   - the hat has six dynamic bones (the quills), set up by the swallow from the word at hat
 *     data +0x10 and released by the loss.
 * The ability has no articles: the hat file has no itFunction and no InitCopyItems export.
 *
 * Where the hat's code is the same instructions as Sonic's own, Sonic's function is used and the
 * state table says so; where it only differs by the state change or by where a value comes
 * from, it is written again below, on Sonic's helpers where they take the values as arguments
 * (ftSn_SpecialN_SearchTarget, ftSn_SpecialN_ClampReboundVel, ftSn_SpawnTrailEffectAt).
 *
 * The hat file has no debug symbols: the names are Sonic's own for the routine in the same
 * place, and each function gives its offset in the hat file's code block
 * (run-source/rel09-b1-wolf/kirby/listings/PlKbCpSn.listing.txt shows it at 0x81800000 plus the
 * offset). The code has no fixed console address: m-ex loads it with the hat file.
 *
 * Nothing below keeps a kind or changes a static during a match (the `logged` counters only
 * limit the log): the charge count, the timer, the angle and the target live in Kirby's motion
 * variables (console fp+2340 on, as in Sonic's own states), the trail in his fighter variables. */
#include "sonic.h"

#include <math.h>

#include <dolphin/os.h>

#include <melee/ef/eflib.h>
#include <melee/ef/efsync.h>
#include <melee/ef/types.h>
#include <melee/ft/fighter.h>
#include <melee/ft/ft_081B.h>
#include <melee/ft/ft_0892.h>
#include <melee/ft/ftanim.h>
#include <melee/ft/ftcamera.h>
#include <melee/ft/ftcliffcommon.h>
#include <melee/ft/ftcommon.h>
#include <melee/ft/ftdynamics.h>
#include <melee/ft/inlines.h>
#include <melee/ft/kinds/ftCommon/ftCo_Fall.h>
#include <melee/ft/kinds/ftCommon/types.h>
#include <melee/ft/kinds/ftKirby/ftkirby.h>
#include <melee/ft/types.h>
#include <melee/lb/lb_00B0.h>
#include <melee/lb/lb_00F9.h>
#include <melee/lb/lbspdisplay.h>
#include <melee/lb/types.h>
#include <melee/mp/forward.h>
#include <sysdolphin/baselib/controller.h>
#include <sysdolphin/baselib/dobj.h>
#include <sysdolphin/baselib/gobj.h>
#include <sysdolphin/baselib/jobj.h>
#include <sysdolphin/baselib/mobj.h>
#include <sysdolphin/baselib/tobj.h>

#include "../common/mu_ak_kirby.h"

/* Kirby's states of this ability (KirbyStateChange state numbers; motion ids 400 to 408), in
 * the order of Sonic's own 341 to 349. */
enum {
    ftKbSn_State_SpecialNStart,      /* 0: windup, ground animation */
    ftKbSn_State_SpecialAirNStart,   /* 1: windup, air animation */
    ftKbSn_State_SpecialNCharge,     /* 2: curled up, counting */
    ftKbSn_State_SpecialNAttackMiss, /* 3: no target, dives forward and down */
    ftKbSn_State_SpecialNAttack,     /* 4: homing dash */
    ftKbSn_State_SpecialNCancel,     /* 5: the dash ended without a hit */
    ftKbSn_State_SpecialNLanding,    /* 6 */
    ftKbSn_State_SpecialNRebound,    /* 7: bounced off a wall, the floor or a shield */
    ftKbSn_State_SpecialNHit,        /* 8: bounce after hitting the target */
    ftKbSn_State_Count
};

/* The spin ball Kirby gets: effect 2 of Sonic's effect file (0x138A), where Sonic's own spin
 * ball is effect 0. */
#define ftKbSn_Ef_Spin 5002

/* The trail's material color, +0x19B0 (the word 0000FFFF): blue. */
static const GXColor ftKbSn_TrailColor = { 0x00, 0x00, 0xFF, 0xFF };

#define FTKBSN_LOG 8 /* evidence lines of each kind that go in the log, per run */

/* What the ability keeps for Kirby between frames, at console fp+2270: the first words of his
 * texture list for the retail "parts" hats (fp->u.kb.x44), not read while a hat of this kind is
 * worn. The layout is the one of Sonic's own trail fields. */
typedef struct ftKbSn_FighterVars {
    /* 2270 */ Vec3 trail_pos; /* bone 2 world position last trail frame */
    /* 227C */ float trail_angle;
} ftKbSn_FighterVars;

_Static_assert(sizeof(ftKbSn_FighterVars) <= sizeof(CostumeTObjList),
               "Kirby's Sonic trail fields must fit the hat texture list they overlay");

static inline ftKbSn_FighterVars* ftKbSn_FV(Fighter* fp)
{
    return (ftKbSn_FighterVars*) &fp->u.kb.x44;
}

/* MEX_GetKirbyCpData(fp->kb.hat.kind) and the word at +0xC of what it returns: the values of the
 * move. NULL when Kirby no longer holds a hat of an added kind (not an m-ex case: there the
 * routine then reads the table of whatever kind he holds); the callers do nothing then. */
static ftSonic_DatAttrs* ftKbSn_DA(Fighter* fp)
{
    KirbyHatStruct* hat = mu_ak_kirby_hat(fp->u.kb.hat.kind);

    if (hat == NULL) {
        return NULL;
    }
    return DISC_GET(ftSonic_DatAttrs, hat->hat_dynamics[0]);
}

static void ftKbSn_SpecialNCharge_Enter(HSD_GObj* gobj);
static void ftKbSn_SearchTarget_EnterAttack(HSD_GObj* gobj);
static void ftKbSn_SpecialNAttackMiss_Enter(HSD_GObj* gobj);
static void ftKbSn_SpecialNAttack_Enter(HSD_GObj* gobj, float target_x, float target_y);
static void ftKbSn_SpecialNCancel_Enter(HSD_GObj* gobj);
static void ftKbSn_SpecialNLanding_Enter(HSD_GObj* gobj);
static void ftKbSn_SpecialNRebound_Enter(HSD_GObj* gobj, u32 env_flags);
static void ftKbSn_SpecialNRebound_OnEnter(HSD_GObj* gobj);
static void ftKbSn_SpecialNHit_Enter(HSD_GObj* gobj);
static void ftKbSn_GFXSpin(HSD_GObj* gobj);
static void ftKbSn_GFXSpinAndTrail(HSD_GObj* gobj);
static void ftKbSn_GFXSpinAndTrailVelocityDirection(HSD_GObj* gobj);

/* ---- the hat ------------------------------------------------------------------------------- */

/* +0x264 (67 words): the hat's dynamic bones, from the ftDynamics at hat data +0x10. The retail
 * hats with dynamic bones do the same from their own hat data (ftCo_8009D074 and its twins in
 * ft/ftdynamics.c); this one finds each bone with lb_80011E24 where those walk the joint tree.
 * The console stops with "fighter dynamics num over!" (an assert in kb_sonic.c, line 48) when
 * the file asks for more than the fighter has room for; here Kirby then gets none. */
static void ftKbSn_SetupHatDynamics(Fighter* fp, KirbyHatStruct* hat)
{
    ftDynamics* dynamics = DP(hat->hat_dynamics[1]);
    int i;
    static int logged;

    fp->dynamics_num = dynamics->dynamicsNum;
    if (fp->dynamics_num >= Ft_Dynamics_NumMax) {
        OSReport("[ak] Kirby (Sonic): fighter dynamics num over! (%d); the hat gets none\n",
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
    if (logged < FTKBSN_LOG) {
        logged++;
        OSReport("[ak] Kirby (Sonic) hat: %d dynamic bones\n", fp->dynamics_num);
    }
}

/* [OnKirbySwallow] +0x000 (51 words): put the hat on (the retail hat attach without the
 * x2225_b2 write) and set up its dynamic bones. Nothing at all when Kirby already wears one. */
static void ftKbSn_OnSwallow(HSD_GObj* gobj)
{
    Fighter* fp = GET_FIGHTER(gobj);
    KirbyHatStruct* hat;

    if (fp->u.kb.hat.jobj != NULL) {
        return;
    }
    mu_ak_kirby_attach_hat(gobj);
    hat = mu_ak_kirby_hat(fp->u.kb.hat.kind);
    /* Not an m-ex case: no hat data, or the attach made no joint. */
    if (hat == NULL || fp->u.kb.hat.jobj == NULL || DISC_NULL(hat->hat_dynamics[1])) {
        return;
    }
    ftKbSn_SetupHatDynamics(fp, hat);
}

/* [OnKirbyLoseAbility] +0x0CC (27 words): take the hat off (the retail
 * ftKb_SpecialN_800EFAF0, instruction for instruction), then, hat or not, release the dynamic
 * bones: a call through the pointer at +0x370, which is ftCo_UnloadDynamicBones (8009E0D4). */
static void ftKbSn_OnLose(HSD_GObj* gobj)
{
    Fighter* fp = GET_FIGHTER(gobj);

    ftKb_SpecialN_800EFAF0(gobj);
    ftCo_UnloadDynamicBones(fp);
}

/* [OnKirbyHurt] +0x140 (1 word): empty. */
static void ftKbSn_OnHurt(HSD_GObj* gobj) {}

/* ---- effects ------------------------------------------------------------------------------- */

/* +0x1280 (93 words): the spin ball, painted like Kirby.
 *
 * The effect's model has two textured display objects under its second joint (root, child,
 * child). The first gets the image and the palette of the first display object of Kirby's own
 * model (his body, so the costume's color). The second gets the image of Kirby's eighth display
 * object when that one has no palette. The pointers are written into the effect model's own
 * image and palette records, as the console code does.
 *
 * The console walks to the eighth display object with MexTK's JOBJ_GetDObjChild, which stops the
 * game with "dobj not found!" (inline.h, line 472) when the list is shorter, and reads every
 * record without a check. Here a missing record ends the painting and the ball keeps the
 * textures it was modeled with. */
static void ftKbSn_SpawnSpin(HSD_GObj* gobj)
{
    Fighter* fp = GET_FIGHTER(gobj);
    EF_Effect* effect = efSync_Spawn(ftKbSn_Ef_Spin, gobj, fp->parts[FtPart_HipN].joint);
    HSD_JObj* body;
    HSD_JObj* ball;
    HSD_DObj* first;
    HSD_DObj* eighth;
    HSD_DObj* ball_dobj;
    int i;

    if (effect == NULL) {
        return;
    }
    body = GET_JOBJ(gobj);
    ball = effect->gobj->hsd_obj;
    if (body == NULL || ball == NULL || ball->child == NULL || ball->child->child == NULL) {
        return;
    }
    first = body->u.dobj;
    ball_dobj = ball->child->child->u.dobj;
    if (first == NULL || ball_dobj == NULL) {
        return;
    }

    eighth = first;
    for (i = 7; i > 0; i--) {
        if (eighth->next == NULL) {
            OSReport("[ak] Kirby (Sonic) spin ball: dobj not found! (Kirby's model has no eighth "
                     "display object)\n");
            return;
        }
        eighth = eighth->next;
    }

    if (first->mobj != NULL && first->mobj->tobj != NULL) {
        HSD_TObj* src = first->mobj->tobj;

        if (src->imagedesc != NULL && src->tlut != NULL && ball_dobj->mobj != NULL &&
            ball_dobj->mobj->tobj != NULL)
        {
            HSD_TObj* dst = ball_dobj->mobj->tobj;

            if (dst->imagedesc != NULL && dst->tlut != NULL) {
                dst->imagedesc->image_ptr = src->imagedesc->image_ptr;
                dst->tlut->lut = src->tlut->lut;
            }
        }
    }

    if (eighth->mobj != NULL && eighth->mobj->tobj != NULL) {
        HSD_TObj* src = eighth->mobj->tobj;

        if (src->tlut == NULL && src->imagedesc != NULL && ball_dobj->next != NULL &&
            ball_dobj->next->mobj != NULL && ball_dobj->next->mobj->tobj != NULL)
        {
            HSD_TObj* dst = ball_dobj->next->mobj->tobj;

            if (dst->imagedesc != NULL) {
                dst->imagedesc->image_ptr = src->imagedesc->image_ptr;
            }
        }
    }
}

/* +0x1078 (30 words), Sonic_GFXSpin: the spin ball, once per move (x2219_b0 marks it spawned).
 * Installed as accessory4_cb and removes itself. Unlike Sonic's own it does not touch a bone's
 * scale. */
static void ftKbSn_GFXSpin(HSD_GObj* gobj)
{
    Fighter* fp = GET_FIGHTER(gobj);

    if (!fp->x2219_b0) {
        ftKbSn_SpawnSpin(gobj);
        fp->x2219_b0 = true;
        fp->pre_hitlag_cb = efLib_PauseAll;
        fp->post_hitlag_cb = efLib_ResumeAll;
    }
    fp->accessory4_cb = NULL;
}

/* +0x16FC (23 words), Sonic_UpdateTrailPosAndRot: remember where bone 2 is now and which way
 * Kirby is moving. */
static void ftKbSn_UpdateTrailPosAndRot(HSD_GObj* gobj)
{
    Fighter* fp = GET_FIGHTER(gobj);
    ftKbSn_FighterVars* fv = ftKbSn_FV(fp);

    lb_8000B1CC(fp->parts[FtPart_XRotN].joint, NULL, &fv->trail_pos);
    fv->trail_angle =
        atan2f(fp->cur_pos.y - fp->prev_pos.y, fp->cur_pos.x - fp->prev_pos.x);
}

/* +0x1758 (6 words), Sonic_GFXTrailLoop, and +0x1770 (142 words), SpawnTrailEffect: Sonic's
 * routine over Kirby's fields, with the fixed color. */
static void ftKbSn_GFXTrailLoop(HSD_GObj* gobj)
{
    Fighter* fp = GET_FIGHTER(gobj);
    ftKbSn_FighterVars* fv = ftKbSn_FV(fp);

    ftSn_SpawnTrailEffectAt(gobj, fp->cmd_vars[3] == 1, &fv->trail_pos, &fv->trail_angle,
                            &ftKbSn_TrailColor);
}

/* Sonic_GFXTrail, which the hat's code has inside its two callers: start a trail and hand over
 * to the per-frame loop. The motion id test is Sonic's (his up smash, 0x3F); Kirby's is 400 and
 * up in these states, so the trail is always switched on. */
static void ftKbSn_GFXTrail(HSD_GObj* gobj)
{
    Fighter* fp = GET_FIGHTER(gobj);

    ftKbSn_UpdateTrailPosAndRot(gobj);
    fp->accessory4_cb = ftKbSn_GFXTrailLoop;
    if (fp->motion_id != ftCo_MS_AttackHi4) {
        fp->cmd_vars[3] = 1;
    }
}

/* +0x1598 (43 words), Sonic_GFXSpinAndTrail: the miss dive's accessory4_cb. */
static void ftKbSn_GFXSpinAndTrail(HSD_GObj* gobj)
{
    ftKbSn_GFXSpin(gobj);
    ftKbSn_GFXTrail(gobj);
}

/* +0x13F4 (48 words), Sonic_GFXSpinAndTrailVelocityDirection: the homing dash's accessory4_cb;
 * as above, with the trail angle taken from the velocity. */
static void ftKbSn_GFXSpinAndTrailVelocityDirection(HSD_GObj* gobj)
{
    Fighter* fp;

    ftKbSn_GFXSpin(gobj);
    ftKbSn_GFXTrail(gobj);
    fp = GET_FIGHTER(gobj);
    ftKbSn_FV(fp)->trail_angle = atan2f(fp->self_vel.y, fp->self_vel.x);
}

/* ---- entry ---------------------------------------------------------------------------------- */

/* [SpecialN] +0x138 and [SpecialAirN] +0x13C are both a branch to +0x374 (60 words),
 * SpecialNStart_Enter: KirbyStateChange(gobj, 0 on the ground or 1 in the air, 0, NULL, 0.0,
 * 1.0, 0.0). From the ground Kirby is put in the air first. */
static void ftKbSn_SpecialNStart_Enter(HSD_GObj* gobj)
{
    Fighter* fp = GET_FIGHTER(gobj);

    if (fp->ground_or_air == GA_Air) {
        mu_ak_kirby_state_change(gobj, ftKbSn_State_SpecialAirNStart, Ft_MF_None, 0.0F, 1.0F,
                                 0.0F);
    } else {
        ftCommon_8007D5D4(fp);
        mu_ak_kirby_state_change(gobj, ftKbSn_State_SpecialNStart, Ft_MF_None, 0.0F, 1.0F, 0.0F);
    }
    ftAnim_8006EBA4(gobj);
    fp->self_vel.x = 0.0F;
    fp->self_vel.y = 0.0F;
    ftSn_MV(fp)->specialn.charge_frames = 0;
}

/* ---- Start ---------------------------------------------------------------------------------- */

/* [state 0 Anim, 1 Anim] +0x464 (19 words) */
static void ftKbSn_SpecialNStart_Anim(HSD_GObj* gobj)
{
    if (!ftAnim_IsFramesRemaining(gobj)) {
        ftKbSn_SpecialNCharge_Enter(gobj);
    }
}

/* ---- Charge --------------------------------------------------------------------------------- */

/* +0xC80 (32 words), SpecialNCharge_Enter */
static void ftKbSn_SpecialNCharge_Enter(HSD_GObj* gobj)
{
    Fighter* fp = GET_FIGHTER(gobj);
    ftSonic_DatAttrs* da = ftKbSn_DA(fp);

    if (da == NULL) {
        return;
    }
    mu_ak_kirby_state_change(gobj, ftKbSn_State_SpecialNCharge, Ft_MF_None, 0.0F,
                             da->specialn_charge_anim_rate, 0.0F);
    fp->self_vel.y = da->specialn_charge_vel_y;
    fp->accessory4_cb = ftKbSn_GFXSpin;
}

/* [state 2 Anim] +0x4BC (35 words) */
static void ftKbSn_SpecialNCharge_Anim(HSD_GObj* gobj)
{
    Fighter* fp = GET_FIGHTER(gobj);
    ftSonic_DatAttrs* da = ftKbSn_DA(fp);
    ftSonic_SpecialNVars* mv = &ftSn_MV(fp)->specialn;

    if (da == NULL) {
        return;
    }
    if (mv->charge_frames >= da->specialn_max_charge_frames ||
        (mv->charge_frames >= da->specialn_min_charge_frames &&
         (fp->input.pressed_buttons & HSD_PAD_B)))
    {
        ftKbSn_SearchTarget_EnterAttack(gobj);
        return;
    }
    mv->charge_frames++;
}

/* [state 2 Coll] +0x550 (24 words). The collision box is the first six words of the values
 * (the console hands ft_800824A0 the address of the block itself). */
static void ftKbSn_SpecialNCharge_Coll(HSD_GObj* gobj)
{
    Fighter* fp = GET_FIGHTER(gobj);
    ftSonic_DatAttrs* da = ftKbSn_DA(fp);
    ftCollisionBox box;

    if (da == NULL) {
        return;
    }
    box = ftCollisionBox_FromDisc(&da->coll_box);
    ft_800824A0(gobj, &box);
    if (fp->coll_data.env_flags & Collide_CeilingMask) {
        fp->self_vel.y = 0.0F;
    }
}

/* ---- target search -------------------------------------------------------------------------- */

/* +0xD00 (160 words), SpecialN_SearchTarget_EnterAttack: Sonic's search with the range and the
 * offset of the hat data; then the dash, its aim point nudged by the control stick, or the
 * dive. */
static void ftKbSn_SearchTarget_EnterAttack(HSD_GObj* gobj)
{
    Fighter* fp = GET_FIGHTER(gobj);
    ftSonic_DatAttrs* da = ftKbSn_DA(fp);
    float target_x;
    float target_y;
    static int logged;

    if (da == NULL) {
        return;
    }
    if (!ftSn_SpecialN_SearchTarget(gobj, da, &target_x, &target_y)) {
        if (logged < FTKBSN_LOG) {
            logged++;
            OSReport("[ak] Kirby (Sonic) homing attack: no target within %.0f of (%.2f, %.2f), "
                     "dive\n",
                     da->specialn_search_range, fp->cur_pos.x, fp->cur_pos.y);
        }
        ftKbSn_SpecialNAttackMiss_Enter(gobj);
        return;
    }
    if (logged < FTKBSN_LOG) {
        logged++;
        OSReport("[ak] Kirby (Sonic) homing attack: from (%.2f, %.2f) at target (%.2f, %.2f)\n",
                 fp->cur_pos.x, fp->cur_pos.y, target_x, target_y);
    }

    ftKbSn_SpecialNAttack_Enter(gobj,
                                da->specialn_aim_stick_x * fp->input.lstick[0].x + target_x,
                                da->specialn_aim_stick_y * fp->input.lstick[0].y + target_y);
}

/* ---- AttackMiss ----------------------------------------------------------------------------- */

/* +0x11DC (41 words), SpecialNAttackMiss_Enter */
static void ftKbSn_SpecialNAttackMiss_Enter(HSD_GObj* gobj)
{
    Fighter* fp = GET_FIGHTER(gobj);
    ftSonic_DatAttrs* da = ftKbSn_DA(fp);

    if (da == NULL) {
        return;
    }
    mu_ak_kirby_state_change(gobj, ftKbSn_State_SpecialNAttackMiss, Ft_MF_None, 0.0F,
                             da->specialn_attack_anim_rate, 0.0F);
    fp->self_vel.x = da->specialn_miss_speed * fp->facing_dir;
    fp->self_vel.y = -da->specialn_miss_speed;
    ftSn_MV(fp)->specialn.timer = da->specialn_attack_frames;
    fp->accessory4_cb = ftKbSn_GFXSpinAndTrail;
    fp->deal_dmg_cb = ftKbSn_SpecialNRebound_OnEnter;
}

/* [state 3 Anim] +0x5B0 (33 words) */
static void ftKbSn_SpecialNAttackMiss_Anim(HSD_GObj* gobj)
{
    Fighter* fp = GET_FIGHTER(gobj);
    ftSonic_SpecialNVars* mv = &ftSn_MV(fp)->specialn;

    if (mv->timer > 0) {
        mv->timer--;
        return;
    }
    ftKbSn_SpecialNCancel_Enter(gobj);
}

/* [state 3 Coll] +0x63C (39 words); [state 4 Coll] +0x864 is a branch to it. Touching a wall,
 * ceiling or floor rebounds; otherwise grab a ledge if one is in reach. */
static void ftKbSn_SpecialNDash_Coll(HSD_GObj* gobj)
{
    Fighter* fp = GET_FIGHTER(gobj);
    ftSonic_DatAttrs* da = ftKbSn_DA(fp);
    ftCollisionBox box;
    u32 env;

    if (da == NULL) {
        return;
    }
    box = ftCollisionBox_FromDisc(&da->coll_box);
    ft_800824A0(gobj, &box);
    env = fp->coll_data.env_flags;
    if (env & (Collide_WallMask | Collide_CeilingMask | Collide_FloorMask)) {
        ftKbSn_SpecialNRebound_Enter(gobj, env);
        return;
    }
    if (ftCliffCommon_80081298(gobj)) {
        ftCliffCommon_80081370(gobj);
    }
}

/* ---- Attack --------------------------------------------------------------------------------- */

/* +0x10F0 (59 words), SpecialNAttack_Enter */
static void ftKbSn_SpecialNAttack_Enter(HSD_GObj* gobj, float target_x, float target_y)
{
    Fighter* fp = GET_FIGHTER(gobj);
    ftSonic_DatAttrs* da = ftKbSn_DA(fp);
    ftSonic_SpecialNVars* mv = &ftSn_MV(fp)->specialn;
    float angle;

    if (da == NULL) {
        return;
    }
    mv->target.x = target_x;
    mv->target.y = target_y;
    mv->timer = da->specialn_attack_frames;
    angle = atan2f(target_y - fp->cur_pos.y, target_x - fp->cur_pos.x);
    mv->angle = angle;
    fp->self_vel.x = da->specialn_attack_speed * cosf(angle);
    fp->self_vel.y = da->specialn_attack_speed * sinf(mv->angle);

    mu_ak_kirby_state_change(gobj, ftKbSn_State_SpecialNAttack, Ft_MF_None, 0.0F,
                             da->specialn_attack_anim_rate, 0.0F);
    fp->accessory4_cb = ftKbSn_GFXSpinAndTrailVelocityDirection;
    fp->deal_dmg_cb = ftKbSn_SpecialNHit_Enter;
}

/* [state 4 Anim] +0x6D8 (97 words). Keep the dash at full speed along its angle; stop when the
 * time runs out or the target point is within one frame's travel. (After the time runs out the
 * distance test still runs and can enter Cancel a second time, as in Sonic's own.) */
static void ftKbSn_SpecialNAttack_Anim(HSD_GObj* gobj)
{
    Fighter* fp = GET_FIGHTER(gobj);
    ftSonic_DatAttrs* da = ftKbSn_DA(fp);
    ftSonic_SpecialNVars* mv = &ftSn_MV(fp)->specialn;
    float dist2;

    if (da == NULL) {
        return;
    }
    fp->self_vel.x = da->specialn_attack_speed * cosf(mv->angle);
    fp->self_vel.y = da->specialn_attack_speed * sinf(mv->angle);

    if (mv->timer > 0) {
        mv->timer--;
    } else {
        ftKbSn_SpecialNCancel_Enter(gobj);
    }

    dist2 = ftSn_Vec3_DistSquared(&fp->cur_pos, &mv->target);
    if (!(dist2 <= da->specialn_attack_speed * da->specialn_attack_speed)) {
        return;
    }
    ftKbSn_SpecialNCancel_Enter(gobj);
}

/* ---- Cancel --------------------------------------------------------------------------------- */

/* SpecialNCancel_Enter, which the hat's code has inside +0x5B0 and (twice) +0x6D8. */
static void ftKbSn_SpecialNCancel_Enter(HSD_GObj* gobj)
{
    Fighter* fp = GET_FIGHTER(gobj);

    mu_ak_kirby_state_change(gobj, ftKbSn_State_SpecialNCancel, Ft_MF_None, 0.0F, 1.0F, 0.0F);
    fp->self_vel.y = 0.0F;
    fp->cmd_vars[0] = 0;
}

/* [state 5 Phys] +0x8C8 (22 words) */
static void ftKbSn_SpecialNCancel_Phys(HSD_GObj* gobj)
{
    Fighter* fp = GET_FIGHTER(gobj);
    ftSonic_DatAttrs* da = ftKbSn_DA(fp);

    if (da == NULL) {
        return;
    }
    ft_80084DB0(gobj);
    ftCommon_CalcSelfAccel_Deaccel(fp, da->specialn_cancel_air_decel);
}

/* [state 5 Coll] +0x920 (45 words) */
static void ftKbSn_SpecialNCancel_Coll(HSD_GObj* gobj)
{
    Fighter* fp = GET_FIGHTER(gobj);

    if (ft_CheckGroundAndLedge(gobj, (int) fp->facing_dir)) {
        ftKbSn_SpecialNLanding_Enter(gobj);
    }
}

/* ---- Landing -------------------------------------------------------------------------------- */

/* SpecialNLanding_Enter, which the hat's code has inside +0x920 and +0xB3C. */
static void ftKbSn_SpecialNLanding_Enter(HSD_GObj* gobj)
{
    Fighter* fp = GET_FIGHTER(gobj);

    ftCommon_8007D6A4(fp);
    mu_ak_kirby_state_change(gobj, ftKbSn_State_SpecialNLanding, Ft_MF_None, 0.0F, 1.0F, 0.0F);
    fp->self_vel.y = 0.0F;
    fp->cmd_vars[0] = 0;
}

/* [state 6 Phys] +0xA34 (20 words) */
static void ftKbSn_SpecialNLanding_Phys(HSD_GObj* gobj)
{
    Fighter* fp = GET_FIGHTER(gobj);
    ftSonic_DatAttrs* da = ftKbSn_DA(fp);

    if (da == NULL) {
        return;
    }
    ftCommon_CalcGroundAccel_Deaccel(fp, da->specialn_landing_decel);
    ftCommon_SetSelfMovementFromGroundedMovement(gobj);
}

/* ---- Rebound / Hit -------------------------------------------------------------------------- */

/* +0xF80 (62 words), SpecialNRebound_Enter: bounce back off whatever was touched. A wall or
 * ceiling reverses and damps x, a floor only damps it; y always flips. */
static void ftKbSn_SpecialNRebound_Enter(HSD_GObj* gobj, u32 env_flags)
{
    Fighter* fp = GET_FIGHTER(gobj);
    ftSonic_DatAttrs* da = ftKbSn_DA(fp);
    static int logged;

    if (da == NULL) {
        return;
    }
    mu_ak_kirby_state_change(gobj, ftKbSn_State_SpecialNRebound, Ft_MF_None, 0.0F, 1.0F, 0.0F);
    if (env_flags & (Collide_WallMask | Collide_CeilingMask)) {
        fp->self_vel.x = fp->self_vel.x * -da->specialn_rebound_mul_x;
    } else if (env_flags & Collide_FloorMask) {
        fp->self_vel.x = fp->self_vel.x * da->specialn_rebound_mul_x;
    }
    ftSn_SpecialN_ClampReboundVel(fp, da, -fp->self_vel.y);
    if (logged < FTKBSN_LOG) {
        logged++;
        OSReport("[ak] Kirby (Sonic) homing attack rebound: touched %08X, velocity (%.2f, %.2f)\n",
                 (unsigned) env_flags, fp->self_vel.x, fp->self_vel.y);
    }
}

/* +0x1644 (2 words), SpecialNRebound_OnEnter: the miss dive's deal_dmg_cb (it hit a shield or
 * an opponent). */
static void ftKbSn_SpecialNRebound_OnEnter(HSD_GObj* gobj)
{
    ftKbSn_SpecialNRebound_Enter(gobj, 0);
}

/* +0x14B4 (57 words), SpecialNHit_Enter: the homing dash's deal_dmg_cb. Pops up at least 2
 * units a frame (+0xC6C). */
static void ftKbSn_SpecialNHit_Enter(HSD_GObj* gobj)
{
    Fighter* fp = GET_FIGHTER(gobj);
    ftSonic_DatAttrs* da = ftKbSn_DA(fp);
    float vel_y;
    static int logged;

    if (da == NULL) {
        return;
    }
    mu_ak_kirby_state_change(gobj, ftKbSn_State_SpecialNHit, Ft_MF_None, 0.0F, 1.0F, 0.0F);
    vel_y = fp->self_vel.y;
    if (vel_y < 0.0F) {
        vel_y = -vel_y;
    }
    if (vel_y < 2.0F) {
        vel_y = 2.0F;
    }
    fp->self_vel.y = vel_y;
    ftSn_SpecialN_ClampReboundVel(fp, da, vel_y);
    if (logged < FTKBSN_LOG) {
        logged++;
        OSReport("[ak] Kirby (Sonic) homing attack hit: velocity (%.2f, %.2f)\n", fp->self_vel.x,
                 fp->self_vel.y);
    }
}

/* [state 7 Phys, 8 Phys] +0xAE8 (21 words) */
static void ftKbSn_SpecialNRebound_Phys(HSD_GObj* gobj)
{
    Fighter* fp = GET_FIGHTER(gobj);
    ftSonic_DatAttrs* da = ftKbSn_DA(fp);

    if (da == NULL) {
        return;
    }
    ftCommon_CalcSelfAccel_Deaccel(fp, da->specialn_rebound_air_decel);
    ftCommon_Fall(fp, da->specialn_rebound_gravity, da->specialn_rebound_terminal_vel);
}

/* [state 7 Coll, 8 Coll] +0xB3C (39 words) */
static void ftKbSn_SpecialNRebound_Coll(HSD_GObj* gobj)
{
    if (ft_80081D0C(gobj)) {
        ftKbSn_SpecialNLanding_Enter(gobj);
    }
}

/* ---- the tables ----------------------------------------------------------------------------- */

/* [move_logic] +0x144 (nine entries of 0x20 bytes). Words +4 and +8 are Sonic's own (0x00340111
 * and move id 0x12); KirbyStateChange does not read them. The first word is the entry of the hat
 * file's ftcmd table (eight animations: the dive and the dash share the spin).
 *
 * The callbacks that hold no state change and read no value are the same routines as Sonic's
 * own and are his functions:
 *   IASA and Phys of states 0 to 4 (+0x4B0, +0x4B4, +0x548, +0x54C, +0x634, +0x638, +0x85C,
 *     +0x860): empty;
 *   [state 0 Coll, 1 Coll] +0x4B8: ft_80081D0C;
 *   [state 5 Anim] +0x868 and [state 7 Anim, 8 Anim] +0xA88: the end of the animation falls;
 *   [state 5 IASA] +0x8B4 and [state 7 IASA, 8 IASA] +0xAD4: the script's cmd_vars[0] opens the
 *     fall's interrupts;
 *   [state 6 Anim] +0x9D4, [state 6 IASA] +0xA20, [state 6 Coll] +0xA84: the landing. */
static const MotionState ftKbSn_MotionStateTable[ftKbSn_State_Count] = {
    {
        /* state 0, ftcmd 0 "SnSpecialNStart" */
        0,
        0x00340111,
        0x12 << 24,
        ftKbSn_SpecialNStart_Anim,
        ftSn_SpecialNStart_IASA,
        ftSn_SpecialNStart_Phys,
        ftSn_SpecialNStart_Coll,
        ftCamera_UpdateCameraBox,
    },
    {
        /* state 1, ftcmd 1 "SnSpecialNAirStart" */
        1,
        0x00340111,
        0x12 << 24,
        ftKbSn_SpecialNStart_Anim,
        ftSn_SpecialNStart_IASA,
        ftSn_SpecialNStart_Phys,
        ftSn_SpecialNStart_Coll,
        ftCamera_UpdateCameraBox,
    },
    {
        /* state 2, ftcmd 2 "SnSpecialNSpin" */
        2,
        0x00340111,
        0x12 << 24,
        ftKbSn_SpecialNCharge_Anim,
        ftSn_SpecialNCharge_IASA,
        ftSn_SpecialNCharge_Phys,
        ftKbSn_SpecialNCharge_Coll,
        ftCamera_UpdateCameraBox,
    },
    {
        /* state 3, ftcmd 3 "SnSpecialNSpin" */
        3,
        0x00340111,
        0x12 << 24,
        ftKbSn_SpecialNAttackMiss_Anim,
        ftSn_SpecialNAttackMiss_IASA,
        ftSn_SpecialNAttackMiss_Phys,
        ftKbSn_SpecialNDash_Coll,
        ftCamera_UpdateCameraBox,
    },
    {
        /* state 4, ftcmd 3 "SnSpecialNSpin" */
        3,
        0x00340111,
        0x12 << 24,
        ftKbSn_SpecialNAttack_Anim,
        ftSn_SpecialNAttack_IASA,
        ftSn_SpecialNAttack_Phys,
        ftKbSn_SpecialNDash_Coll,
        ftCamera_UpdateCameraBox,
    },
    {
        /* state 5, ftcmd 4 "SnSpecialNCancel" */
        4,
        0x00340111,
        0x12 << 24,
        ftSn_SpecialNCancel_Anim,
        ftSn_SpecialNCancel_IASA,
        ftKbSn_SpecialNCancel_Phys,
        ftKbSn_SpecialNCancel_Coll,
        ftCamera_UpdateCameraBox,
    },
    {
        /* state 6, ftcmd 7 "SnSpecialNLanding" */
        7,
        0x00340111,
        0x12 << 24,
        ftSn_SpecialNLanding_Anim,
        ftSn_SpecialNLanding_IASA,
        ftKbSn_SpecialNLanding_Phys,
        ftSn_SpecialNLanding_Coll,
        ftCamera_UpdateCameraBox,
    },
    {
        /* state 7, ftcmd 6 "SnSpecialNRebound" */
        6,
        0x00340111,
        0x12 << 24,
        ftSn_SpecialNRebound_Anim,
        ftSn_SpecialNRebound_IASA,
        ftKbSn_SpecialNRebound_Phys,
        ftKbSn_SpecialNRebound_Coll,
        ftCamera_UpdateCameraBox,
    },
    {
        /* state 8, ftcmd 5 "SnSpecialNHit" */
        5,
        0x00340111,
        0x12 << 24,
        ftSn_SpecialNRebound_Anim,
        ftSn_SpecialNRebound_IASA,
        ftKbSn_SpecialNRebound_Phys,
        ftKbSn_SpecialNRebound_Coll,
        ftCamera_UpdateCameraBox,
    },
};

/* The kbFunction exports of PlKbCpSn.dat (0 to 4 and 6; it has no export 5 and no export 7). */
const MuAkKirbyCopy ftKbSn_Copy = {
    .on_swallow = ftKbSn_OnSwallow,
    .on_lose = ftKbSn_OnLose,
    .special_n = ftKbSn_SpecialNStart_Enter,
    .special_air_n = ftKbSn_SpecialNStart_Enter,
    .on_hurt = ftKbSn_OnHurt,
    .init_items = NULL,
    .move_logic = ftKbSn_MotionStateTable,
    .move_logic_count = ftKbSn_State_Count,
    .on_frame = NULL,
};
