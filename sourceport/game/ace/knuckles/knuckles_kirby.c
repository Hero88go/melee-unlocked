/* ACE 2.0.0's Knuckles: the ability Kirby copies from him (m-ex "kbFunction" of PlKbCpKx.dat).
 *
 * The hat file's code is the code of Akaneia's Sonic hat (PlKbCpSn.dat,
 * akaneia/sonic/sonic_kirby.c) built again: the same routines in the same order, the homing
 * attack's nine states, the spin ball painted like Kirby, the trail in Kirby's fighter variables
 * (console fp+2270 to fp+227C), the six dynamic bones of the hat. Compared routine by routine
 * (run-source/rel09-ace-native/knuckles/kirby/KIRBY.md): every routine does the steps of the Sonic
 * hat's, most word for word, the rest with other registers or another order of independent
 * stores, and one with another float order:
 *   - SpawnTrailEffect (+0x1724): the step length squared is (dy*dy + dx*dx, one fmadds)
 *     + dz*dz, where the Sonic hat's is two fmadds; so the trail is written here
 *     (ftKbKx_SpawnTrailEffect) and not taken from ftSn_SpawnTrailEffectAt.
 * What makes Kirby's Knuckles ability differ from Kirby's Sonic ability is the data: the values
 * at hat data +0xC (the layout of Sonic's attributes, ftSonic_DatAttrs) have a search range of 0
 * and a target offset of 0, so the search never finds a target and every use is the dive; the
 * charge can not be cut short (minimum = maximum = 30 frames); the dive moves at 2.5 for 40
 * frames. The hat data +0x10 is the six-bone ftDynamics of the hat.
 * The ability has no articles: the hat file has no itFunction and no InitCopyItems export.
 *
 * The hat file has no debug symbols: the names are the Sonic hat's for the routine in the same
 * place, and each function gives its offset in the hat file's code block
 * (run-source/rel09-ace-native/knuckles/kirby/PlKbCpKx.listing.txt shows it at 0x81800000 plus
 * the offset). The code has no fixed console address: m-ex loads it with the hat file.
 *
 * Nothing below keeps a kind or changes a static during a match (the `logged` counters only
 * limit the log): the charge count, the timer, the angle and the target live in Kirby's motion
 * variables (console fp+2340 on, Sonic's layout), the trail in his fighter variables. */
#include "../../ace/knuckles/knuckles.h"

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

#include "knuckles_kirby.h"

/* Kirby's states of this ability (KirbyStateChange state numbers; motion ids 400 to 408), in
 * the order of the Sonic hat's. */
enum {
    ftKbKx_State_SpecialNStart,      /* 0: windup, ground animation */
    ftKbKx_State_SpecialAirNStart,   /* 1: windup, air animation */
    ftKbKx_State_SpecialNCharge,     /* 2: curled up, counting */
    ftKbKx_State_SpecialNAttackMiss, /* 3: no target, dives forward and down */
    ftKbKx_State_SpecialNAttack,     /* 4: homing dash (never entered with this hat's data) */
    ftKbKx_State_SpecialNCancel,     /* 5: the dash ended without a hit */
    ftKbKx_State_SpecialNLanding,    /* 6 */
    ftKbKx_State_SpecialNRebound,    /* 7: bounced off a wall, the floor or a shield */
    ftKbKx_State_SpecialNHit,        /* 8: bounce after hitting the target */
    ftKbKx_State_Count
};

/* The spin ball: effect 2 of Knuckles' effect file (+0x1238: li r3, 0x138A), on Kirby's part 4. */
#define ftKbKx_Ef_Spin 5002
/* The trail segment (+0x1888: li r3, 0x1389). */
#define ftKbKx_Ef_Trail 5001

/* The trail's material color, an immediate of the code (+0x18CC: ori r4, r9, 0xFFFF): blue. */
static const GXColor ftKbKx_TrailColor = { 0x00, 0x00, 0xFF, 0xFF };

/* +0x1964, the double 6.283185307179586. */
#define FTKBKX_TAU 6.283185307179586

/* +0xC58: the longest trail step, squared. */
#define FTKBKX_TRAIL_MAX_DIST2 400.0f

#define FTKBKX_LOG 8 /* evidence lines of each kind that go in the log, per run */

/* What the ability keeps for Kirby between frames, at console fp+2270: the first words of his
 * texture list for the retail "parts" hats (fp->u.kb.x44), not read while a hat of this kind is
 * worn. The layout of Sonic's own trail fields (and of ftKbSn_FighterVars). */
typedef struct ftKbKx_FighterVars {
    /* 2270 */ Vec3 trail_pos; /* bone 2 world position last trail frame */
    /* 227C */ float trail_angle;
} ftKbKx_FighterVars;

static inline ftKbKx_FighterVars* ftKbKx_FV(Fighter* fp)
{
    _Static_assert(sizeof(ftKbKx_FighterVars) <= sizeof(fp->u.kb.x44),
                   "Kirby's Knuckles trail fields must fit the hat texture list they overlay");
    return (ftKbKx_FighterVars*) &fp->u.kb.x44;
}

/* MEX_GetKirbyCpData(fp->kb.hat.kind) and the word at +0xC of what it returns: the values of the
 * move. NULL when Kirby no longer holds a hat of an added kind (not an m-ex case: there the
 * routine then reads the table of whatever kind he holds); the callers do nothing then. */
static ftSonic_DatAttrs* ftKbKx_DA(Fighter* fp)
{
    KirbyHatStruct* hat = mu_ak_kirby_hat(fp->u.kb.hat.kind);

    if (hat == NULL) {
        return NULL;
    }
    return DISC_GET(ftSonic_DatAttrs, hat->hat_dynamics[0]);
}

static void ftKbKx_SpecialNCharge_Enter(HSD_GObj* gobj);
static void ftKbKx_SearchTarget_EnterAttack(HSD_GObj* gobj);
static void ftKbKx_SpecialNAttackMiss_Enter(HSD_GObj* gobj);
static void ftKbKx_SpecialNAttack_Enter(HSD_GObj* gobj, float target_x, float target_y);
static void ftKbKx_SpecialNCancel_Enter(HSD_GObj* gobj);
static void ftKbKx_SpecialNLanding_Enter(HSD_GObj* gobj);
static void ftKbKx_SpecialNRebound_Enter(HSD_GObj* gobj, u32 env_flags);
static void ftKbKx_SpecialNRebound_OnEnter(HSD_GObj* gobj);
static void ftKbKx_SpecialNHit_Enter(HSD_GObj* gobj);
static void ftKbKx_GFXSpin(HSD_GObj* gobj);
static void ftKbKx_GFXSpinAndTrail(HSD_GObj* gobj);
static void ftKbKx_GFXSpinAndTrailVelocityDirection(HSD_GObj* gobj);

/* ---- the hat ------------------------------------------------------------------------------- */

/* +0x250 (67 words, word for word the Sonic hat's +0x264): the hat's dynamic bones, from the
 * ftDynamics at hat data +0x10. The console stops with "fighter dynamics num over!" (an assert,
 * line 48 of the hat's source) when the file asks for more than 9; here Kirby then gets none. */
static void ftKbKx_SetupHatDynamics(Fighter* fp, KirbyHatStruct* hat)
{
    ftDynamics* dynamics = DP(hat->hat_dynamics[1]);
    int i;
    static int logged;

    fp->dynamics_num = dynamics->dynamicsNum;
    if (fp->dynamics_num >= Ft_Dynamics_NumMax) {
        OSReport("[ak] Kirby (Knuckles): fighter dynamics num over! (%d); the hat gets none\n",
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
    if (logged < FTKBKX_LOG) {
        logged++;
        OSReport("[ak] Kirby (Knuckles) hat: %d dynamic bones\n", fp->dynamics_num);
    }
}

/* [OnKirbySwallow] +0x000 (47 words): nothing when Kirby already wears a hat; else the retail
 * hat attach without the x2225_b2 write, then +0x250. The Sonic hat's +0x000 (51 words) with the
 * gobj and the joint kept on the stack. */
static void ftKbKx_OnSwallow(HSD_GObj* gobj)
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
    ftKbKx_SetupHatDynamics(fp, hat);
}

/* [OnKirbyLoseAbility] +0x0BC (26 words): the steps of the retail ftKb_SpecialN_800EFAF0 (the
 * array pointer loaded before the joint word is cleared), then, hat or not, a call through the
 * word at +0x35C, 8009E0D4, ftCo_UnloadDynamicBones. The Sonic hat's +0x0CC. */
static void ftKbKx_OnLose(HSD_GObj* gobj)
{
    Fighter* fp = GET_FIGHTER(gobj);

    ftKb_SpecialN_800EFAF0(gobj);
    ftCo_UnloadDynamicBones(fp);
}

/* [OnKirbyHurt] +0x12C (1 word): empty. */
static void ftKbKx_OnHurt(HSD_GObj* gobj) {}

/* ---- effects ------------------------------------------------------------------------------- */

/* +0x1224 (96 words): the spin ball, painted like Kirby. The Sonic hat's +0x1280 (93 words, the
 * same calls and loads with other registers): see ftKbSn_SpawnSpin in sonic_kirby.c for what the
 * paint does and why the checks are there. */
static void ftKbKx_SpawnSpin(HSD_GObj* gobj)
{
    Fighter* fp = GET_FIGHTER(gobj);
    EF_Effect* effect = efSync_Spawn(ftKbKx_Ef_Spin, gobj, fp->parts[FtPart_HipN].joint);
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
            OSReport("[ak] Kirby (Knuckles) spin ball: dobj not found! (Kirby's model has no "
                     "eighth display object)\n");
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

/* +0x1024 (7 words) and +0x1040 (21 words), Sonic_GFXSpin split in two: the spin ball once per
 * move (x2219_b0 marks it spawned). Installed as accessory4_cb and removes itself. The Sonic hat's
 * +0x1078 (30 words). */
static void ftKbKx_GFXSpin(HSD_GObj* gobj)
{
    Fighter* fp = GET_FIGHTER(gobj);

    if (!fp->x2219_b0) {
        ftKbKx_SpawnSpin(gobj);
        fp->x2219_b0 = true;
        fp->pre_hitlag_cb = efLib_PauseAll;
        fp->post_hitlag_cb = efLib_ResumeAll;
    }
    fp->accessory4_cb = NULL;
}

/* +0x16B0 (23 words), Sonic_UpdateTrailPosAndRot: word for word the Sonic hat's +0x16FC. */
static void ftKbKx_UpdateTrailPosAndRot(HSD_GObj* gobj)
{
    Fighter* fp = GET_FIGHTER(gobj);
    ftKbKx_FighterVars* fv = ftKbKx_FV(fp);

    lb_8000B1CC(fp->parts[FtPart_XRotN].joint, NULL, &fv->trail_pos);
    fv->trail_angle =
        atan2f(fp->cur_pos.y - fp->prev_pos.y, fp->cur_pos.x - fp->prev_pos.x);
}

/* +0x1724 (144 words), SpawnTrailEffect over Kirby's fields with the fixed color: lay a trail
 * segment from last frame's bone 2 position to this frame's. Sonic's routine
 * (ftSn_SpawnTrailEffectAt) step for step, but for the float order of the step length: dx*dx
 * and dz*dz are rounded on their own, dy*dy + dx*dx is one fmadds, then dz*dz is added. The
 * length test is "not more than 400" as the console's bgt has it (a NaN length spawns). */
MU_NO_CONTRACT static void ftKbKx_SpawnTrailEffect(HSD_GObj* gobj, bool spawn)
{
    Fighter* fp = GET_FIGHTER(gobj);
    ftKbKx_FighterVars* fv = ftKbKx_FV(fp);
    Vec3* trail_pos = &fv->trail_pos;
    Vec3 pos;
    float angle;

    lb_8000B1CC(fp->parts[FtPart_XRotN].joint, NULL, &pos);

    angle = atan2f(pos.y - trail_pos->y, pos.x - trail_pos->x);
    if (angle < 0.0F) {
        do {
            angle = (float) (angle + FTKBKX_TAU);
        } while (angle < 0.0F);
    }
    while (angle >= FTKBKX_TAU) {
        angle = (float) (angle - FTKBKX_TAU);
    }

    if (trail_pos->x != pos.x || trail_pos->y != pos.y) {
        float dx = trail_pos->x - pos.x;
        float dz = trail_pos->z - pos.z;
        float dy = trail_pos->y - pos.y;
        float dx2 = dx * dx;
        float dz2 = dz * dz;
        float dist2 = MU_FMADDS(dy, dy, dx2) + dz2;

        if (spawn && !(fp->self_vel.x * fp->facing_dir < 0.0F) &&
            !(dist2 > FTKBKX_TRAIL_MAX_DIST2))
        {
            Vec3 zero = { 0.0F, 0.0F, 0.0F };
            EF_Effect* effect = efSync_Spawn(ftKbKx_Ef_Trail, gobj, &zero, NULL);

            if (effect != NULL) {
                HSD_JObj* root;
                HSD_JObj* head;
                HSD_JObj* tail;
                Vec3 scale;

                /* Leave it in the world rather than following the fighter. */
                effect->parent_gobj = NULL;

                /* The root carries the fighter's scale; move it onto the two ends and put the
                 * head at this frame's position, the tail at last frame's. */
                root = effect->gobj->hsd_obj;
                scale = root->scale;
                root->scale.x = 1.0F;
                root->scale.y = 1.0F;
                root->scale.z = 1.0F;

                head = root->child;
                head->scale = scale;
                head->translate = pos;
                head->rotate.z = angle;

                tail = head->next;
                tail->scale = scale;
                tail->translate = *trail_pos;
                tail->rotate.z = fv->trail_angle;

                {
                    HSD_DObj* dobj = root->u.dobj;
                    dobj->mobj->mat->diffuse = ftKbKx_TrailColor;
                }
            }
        }
    }

    *trail_pos = pos;
    fv->trail_angle = angle;
}

/* +0x170C (6 words), Sonic_GFXTrailLoop: word for word the Sonic hat's +0x1758 but the branch. */
static void ftKbKx_GFXTrailLoop(HSD_GObj* gobj)
{
    Fighter* fp = GET_FIGHTER(gobj);

    ftKbKx_SpawnTrailEffect(gobj, fp->cmd_vars[3] == 1);
}

/* Sonic_GFXTrail, which the hat's code has inside its two callers: start a trail and hand over
 * to the per-frame loop. The motion id test is Sonic's (his up smash, 0x3F); Kirby's is 400 and
 * up in these states, so the trail is always switched on. */
static void ftKbKx_GFXTrail(HSD_GObj* gobj)
{
    Fighter* fp = GET_FIGHTER(gobj);

    ftKbKx_UpdateTrailPosAndRot(gobj);
    fp->accessory4_cb = ftKbKx_GFXTrailLoop;
    if (fp->motion_id != ftCo_MS_AttackHi4) {
        fp->cmd_vars[3] = 1;
    }
}

/* +0x1544 (43 words), Sonic_GFXSpinAndTrail: the miss dive's accessory4_cb (the Sonic hat's
 * +0x1598). */
static void ftKbKx_GFXSpinAndTrail(HSD_GObj* gobj)
{
    ftKbKx_GFXSpin(gobj);
    ftKbKx_GFXTrail(gobj);
}

/* +0x13A4 (48 words), Sonic_GFXSpinAndTrailVelocityDirection: the homing dash's accessory4_cb;
 * as above, with the trail angle taken from the velocity (the Sonic hat's +0x13F4). */
static void ftKbKx_GFXSpinAndTrailVelocityDirection(HSD_GObj* gobj)
{
    Fighter* fp;

    ftKbKx_GFXSpin(gobj);
    ftKbKx_GFXTrail(gobj);
    fp = GET_FIGHTER(gobj);
    ftKbKx_FV(fp)->trail_angle = atan2f(fp->self_vel.y, fp->self_vel.x);
}

/* ---- entry ---------------------------------------------------------------------------------- */

/* [SpecialN] +0x124 and [SpecialAirN] +0x128 are both a branch to +0x360 (60 words),
 * SpecialNStart_Enter: KirbyStateChange(gobj, 0 on the ground or 1 in the air, 0, 0, 0.0, 1.0,
 * 0.0). From the ground Kirby is put in the air first. The Sonic hat's +0x374. */
static void ftKbKx_SpecialNStart_Enter(HSD_GObj* gobj)
{
    Fighter* fp = GET_FIGHTER(gobj);

    if (fp->ground_or_air == GA_Air) {
        mu_ak_kirby_state_change(gobj, ftKbKx_State_SpecialAirNStart, Ft_MF_None, 0.0F, 1.0F,
                                 0.0F);
    } else {
        ftCommon_8007D5D4(fp);
        mu_ak_kirby_state_change(gobj, ftKbKx_State_SpecialNStart, Ft_MF_None, 0.0F, 1.0F, 0.0F);
    }
    ftAnim_8006EBA4(gobj);
    ftSn_MV(fp)->specialn.charge_frames = 0;
    fp->self_vel.x = 0.0F;
    fp->self_vel.y = 0.0F;
}

/* ---- Start ---------------------------------------------------------------------------------- */

/* [state 0 Anim, 1 Anim] +0x450 (19 words) */
static void ftKbKx_SpecialNStart_Anim(HSD_GObj* gobj)
{
    if (!ftAnim_IsFramesRemaining(gobj)) {
        ftKbKx_SpecialNCharge_Enter(gobj);
    }
}

/* ---- Charge --------------------------------------------------------------------------------- */

/* +0xC60 (32 words), SpecialNCharge_Enter */
static void ftKbKx_SpecialNCharge_Enter(HSD_GObj* gobj)
{
    Fighter* fp = GET_FIGHTER(gobj);
    ftSonic_DatAttrs* da = ftKbKx_DA(fp);

    if (da == NULL) {
        return;
    }
    mu_ak_kirby_state_change(gobj, ftKbKx_State_SpecialNCharge, Ft_MF_None, 0.0F,
                             da->specialn_charge_anim_rate, 0.0F);
    fp->self_vel.y = da->specialn_charge_vel_y;
    fp->accessory4_cb = ftKbKx_GFXSpin;
}

/* [state 2 Anim] +0x4A8 (35 words). With this hat's data (minimum 30 = maximum 30) the B press
 * can not end the charge early. */
static void ftKbKx_SpecialNCharge_Anim(HSD_GObj* gobj)
{
    Fighter* fp = GET_FIGHTER(gobj);
    ftSonic_DatAttrs* da = ftKbKx_DA(fp);
    ftSonic_SpecialNVars* mv = &ftSn_MV(fp)->specialn;

    if (da == NULL) {
        return;
    }
    if (mv->charge_frames >= da->specialn_max_charge_frames ||
        (mv->charge_frames >= da->specialn_min_charge_frames &&
         (fp->input.pressed_buttons & HSD_PAD_B)))
    {
        ftKbKx_SearchTarget_EnterAttack(gobj);
        return;
    }
    mv->charge_frames++;
}

/* [state 2 Coll] +0x53C (24 words, word for word the Sonic hat's +0x550). The collision box is
 * the first six words of the values. */
static void ftKbKx_SpecialNCharge_Coll(HSD_GObj* gobj)
{
    Fighter* fp = GET_FIGHTER(gobj);
    ftSonic_DatAttrs* da = ftKbKx_DA(fp);
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

/* +0xCE0 (133 words) with its tail +0xEF4 (15 words), SpecialN_SearchTarget_EnterAttack: the
 * search of the Sonic hat's +0xD00 (the same loads, compares, team test, fmadds order and item
 * walk, other registers and block order), so ftSn_SpecialN_SearchTarget serves with the hat's
 * values; then the dash at the aim point nudged by the stick (two fmadds), or the dive. This hat's
 * search range is 0, so the dive is all that happens. */
static void ftKbKx_SearchTarget_EnterAttack(HSD_GObj* gobj)
{
    Fighter* fp = GET_FIGHTER(gobj);
    ftSonic_DatAttrs* da = ftKbKx_DA(fp);
    float target_x;
    float target_y;
    static int logged;

    if (da == NULL) {
        return;
    }
    if (!ftSn_SpecialN_SearchTarget(gobj, da, &target_x, &target_y)) {
        if (logged < FTKBKX_LOG) {
            logged++;
            OSReport("[ak] Kirby (Knuckles) dive: no target within %.0f of (%.2f, %.2f)\n",
                     da->specialn_search_range, fp->cur_pos.x, fp->cur_pos.y);
        }
        ftKbKx_SpecialNAttackMiss_Enter(gobj);
        return;
    }
    if (logged < FTKBKX_LOG) {
        logged++;
        OSReport("[ak] Kirby (Knuckles) homing attack: from (%.2f, %.2f) at target (%.2f, %.2f)\n",
                 fp->cur_pos.x, fp->cur_pos.y, target_x, target_y);
    }

    ftKbKx_SpecialNAttack_Enter(gobj,
                                MU_FMADDS(da->specialn_aim_stick_x, fp->input.lstick[0].x,
                                          target_x),
                                MU_FMADDS(da->specialn_aim_stick_y, fp->input.lstick[0].y,
                                          target_y));
}

/* ---- AttackMiss ----------------------------------------------------------------------------- */

/* +0x1180 (41 words), SpecialNAttackMiss_Enter (the Sonic hat's +0x11DC): the dive. */
static void ftKbKx_SpecialNAttackMiss_Enter(HSD_GObj* gobj)
{
    Fighter* fp = GET_FIGHTER(gobj);
    ftSonic_DatAttrs* da = ftKbKx_DA(fp);

    if (da == NULL) {
        return;
    }
    mu_ak_kirby_state_change(gobj, ftKbKx_State_SpecialNAttackMiss, Ft_MF_None, 0.0F,
                             da->specialn_attack_anim_rate, 0.0F);
    fp->self_vel.y = -da->specialn_miss_speed;
    fp->self_vel.x = fp->facing_dir * da->specialn_miss_speed;
    ftSn_MV(fp)->specialn.timer = da->specialn_attack_frames;
    fp->accessory4_cb = ftKbKx_GFXSpinAndTrail;
    fp->deal_dmg_cb = ftKbKx_SpecialNRebound_OnEnter;
}

/* [state 3 Anim] +0x59C (30 words) */
static void ftKbKx_SpecialNAttackMiss_Anim(HSD_GObj* gobj)
{
    Fighter* fp = GET_FIGHTER(gobj);
    ftSonic_SpecialNVars* mv = &ftSn_MV(fp)->specialn;

    if (mv->timer > 0) {
        mv->timer--;
        return;
    }
    ftKbKx_SpecialNCancel_Enter(gobj);
}

/* [state 3 Coll] +0x61C (39 words); [state 4 Coll] +0x844 is a branch to it. Touching a wall,
 * ceiling or floor rebounds; otherwise grab a ledge if one is in reach. */
static void ftKbKx_SpecialNDash_Coll(HSD_GObj* gobj)
{
    Fighter* fp = GET_FIGHTER(gobj);
    ftSonic_DatAttrs* da = ftKbKx_DA(fp);
    ftCollisionBox box;
    u32 env;

    if (da == NULL) {
        return;
    }
    box = ftCollisionBox_FromDisc(&da->coll_box);
    ft_800824A0(gobj, &box);
    env = fp->coll_data.env_flags;
    if (env & (Collide_WallMask | Collide_CeilingMask | Collide_FloorMask)) {
        ftKbKx_SpecialNRebound_Enter(gobj, env);
        return;
    }
    if (ftCliffCommon_80081298(gobj)) {
        ftCliffCommon_80081370(gobj);
    }
}

/* ---- Attack --------------------------------------------------------------------------------- */

/* +0x1094 (59 words), SpecialNAttack_Enter (the Sonic hat's +0x10F0 word for word but the call
 * distances and the constant addresses). */
static void ftKbKx_SpecialNAttack_Enter(HSD_GObj* gobj, float target_x, float target_y)
{
    Fighter* fp = GET_FIGHTER(gobj);
    ftSonic_DatAttrs* da = ftKbKx_DA(fp);
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

    mu_ak_kirby_state_change(gobj, ftKbKx_State_SpecialNAttack, Ft_MF_None, 0.0F,
                             da->specialn_attack_anim_rate, 0.0F);
    fp->accessory4_cb = ftKbKx_GFXSpinAndTrailVelocityDirection;
    fp->deal_dmg_cb = ftKbKx_SpecialNHit_Enter;
}

/* [state 4 Anim] +0x6B8 (97 words; the Sonic hat's +0x6D8 with other stack slots). Keep the
 * dash at full speed along its angle; stop when the time runs out or the target point is within
 * one frame's travel (and, after the time ran out, a second Cancel when it is near, as Sonic's). */
static void ftKbKx_SpecialNAttack_Anim(HSD_GObj* gobj)
{
    Fighter* fp = GET_FIGHTER(gobj);
    ftSonic_DatAttrs* da = ftKbKx_DA(fp);
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
        ftKbKx_SpecialNCancel_Enter(gobj);
    }

    dist2 = ftSn_Vec3_DistSquared(&fp->cur_pos, &mv->target);
    if (!(dist2 <= da->specialn_attack_speed * da->specialn_attack_speed)) {
        return;
    }
    ftKbKx_SpecialNCancel_Enter(gobj);
}

/* ---- Cancel --------------------------------------------------------------------------------- */

/* SpecialNCancel_Enter, which the hat's code has inside +0x59C and (twice) +0x6B8. */
static void ftKbKx_SpecialNCancel_Enter(HSD_GObj* gobj)
{
    Fighter* fp = GET_FIGHTER(gobj);

    mu_ak_kirby_state_change(gobj, ftKbKx_State_SpecialNCancel, Ft_MF_None, 0.0F, 1.0F, 0.0F);
    fp->self_vel.y = 0.0F;
    fp->cmd_vars[0] = 0;
}

/* [state 5 Phys] +0x8A8 (21 words) */
static void ftKbKx_SpecialNCancel_Phys(HSD_GObj* gobj)
{
    Fighter* fp = GET_FIGHTER(gobj);
    ftSonic_DatAttrs* da = ftKbKx_DA(fp);

    if (da == NULL) {
        return;
    }
    ft_80084DB0(gobj);
    ftCommon_CalcSelfAccel_Deaccel(fp, da->specialn_cancel_air_decel);
}

/* [state 5 Coll] +0x8FC (44 words) */
static void ftKbKx_SpecialNCancel_Coll(HSD_GObj* gobj)
{
    Fighter* fp = GET_FIGHTER(gobj);

    if (ft_CheckGroundAndLedge(gobj, (int) fp->facing_dir)) {
        ftKbKx_SpecialNLanding_Enter(gobj);
    }
}

/* ---- Landing -------------------------------------------------------------------------------- */

/* SpecialNLanding_Enter, which the hat's code has inside +0x8FC and +0xB14. */
static void ftKbKx_SpecialNLanding_Enter(HSD_GObj* gobj)
{
    Fighter* fp = GET_FIGHTER(gobj);

    ftCommon_8007D6A4(fp);
    mu_ak_kirby_state_change(gobj, ftKbKx_State_SpecialNLanding, Ft_MF_None, 0.0F, 1.0F, 0.0F);
    fp->self_vel.y = 0.0F;
    fp->cmd_vars[0] = 0;
}

/* [state 6 Phys] +0xA0C (20 words, word for word the Sonic hat's +0xA34) */
static void ftKbKx_SpecialNLanding_Phys(HSD_GObj* gobj)
{
    Fighter* fp = GET_FIGHTER(gobj);
    ftSonic_DatAttrs* da = ftKbKx_DA(fp);

    if (da == NULL) {
        return;
    }
    ftCommon_CalcGroundAccel_Deaccel(fp, da->specialn_landing_decel);
    ftCommon_SetSelfMovementFromGroundedMovement(gobj);
}

/* ---- Rebound / Hit -------------------------------------------------------------------------- */

/* +0xF30 (61 words), SpecialNRebound_Enter (the Sonic hat's +0xF80): bounce back off whatever
 * was touched. A wall or ceiling (flags 0x6FFF) reverses and damps x, a floor (0x18000) only damps
 * it; then the clamps of ftSn_SpecialN_ClampReboundVel with y flipped. */
static void ftKbKx_SpecialNRebound_Enter(HSD_GObj* gobj, u32 env_flags)
{
    Fighter* fp = GET_FIGHTER(gobj);
    ftSonic_DatAttrs* da = ftKbKx_DA(fp);
    static int logged;

    if (da == NULL) {
        return;
    }
    mu_ak_kirby_state_change(gobj, ftKbKx_State_SpecialNRebound, Ft_MF_None, 0.0F, 1.0F, 0.0F);
    if (env_flags & (Collide_WallMask | Collide_CeilingMask)) {
        fp->self_vel.x = fp->self_vel.x * -da->specialn_rebound_mul_x;
    } else if (env_flags & Collide_FloorMask) {
        fp->self_vel.x = fp->self_vel.x * da->specialn_rebound_mul_x;
    }
    ftSn_SpecialN_ClampReboundVel(fp, da, -fp->self_vel.y);
    if (logged < FTKBKX_LOG) {
        logged++;
        OSReport("[ak] Kirby (Knuckles) rebound: touched %08X, velocity (%.2f, %.2f)\n",
                 (unsigned) env_flags, fp->self_vel.x, fp->self_vel.y);
    }
}

/* +0x15F0 (2 words), SpecialNRebound_OnEnter: the miss dive's deal_dmg_cb (it hit a shield or
 * an opponent). */
static void ftKbKx_SpecialNRebound_OnEnter(HSD_GObj* gobj)
{
    ftKbKx_SpecialNRebound_Enter(gobj, 0);
}

/* +0x1464 (56 words), SpecialNHit_Enter: the homing dash's deal_dmg_cb (the Sonic hat's +0x14B4).
 * Pops up at least 2 units a frame (+0xC54). */
static void ftKbKx_SpecialNHit_Enter(HSD_GObj* gobj)
{
    Fighter* fp = GET_FIGHTER(gobj);
    ftSonic_DatAttrs* da = ftKbKx_DA(fp);
    float vel_y;

    if (da == NULL) {
        return;
    }
    mu_ak_kirby_state_change(gobj, ftKbKx_State_SpecialNHit, Ft_MF_None, 0.0F, 1.0F, 0.0F);
    vel_y = fp->self_vel.y;
    if (vel_y < 0.0F) {
        vel_y = -vel_y;
    }
    if (vel_y < 2.0F) {
        vel_y = 2.0F;
    }
    fp->self_vel.y = vel_y;
    ftSn_SpecialN_ClampReboundVel(fp, da, vel_y);
}

/* [state 7 Phys, 8 Phys] +0xAC0 (21 words) */
static void ftKbKx_SpecialNRebound_Phys(HSD_GObj* gobj)
{
    Fighter* fp = GET_FIGHTER(gobj);
    ftSonic_DatAttrs* da = ftKbKx_DA(fp);

    if (da == NULL) {
        return;
    }
    ftCommon_CalcSelfAccel_Deaccel(fp, da->specialn_rebound_air_decel);
    ftCommon_Fall(fp, da->specialn_rebound_gravity, da->specialn_rebound_terminal_vel);
}

/* [state 7 Coll, 8 Coll] +0xB14 (38 words) */
static void ftKbKx_SpecialNRebound_Coll(HSD_GObj* gobj)
{
    if (ft_80081D0C(gobj)) {
        ftKbKx_SpecialNLanding_Enter(gobj);
    }
}

/* ---- the tables ----------------------------------------------------------------------------- */

/* [move_logic] +0x130 (nine entries of 0x20 bytes): the Sonic hat's table, the same ftcmd
 * indexes, words +4 (0x00340111) and +8 (move id 0x12), which KirbyStateChange does not read.
 * The callbacks that are word for word the Sonic hat's and, through it (SONIC.md), the same
 * routines as Sonic's own are his functions:
 *   IASA and Phys of states 0 to 4 (+0x49C, +0x4A0, +0x534, +0x538, +0x614, +0x618, +0x83C,
 *     +0x840): empty;
 *   [state 0 Coll, 1 Coll] +0x4A4: ft_80081D0C;
 *   [state 5 Anim] +0x848 and [state 7 Anim, 8 Anim] +0xA60: the end of the animation falls;
 *   [state 5 IASA] +0x894 and [state 7 IASA, 8 IASA] +0xAAC: the script's cmd_vars[0] opens the
 *     fall's interrupts;
 *   [state 6 Anim] +0x9AC, [state 6 IASA] +0x9F8, [state 6 Coll] +0xA5C: the landing. */
static const MotionState ftKbKx_MotionStateTable[ftKbKx_State_Count] = {
    {
        /* state 0, ftcmd 0 "SnSpecialNStart" */
        0,
        0x00340111,
        0x12 << 24,
        ftKbKx_SpecialNStart_Anim,
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
        ftKbKx_SpecialNStart_Anim,
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
        ftKbKx_SpecialNCharge_Anim,
        ftSn_SpecialNCharge_IASA,
        ftSn_SpecialNCharge_Phys,
        ftKbKx_SpecialNCharge_Coll,
        ftCamera_UpdateCameraBox,
    },
    {
        /* state 3, ftcmd 3 "SnSpecialNSpin" */
        3,
        0x00340111,
        0x12 << 24,
        ftKbKx_SpecialNAttackMiss_Anim,
        ftSn_SpecialNAttackMiss_IASA,
        ftSn_SpecialNAttackMiss_Phys,
        ftKbKx_SpecialNDash_Coll,
        ftCamera_UpdateCameraBox,
    },
    {
        /* state 4, ftcmd 3 "SnSpecialNSpin" */
        3,
        0x00340111,
        0x12 << 24,
        ftKbKx_SpecialNAttack_Anim,
        ftSn_SpecialNAttack_IASA,
        ftSn_SpecialNAttack_Phys,
        ftKbKx_SpecialNDash_Coll,
        ftCamera_UpdateCameraBox,
    },
    {
        /* state 5, ftcmd 4 "SnSpecialNCancel" */
        4,
        0x00340111,
        0x12 << 24,
        ftSn_SpecialNCancel_Anim,
        ftSn_SpecialNCancel_IASA,
        ftKbKx_SpecialNCancel_Phys,
        ftKbKx_SpecialNCancel_Coll,
        ftCamera_UpdateCameraBox,
    },
    {
        /* state 6, ftcmd 7 "SnSpecialNLanding" */
        7,
        0x00340111,
        0x12 << 24,
        ftSn_SpecialNLanding_Anim,
        ftSn_SpecialNLanding_IASA,
        ftKbKx_SpecialNLanding_Phys,
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
        ftKbKx_SpecialNRebound_Phys,
        ftKbKx_SpecialNRebound_Coll,
        ftCamera_UpdateCameraBox,
    },
    {
        /* state 8, ftcmd 5 "SnSpecialNHit" */
        5,
        0x00340111,
        0x12 << 24,
        ftSn_SpecialNRebound_Anim,
        ftSn_SpecialNRebound_IASA,
        ftKbKx_SpecialNRebound_Phys,
        ftKbKx_SpecialNRebound_Coll,
        ftCamera_UpdateCameraBox,
    },
};

/* The kbFunction exports of PlKbCpKx.dat (0 to 4 and 6; it has no export 5 and no export 7). */
const MuAkKirbyCopy ftKbKx_Copy = {
    .on_swallow = ftKbKx_OnSwallow,
    .on_lose = ftKbKx_OnLose,
    .special_n = ftKbKx_SpecialNStart_Enter,
    .special_air_n = ftKbKx_SpecialNStart_Enter,
    .on_hurt = ftKbKx_OnHurt,
    .init_items = NULL,
    .move_logic = ftKbKx_MotionStateTable,
    .move_logic_count = ftKbKx_State_Count,
    .on_frame = NULL,
};
