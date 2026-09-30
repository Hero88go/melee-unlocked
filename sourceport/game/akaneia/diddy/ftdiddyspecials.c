/* Akaneia's Diddy Kong, Special S: Monkey Flip.
 *
 * Diddy flips forward (AirSJump) with a grab box armed (ftCommon_8007E2D0). A caught fighter
 * becomes the "Taro": it is put into Diddy's own victim states (369..374), which run on the
 * victim with Diddy's animations through a borrowed common-state slot (ftDd_SpecialS_TaroChange).
 * Latched on (SStick), Diddy either attacks (A/B: SStickAttack, a kick off the victim) or jumps
 * away (SStickJump). The script's throw flag b3 releases the victim with Diddy's throw hitbox
 * (ftDd_SpecialS_ThrowDetach). If the victim mashes out on the ground, both are knocked apart
 * by Diddy's second throw hitbox (ftDd_SpecialS_BreakDetach).
 * Pressing A or B during the flip without a catch kicks instead (AirSKick). */
#include "ftdiddy.h"

#include <melee/ft/fighter.h>
#include <melee/ft/ft_081B.h>
#include <melee/ft/ftanim.h>
#include <melee/ft/ftcliffcommon.h>
#include <melee/ft/ftcoll.h>
#include <melee/ft/ftcommon.h>
#include <melee/ft/ftparts.h>
#include <melee/ft/inlines.h>
#include <melee/ft/kinds/ftCommon/ftCo_Attack100.h>
#include <melee/ft/kinds/ftCommon/ftCo_Damage.h>
#include <melee/ft/kinds/ftCommon/ftCo_Fall.h>
#include <melee/ft/kinds/ftCommon/ftCo_FallSpecial.h>
#include <melee/ft/kinds/ftCommon/ftCo_Jump.h>
#include <melee/ft/kinds/ftCommon/ftCo_Landing.h>
#include <melee/ft/kinds/ftCommon/ftCo_Lift.h>
#include <melee/ft/kinds/ftCommon/ftCo_Throw.h>
#include <melee/ft/kinds/ftCommon/ftCo_Thrown.h>
#include <melee/gm/gmvs.h>
#include <melee/lb/lb_00B0.h>
#include <melee/mp/mpcoll.h>
#include <melee/pl/player.h>
#include <sysdolphin/baselib/controller.h>
#include <sysdolphin/baselib/jobj.h>

/* Every call PlDd.dat makes to ftCommon_8007E2F4 (MexTK: Fighter_SetGrabbableFlag) passes the
 * GObj where the function wants the Fighter, a prototype error in MexTK's fighter.h. On the
 * console the 16-bit store lands at gobj+0x1A6A, past the GObj, and the fighter's grab mask
 * (fp->x1A6A) is left alone. The native port keeps what players get on the console: no change.
 * Define MU_AK_DIDDY_FIX_GRAB_MASK to apply the intended fp->x1A6A = val instead. */
static inline void ftDd_SpecialS_SetGrabMask(HSD_GObj* gobj, s16 val)
{
#ifdef MU_AK_DIDDY_FIX_GRAB_MASK
    ftCommon_8007E2F4(GET_FIGHTER(gobj), val);
#else
    (void) gobj;
    (void) val;
#endif
}

static inline void ftDd_SpecialS_AccessoryUpdatePos(Fighter* fp);

/* ------------------------------------------------------------------------------------------ */
/* Victim handling                                                                            */
/* ------------------------------------------------------------------------------------------ */

/* Fighter_TaroStateChange: put @p victim into state @p msid of @p thrower's tables. The victim
 * has no row for Diddy's states, so for the duration of one Fighter_ChangeMotionState the
 * shared common row of ftCo_MS_CaptureCaptain is replaced with Diddy's row. The victim ends up
 * with motion_id CaptureCaptain, Diddy's callbacks and Diddy's animation (the thrower gobj is
 * passed so the animation comes from Diddy's set). The console ignores the frame, speed and
 * blend its callers pass in registers: every change starts at frame 0, speed 1, no blend. */
static void ftDd_SpecialS_TaroChange(HSD_GObj* victim, FtMotionId msid, HSD_GObj* thrower)
{
    Fighter* tfp = GET_FIGHTER(thrower);
    MotionState* entry;
    MotionState saved;

    if (msid < tfp->x18) {
        entry = &tfp->x1C_actionStateList[msid];
    } else {
        entry = &tfp->x20_actionStateList[msid - tfp->x18];
    }
    saved = tfp->x1C_actionStateList[ftCo_MS_CaptureCaptain];
    tfp->x1C_actionStateList[ftCo_MS_CaptureCaptain] = *entry;
    Fighter_ChangeMotionState(victim, ftCo_MS_CaptureCaptain, Ft_MF_None, 0.0F, 1.0F, 0.0F,
                              thrower);
    tfp->x1C_actionStateList[ftCo_MS_CaptureCaptain] = saved;
}

/* SpecialS_ThrowInit: bind Diddy's @p thrower_bone to the victim's @p victim_bone (the same
 * constraint the common throws set up). */
static void ftDd_SpecialS_ThrowInit(HSD_GObj* victim, Fighter_Part victim_bone,
                                    HSD_GObj* thrower, Fighter_Part thrower_bone)
{
    Fighter* tfp = GET_FIGHTER(thrower);
    Fighter* vfp;
    HSD_JObj* vjoint;
    HSD_JObj* tjoint;

    if (tfp->x2226_b2) {
        return;
    }
    vfp = GET_FIGHTER(victim);
    vjoint = vfp->parts[ftParts_GetBoneIndex(vfp, victim_bone)].joint;
    tjoint = tfp->parts[ftParts_GetBoneIndex(tfp, thrower_bone)].joint;

    tjoint->rotate.x = 0.0F;
    tjoint->rotate.y = 0.0F;
    tjoint->rotate.z = 0.0F;
    tjoint->rotate.w = 0.0F;
    HSD_JObjClearFlags(tjoint, JOBJ_USE_QUATERNION);
    tfp->x2174 = tjoint->translate;
    lb_8000C1C0(tjoint, vjoint);
    tfp->x2226_b2 = true;
}

/* Undo ThrowInit's constraint and put the held fighter @p held where the release point is, the
 * block the common release (ftCo_800DDDE4) runs with its "arg" set. Diddy's version skips the
 * bone's translate restore. */
static void ftDd_SpecialS_PlaceHeld(Fighter* holder, Fighter* held, Vec3* pos,
                                    Fighter_Part bone)
{
    CollData* cd = &held->coll_data;
    HSD_JObj* jobj;

    held->x2226_b2 = false;
    lb_8000C390(held->parts[ftParts_GetBoneIndex(held, bone)].joint);

    pos->x += held->facing_dir * (held->x1A70.z * held->x34_scale.y);
    pos->y += held->x1A70.y * held->x34_scale.y;
    pos->z = 0.0F;

    jobj = GET_JOBJ(held->gobj);
    cd->last_pos.x = holder->cur_pos.x;
    cd->last_pos.y =
        holder->cur_pos.y + 0.5 * (holder->coll_data.ecb.top.y + holder->coll_data.ecb.bottom.y);
    cd->last_pos.z = holder->cur_pos.z;
    cd->x130_flags |= CollData_X130_Clear;
    cd->cur_pos = *pos;
    jobj->translate = *pos;
    ftCommon_UnlockECB(held);
    mpColl_800471F8(cd);
    held->cur_pos = cd->cur_pos;
    jobj->translate = held->cur_pos;
    HSD_JObjSetMtxDirtySub(jobj);
}

/* Fill @p fp's damage from @p hit dealt by @p attacker and knock it back. */
static void ftDd_SpecialS_ApplyHit(Fighter* fp, Fighter* attacker, HitCapsule* hit,
                                   float facing, Vec3* pos, float damage)
{
    ftColl_80076640(fp, &damage);
    fp->dmg.kb_applied =
        ftColl_80079AB0(fp, hit, hit->unk_count, gm_8016B248(),
                        Player_GetAttackRatio(attacker->player_idx),
                        Player_GetDefenseRatio(fp->player_idx), p_ftCommonData->x10C);
    fp->dmg.x1848_kb_angle = hit->kb_angle;
    fp->dmg.facing_dir_1 = facing;
    fp->dmg.x184c_damaged_hurtbox = 1;
    fp->dmg.x1854_collpos = *pos;
    fp->dmg.x1860_element = hit->element;
}

/* SpecialS_ThrowDetach: release the victim with Diddy's throw hitbox (xDF4[0]). Diddy's copy of
 * ftCo_800DDDE4: it takes the bones as arguments, spends all of the victim's jumps but one, and
 * does not make the held fighter airborne. */
static void ftDd_SpecialS_ThrowDetach(HSD_GObj* gobj, HSD_GObj* victim, Fighter_Part pos_bone,
                                      Fighter_Part held_bone)
{
    Fighter* fp = GET_FIGHTER(gobj);
    Fighter* fp2 = GET_FIGHTER(victim);
    HitCapsule* hit = &fp->xDF4[0];
    Fighter* holder;
    Fighter* held;
    Vec3 pos;

    if (fp->x221B_b7) {
        holder = fp2;
        held = fp;
    } else {
        holder = fp;
        held = fp2;
    }
    fp->x1A5C = NULL;
    fp->victim_gobj = NULL;
    fp->x221B_b5 = false;
    fp->x221B_b7 = false;
    ftColl_8007891C(gobj, victim, hit->damage);
    if (fp2->x1988 != 0) {
        ftColl_8007B62C(victim, 0);
    }
    lb_8000B1CC(holder->parts[ftParts_GetBoneIndex(holder, pos_bone)].joint, NULL, &pos);

    ftDd_SpecialS_ApplyHit(fp2, fp, hit, -fp->facing_dir, &pos,
                           ftColl_8007B868(victim) == 0 ? hit->damage : 0.0F);
    ftColl_80078710(gobj, victim, &fp2->dmg.facing_dir_1);
    ftCommon_8007EE0C(fp, hit->damage);
    ftCo_Damage_CalcKnockback(fp2);
    fp2->x1968_jumpsUsed = 1;

    if (held->x2226_b2) {
        ftDd_SpecialS_PlaceHeld(holder, held, &pos, held_bone);
    }
    fp2->x1A5C = NULL;
    fp2->victim_gobj = NULL;
    fp2->x221B_b7 = false;
}

/* SpecialS_BreakDetach: the victim broke free. Diddy's second throw hitbox (xDF4[1]) hits both
 * of them, away from each other, and both enter damage. */
static void ftDd_SpecialS_BreakDetach(HSD_GObj* gobj, HSD_GObj* victim, Fighter_Part pos_bone,
                                      Fighter_Part held_bone)
{
    Fighter* fp = GET_FIGHTER(gobj);
    Fighter* vfp = GET_FIGHTER(victim);
    HitCapsule* hit = &fp->xDF4[1];
    Fighter* holder;
    Fighter* held;
    Vec3 pos;

    if (fp->x221B_b7) {
        holder = vfp;
        held = fp;
    } else {
        holder = fp;
        held = vfp;
    }
    fp->x1A5C = NULL;
    fp->victim_gobj = NULL;
    fp->x221B_b5 = false;
    fp->x221B_b7 = false;
    lb_8000B1CC(holder->parts[ftParts_GetBoneIndex(holder, pos_bone)].joint, NULL, &pos);

    ftDd_SpecialS_ApplyHit(fp, fp, hit, fp->facing_dir, &pos,
                           ftColl_8007B868(gobj) == 0 ? hit->damage : 0.0F);
    ftColl_80078710(gobj, victim, &fp->dmg.facing_dir_1);
    ftCo_Damage_CalcKnockback(fp);
    ftCo_8008E908(gobj, 0.0F);

    if (held->x2226_b2) {
        ftDd_SpecialS_PlaceHeld(holder, held, &pos, held_bone);
    }

    ftDd_SpecialS_ApplyHit(vfp, fp, hit, -fp->facing_dir, &pos,
                           ftColl_8007B868(victim) == 0 ? hit->damage : 0.0F);
    ftColl_80078710(gobj, victim, &vfp->dmg.facing_dir_1);
    ftCo_Damage_CalcKnockback(vfp);
    ftCo_8008E908(victim, 0.0F);

    vfp->x1A5C = NULL;
    vfp->victim_gobj = NULL;
    vfp->x221B_b7 = false;
}

/* SpecialS_ThrowGroundCorrect (victim accessory callback): keep the victim's body on the ground
 * line while its animation is Diddy's, from the fighter's two foot bones (ftData x58). */
static void ftDd_SpecialS_ThrowGroundCorrect(HSD_GObj* gobj)
{
    Fighter* fp = GET_FIGHTER(gobj);
    HSD_JObj* trans = fp->parts[FtPart_TransN].joint;
    ftData_x58_t* feet;
    Vec3 foot0;
    Vec3 foot1;
    float height;
    float mid;

    trans->translate.y = 0.0F;
    HSD_JObjSetMtxDirtySub(GET_JOBJ(gobj));

    feet = DP(fp->ft_data->x58);
    lb_8000B1CC(fp->parts[feet->x11].joint, NULL, &foot1);
    lb_8000B1CC(fp->parts[feet->x10].joint, NULL, &foot0);
    mid = (foot1.y + foot0.y) * 0.5F;
    height = (fp->kind == Ft_Kind_Koopa) ? 0.65F : feet->x18;
    trans->translate.y = height * fp->x34_scale.y + (fp->cur_pos.y - mid);
    HSD_JObjSetMtxDirtySub(GET_JOBJ(gobj));
}

/* ------------------------------------------------------------------------------------------ */
/* Victim state entries                                                                       */
/* ------------------------------------------------------------------------------------------ */

/* SpecialSStickWaitTaro_Enter */
static void ftDd_SpecialSStickWaitTaro_Enter(HSD_GObj* victim)
{
    Fighter* vfp = GET_FIGHTER(victim);

    vfp->x221B_b5 = false;
    ftDd_MV(vfp)->taro.mash_lock = 0;
    ftDd_SpecialS_TaroChange(victim, ftDd_MS_SpecialSStickWaitTaro, vfp->victim_gobj);
    ftCommon_8007F824(victim);
    ftDd_SpecialS_SetGrabMask(victim, 0x1FF);
    vfp->accessory1_cb = ftDd_SpecialS_ThrowGroundCorrect;
}

/* SpecialAirSStickWaitTaro_Enter */
static void ftDd_SpecialAirSStickWaitTaro_Enter(HSD_GObj* victim)
{
    Fighter* vfp = GET_FIGHTER(victim);

    ftDd_SpecialS_TaroChange(victim, ftDd_MS_SpecialAirSStickWaitTaro, vfp->victim_gobj);
    ftCommon_8007F824(victim);
    ftDd_SpecialS_SetGrabMask(victim, 0x1FF);
}

/* The attack and jump victim entries share one shape. */
static void ftDd_SpecialS_TaroEnter(HSD_GObj* victim, FtMotionId msid, bool grounded)
{
    Fighter* vfp = GET_FIGHTER(victim);

    if (grounded) {
        vfp->x221B_b5 = false;
    }
    ftDd_SpecialS_TaroChange(victim, msid, vfp->victim_gobj);
    ftCommon_8007F824(victim);
    if (vfp->x221B_b5) {
        vfp->accessory1_cb = ftCo_800DB464;
    }
    ftDd_SpecialS_SetGrabMask(victim, 0x1FF);
    ftAnim_8006EBA4(victim);
    if (grounded) {
        vfp->accessory1_cb = ftDd_SpecialS_ThrowGroundCorrect;
    }
}

/* SpecialSStick_OnVictimGrabbed (grabbed_cb): @p victim was caught by @p thrower. */
static void ftDd_SpecialSStick_OnVictimGrabbed(HSD_GObj* victim, HSD_GObj* thrower)
{
    Fighter* vfp = GET_FIGHTER(victim);
    Fighter* tfp = GET_FIGHTER(thrower);
    ftDd_DatAttrs* tda = tfp->dat_attrs_backup;
    ftCommonData* cd = p_ftCommonData;
    float timer;

    ftCommon_8007DB58(victim);
    ftCo_8009750C(victim);
    ftCo_800DD168(victim);
    vfp->victim_gobj = thrower;
    vfp->x1A5C = thrower;
    vfp->x221B_b5 = false;
    vfp->x221B_b7 = true;
    vfp->facing_dir = -tfp->facing_dir;

    /* The common grab timer (as ftCo_CapturePulled), capped by Diddy's attribute. */
    timer = cd->x360 * (cd->x364 - (float) (Player_80033BB8(vfp->player_idx) + 1));
    timer += cd->x358 * (cd->x35C - (float) Player_GetHandicap(vfp->player_idx)) + cd->x354;
    timer += cd->x368 * vfp->dmg.x1830_percent;
    if (timer > tda->specials_grab_timer_max) {
        timer = tda->specials_grab_timer_max;
    }
    ftCommon_InitGrab(vfp, false, timer);

    if (vfp->ground_or_air == GA_Ground) {
        ftDd_SpecialSStickWaitTaro_Enter(victim);
    } else {
        ftDd_SpecialAirSStickWaitTaro_Enter(victim);
    }
    if (vfp->x221B_b5) {
        vfp->accessory1_cb = ftCo_800DB464;
    }
    ftAnim_8006EBA4(victim);
    ftCommon_8007E2FC(victim);
    ftDd_SpecialS_ThrowGroundCorrect(victim);
}

/* Victim ground/air swaps. */
static void ftDd_SpecialS_TaroToAir(HSD_GObj* victim, FtMotionId msid)
{
    Fighter* vfp = GET_FIGHTER(victim);

    ftDd_SpecialS_TaroChange(victim, msid, vfp->victim_gobj);
    ftDd_SpecialS_SetGrabMask(victim, 0x1FF);
    ftCommon_8007D5D4(vfp);
}

/* Order differs between the two console copies (wait: mask first; jump: knockback hook first);
 * the mask does nothing either way (see ftDd_SpecialS_SetGrabMask). */
static void ftDd_SpecialS_TaroToGround(HSD_GObj* victim, FtMotionId msid)
{
    Fighter* vfp = GET_FIGHTER(victim);

    ftDd_SpecialS_TaroChange(victim, msid, vfp->victim_gobj);
    ftDd_SpecialS_SetGrabMask(victim, 0x1FF);
    ftCommon_8007F824(victim);
    vfp->self_vel.y = 0.0F;
    ftCommon_8007D6A4(vfp);
    vfp->accessory1_cb = ftDd_SpecialS_ThrowGroundCorrect;
}

/* ------------------------------------------------------------------------------------------ */
/* Diddy's entries                                                                            */
/* ------------------------------------------------------------------------------------------ */

/* SpecialSStart_Enter */
void ftDd_SpecialS_Enter(HSD_GObj* gobj)
{
    Fighter* fp = GET_FIGHTER(gobj);

    Fighter_ChangeMotionState(gobj, ftDd_MS_SpecialSStart, Ft_MF_None, 0.0F, 1.0F, 0.0F, NULL);
    ftAnim_8006EBA4(gobj);
    fp->gr_vel = 0.0F;
    fp->xF0_ground_kb_vel = 0.0F;
    fp->self_vel.x = 0.0F;
    fp->self_vel.y = 0.0F;
    ftDd_MV(fp)->specials.jumps_used = fp->x1968_jumpsUsed;
}

/* SpecialAirSStart_Enter */
void ftDd_SpecialAirS_Enter(HSD_GObj* gobj)
{
    Fighter* fp = GET_FIGHTER(gobj);

    Fighter_ChangeMotionState(gobj, ftDd_MS_SpecialAirSStart, Ft_MF_None, 0.0F, 1.0F, 0.0F,
                              NULL);
    ftAnim_8006EBA4(gobj);
    fp->self_vel.x = 0.0F;
    fp->self_vel.y = 0.0F;
    ftDd_MV(fp)->specials.jumps_used = fp->x1968_jumpsUsed;
}

static void ftDd_SpecialSStick_Enter(HSD_GObj* gobj);

/* SpecialAirSJump_Enter: the flip, with the grab armed. Uses up the air jumps. */
static void ftDd_SpecialAirSJump_Enter(HSD_GObj* gobj)
{
    Fighter* fp = GET_FIGHTER(gobj);
    ftDd_DatAttrs* da = fp->dat_attrs_backup;

    Fighter_ChangeMotionState(gobj, ftDd_MS_SpecialAirSJump, Ft_MF_None, 0.0F, 1.0F, 0.0F,
                              NULL);
    fp->self_vel.x = da->specials_flip_vel.x * fp->facing_dir;
    fp->self_vel.y = da->specials_flip_vel.y;
    ftCommon_8007E2D0(fp, 1, ftDd_SpecialSStick_Enter, NULL, ftDd_SpecialSStick_OnVictimGrabbed);
    fp->x1968_jumpsUsed = fp->co_attrs.max_jumps;
}

/* SpecialAirSFall_Enter: special fall with Diddy's side-B landing lag. */
static void ftDd_SpecialAirSFall_Enter(HSD_GObj* gobj)
{
    Fighter* fp = GET_FIGHTER(gobj);
    ftDd_DatAttrs* da = fp->dat_attrs_backup;

    ftCo_80096900(gobj, 0, 1, false, 1.0F, da->specials_landing_lag);
}

/* SpecialAirSKick_Enter */
static void ftDd_SpecialAirSKick_Enter(HSD_GObj* gobj)
{
    Fighter_ChangeMotionState(gobj, ftDd_MS_SpecialAirSKick, Ft_MF_None, 0.0F, 1.0F, 0.0F,
                              NULL);
}

/* SpecialSStick_UpdatePos (accessory4_cb): ride the victim. */
static void ftDd_SpecialSStick_UpdatePos(HSD_GObj* gobj)
{
    Fighter* fp = GET_FIGHTER(gobj);

    if (fp->x1A5C != NULL) {
        fp->cur_pos = GET_FIGHTER(fp->x1A5C)->cur_pos;
    }
}

static inline void ftDd_SpecialS_AccessoryUpdatePos(Fighter* fp)
{
    fp->accessory4_cb = ftDd_SpecialSStick_UpdatePos;
}

/* SpecialSStick_Enter (grab_cb): Diddy caught someone. The jump refund: if Diddy had jumps left
 * when the move began he gets all but one back, otherwise the snapshot is clamped. */
static void ftDd_SpecialSStick_Enter(HSD_GObj* gobj)
{
    Fighter* fp = GET_FIGHTER(gobj);

    ftDd_SpecialS_ThrowInit(fp->x1A5C, FtPart_XRotN, gobj, FtPart_XRotN);
    fp->x221B_b7 = true;
    Fighter_ChangeMotionState(gobj, ftDd_MS_SpecialSStick, Ft_MF_None, 0.0F, 1.0F, 0.0F, NULL);
    fp->x2222_b2 = true;
    ftDd_SpecialS_SetGrabMask(gobj, 0x1FF);
    ftCommon_8007E2FC(gobj);
    ftDd_SpecialS_AccessoryUpdatePos(fp);
    if (ftDd_MV(fp)->specials.jumps_used < fp->co_attrs.max_jumps) {
        fp->x1968_jumpsUsed = 1;
    } else {
        ftDd_MV(fp)->specials.jumps_used = fp->co_attrs.max_jumps;
    }
}

/* SpecialSStickAttack_Enter */
static void ftDd_SpecialSStickAttack_Enter(HSD_GObj* gobj)
{
    Fighter* fp = GET_FIGHTER(gobj);
    HSD_GObj* victim = fp->x1A5C;
    Fighter* vfp = GET_FIGHTER(victim);

    Fighter_ChangeMotionState(gobj, ftDd_MS_SpecialSStickAttack, Ft_MF_None, 0.0F, 1.0F, 0.0F,
                              NULL);
    ftDd_SpecialS_AccessoryUpdatePos(fp);
    if (vfp->ground_or_air == GA_Ground) {
        ftDd_SpecialS_TaroEnter(victim, ftDd_MS_SpecialSStickAttackTaro, true);
    } else {
        ftDd_SpecialS_TaroEnter(victim, ftDd_MS_SpecialAirSStickAttackTaro, false);
    }
}

/* SpecialSStickAttack2_Enter */
static void ftDd_SpecialSStickAttack2_Enter(HSD_GObj* gobj)
{
    Fighter* fp = GET_FIGHTER(gobj);

    fp->throw_flags_b3 = false;
    fp->throw_flags_b4 = false;
    Fighter_ChangeMotionState(gobj, ftDd_MS_SpecialSStickAttack2, Ft_MF_None, 0.0F, 1.0F, 0.0F,
                              NULL);
    ftDd_SpecialS_AccessoryUpdatePos(fp);
}

/* SpecialSStickJump_Enter */
static void ftDd_SpecialSStickJump_Enter(HSD_GObj* gobj)
{
    Fighter* fp = GET_FIGHTER(gobj);
    HSD_GObj* victim = fp->x1A5C;
    Fighter* vfp = GET_FIGHTER(victim);

    fp->throw_flags_b3 = false;
    fp->throw_flags_b4 = false;
    Fighter_ChangeMotionState(gobj, ftDd_MS_SpecialSStickJump, Ft_MF_None, 0.0F, 1.0F, 0.0F,
                              NULL);
    ftDd_SpecialS_AccessoryUpdatePos(fp);
    if (vfp->ground_or_air == GA_Ground) {
        ftDd_SpecialS_TaroEnter(victim, ftDd_MS_SpecialSStickJumpTaro, true);
    } else {
        ftDd_SpecialS_TaroEnter(victim, ftDd_MS_SpecialAirSStickJumpTaro, false);
    }
}

/* SpecialSStickJump2_Enter: the hop off the released victim. */
static void ftDd_SpecialSStickJump2_Enter(HSD_GObj* gobj)
{
    Fighter* fp = GET_FIGHTER(gobj);
    ftDd_DatAttrs* da = fp->dat_attrs_backup;

    fp->self_vel.x = da->specials_jump2_vel.x * fp->facing_dir;
    fp->self_vel.y = da->specials_jump2_vel.y;
    Fighter_ChangeMotionState(gobj, ftDd_MS_SpecialSStickJump2, Ft_MF_None, 0.0F, 1.0F, 0.0F,
                              NULL);
}

/* The Trans pair of the start. */
static void ftDd_SpecialAirSStart_Trans(HSD_GObj* gobj)
{
    Fighter* fp = GET_FIGHTER(gobj);

    Fighter_ChangeMotionState(gobj, ftDd_MS_SpecialAirSStart, ftDd_MF_SwapN, fp->cur_anim_frame,
                              fp->frame_speed_mul, fp->x8A4_animBlendFrames, NULL);
    ftCommon_8007D5D4(fp);
}

static void ftDd_SpecialSStart_Trans(HSD_GObj* gobj)
{
    Fighter* fp = GET_FIGHTER(gobj);

    Fighter_ChangeMotionState(gobj, ftDd_MS_SpecialSStart, ftDd_MF_SwapN, fp->cur_anim_frame,
                              fp->frame_speed_mul, fp->x8A4_animBlendFrames, NULL);
    ftCommon_8007D6A4(fp);
}

/* The throw flag release shared by SStickAttack2 and SStickJump. */
static bool ftDd_SpecialS_CheckRelease(HSD_GObj* gobj)
{
    Fighter* fp = GET_FIGHTER(gobj);
    HSD_GObj* victim;

    if (!fp->throw_flags_b3) {
        return false;
    }
    victim = fp->x1A5C;
    fp->throw_flags_b3 = false;
    ftDd_SpecialS_SetGrabMask(gobj, 0);
    if (victim == NULL) {
        return false;
    }
    ftDd_SpecialS_ThrowDetach(gobj, victim, FtPart_XRotN, FtPart_XRotN);
    ftCo_800DE7C0(victim, NULL, false);
    return true;
}

/* ------------------------------------------------------------------------------------------ */
/* Diddy's states                                                                             */
/* ------------------------------------------------------------------------------------------ */

void ftDd_SpecialSStart_Anim(HSD_GObj* gobj)
{
    Fighter* fp = GET_FIGHTER(gobj);

    if (!ftAnim_IsFramesRemaining(gobj)) {
        ftDd_SpecialAirSJump_Enter(gobj);
        ftCommon_8007D5D4(fp);
    }
}

void ftDd_SpecialSStart_IASA(HSD_GObj* gobj) {}

void ftDd_SpecialSStart_Phys(HSD_GObj* gobj) {}

void ftDd_SpecialSStart_Coll(HSD_GObj* gobj)
{
    if (!ft_80082708(gobj)) {
        ftDd_SpecialAirSStart_Trans(gobj);
    }
}

/* The console asks whether frames remain and ignores the answer: the latch holds until the
 * player acts or the victim breaks free. */
void ftDd_SpecialSStick_Anim(HSD_GObj* gobj)
{
    (void) ftAnim_IsFramesRemaining(gobj);
}

void ftDd_SpecialSStick_IASA(HSD_GObj* gobj)
{
    Fighter* fp = GET_FIGHTER(gobj);

    if (fp->input.pressed_buttons & (HSD_PAD_A | HSD_PAD_B)) {
        ftDd_SpecialSStickAttack_Enter(gobj);
    } else if (ftCo_Jump_GetInput(gobj)) {
        ftDd_SpecialSStickJump_Enter(gobj);
    }
}

void ftDd_SpecialSStick_Phys(HSD_GObj* gobj) {}

void ftDd_SpecialSStick_Coll(HSD_GObj* gobj) {}

void ftDd_SpecialSStickAttack_Anim(HSD_GObj* gobj)
{
    if (!ftAnim_IsFramesRemaining(gobj)) {
        ftDd_SpecialSStickAttack2_Enter(gobj);
    }
}

void ftDd_SpecialSStickAttack_IASA(HSD_GObj* gobj) {}

void ftDd_SpecialSStickAttack_Phys(HSD_GObj* gobj) {}

void ftDd_SpecialSStickAttack_Coll(HSD_GObj* gobj) {}

/* The kick off: on the release flag the victim is launched and Diddy bounces back. */
void ftDd_SpecialSStickAttack2_Anim(HSD_GObj* gobj)
{
    Fighter* fp = GET_FIGHTER(gobj);
    ftDd_DatAttrs* da = fp->dat_attrs_backup;

    if (ftDd_SpecialS_CheckRelease(gobj)) {
        fp->self_vel.x = da->specials_attack2_vel.x * fp->facing_dir;
        fp->self_vel.y = da->specials_attack2_vel.y;
    }
    if (!ftAnim_IsFramesRemaining(gobj)) {
        ftCo_Fall_Enter(gobj);
        fp->self_vel.y = 0.7F;
    }
}

void ftDd_SpecialSStickAttack2_IASA(HSD_GObj* gobj) {}

void ftDd_SpecialSStickAttack2_Phys(HSD_GObj* gobj) {}

void ftDd_SpecialSStickAttack2_Coll(HSD_GObj* gobj)
{
    Fighter* fp = GET_FIGHTER(gobj);

    if (fp->x1A5C != NULL) {
        return;
    }
    if (ft_80081D0C(gobj)) {
        ft_80082B1C(gobj);
    }
}

void ftDd_SpecialSStickJump_Anim(HSD_GObj* gobj)
{
    ftDd_SpecialS_CheckRelease(gobj);
    if (!ftAnim_IsFramesRemaining(gobj)) {
        ftDd_SpecialSStickJump2_Enter(gobj);
    }
}

void ftDd_SpecialSStickJump_IASA(HSD_GObj* gobj) {}

void ftDd_SpecialSStickJump_Phys(HSD_GObj* gobj) {}

void ftDd_SpecialSStickJump_Coll(HSD_GObj* gobj) {}

void ftDd_SpecialSStickJump2_Anim(HSD_GObj* gobj)
{
    if (!ftAnim_IsFramesRemaining(gobj)) {
        ftCo_Fall_Enter(gobj);
    }
}

void ftDd_SpecialSStickJump2_IASA(HSD_GObj* gobj) {}

void ftDd_SpecialSStickJump2_Phys(HSD_GObj* gobj)
{
    Fighter* fp = GET_FIGHTER(gobj);

    ftCommon_Fall(fp, fp->co_attrs.gravity, fp->co_attrs.terminal_velocity);
    ftCommon_CalcSelfAccel_Drift(fp);
}

void ftDd_SpecialSStickJump2_Coll(HSD_GObj* gobj)
{
    if (ft_80081D0C(gobj)) {
        ft_80082B1C(gobj);
    }
}

void ftDd_SpecialAirSStart_Anim(HSD_GObj* gobj)
{
    if (!ftAnim_IsFramesRemaining(gobj)) {
        ftDd_SpecialAirSJump_Enter(gobj);
    }
}

void ftDd_SpecialAirSStart_IASA(HSD_GObj* gobj) {}

void ftDd_SpecialAirSStart_Phys(HSD_GObj* gobj) {}

void ftDd_SpecialAirSStart_Coll(HSD_GObj* gobj)
{
    if (ft_80081D0C(gobj)) {
        ftDd_SpecialSStart_Trans(gobj);
    }
}

void ftDd_SpecialAirSJump_Anim(HSD_GObj* gobj)
{
    Fighter* fp = GET_FIGHTER(gobj);

    if (!ftAnim_IsFramesRemaining(gobj)) {
        fp->x1968_jumpsUsed = fp->co_attrs.max_jumps;
        ftDd_SpecialAirSFall_Enter(gobj);
    }
}

void ftDd_SpecialAirSJump_IASA(HSD_GObj* gobj)
{
    Fighter* fp = GET_FIGHTER(gobj);

    if (fp->input.pressed_buttons & (HSD_PAD_A | HSD_PAD_B)) {
        ftDd_SpecialAirSKick_Enter(gobj);
    }
}

void ftDd_SpecialAirSJump_Phys(HSD_GObj* gobj)
{
    Fighter* fp = GET_FIGHTER(gobj);

    ftCommon_Fall(fp, fp->co_attrs.gravity, fp->co_attrs.terminal_velocity);
    ftCommon_CalcSelfAccel_Deaccel(fp, fp->co_attrs.aerial_friction);
}

void ftDd_SpecialAirSJump_Coll(HSD_GObj* gobj)
{
    Fighter* fp = GET_FIGHTER(gobj);
    ftDd_DatAttrs* da = fp->dat_attrs_backup;

    if (ft_CheckGroundAndLedge(gobj, (int) fp->facing_dir)) {
        ftCo_LandingFallSpecial_Enter(gobj, false, da->specials_landing_lag);
    } else {
        ftCliffCommon_80081298(gobj);
    }
}

void ftDd_SpecialAirSKick_Anim(HSD_GObj* gobj)
{
    if (!ftAnim_IsFramesRemaining(gobj)) {
        ftDd_SpecialAirSFall_Enter(gobj);
    }
}

void ftDd_SpecialAirSKick_IASA(HSD_GObj* gobj) {}

void ftDd_SpecialAirSKick_Phys(HSD_GObj* gobj)
{
    Fighter* fp = GET_FIGHTER(gobj);

    ftCommon_Fall(fp, fp->co_attrs.gravity, fp->co_attrs.terminal_velocity);
    ftCommon_CalcSelfAccel_Deaccel(fp, fp->co_attrs.aerial_friction);
}

void ftDd_SpecialAirSKick_Coll(HSD_GObj* gobj)
{
    Fighter* fp = GET_FIGHTER(gobj);
    ftDd_DatAttrs* da = fp->dat_attrs_backup;

    if (ft_80081D0C(gobj)) {
        ftCo_LandingFallSpecial_Enter(gobj, false, da->specials_landing_lag);
    }
}

/* ------------------------------------------------------------------------------------------ */
/* Victim states (these run on the caught fighter)                                            */
/* ------------------------------------------------------------------------------------------ */

/* Victim fall in the air uses Diddy's attributes (the holder is vfp->victim_gobj). */
static void ftDd_SpecialS_TaroAirPhys(HSD_GObj* victim)
{
    Fighter* vfp = GET_FIGHTER(victim);
    ftDd_DatAttrs* tda = GET_FIGHTER(vfp->victim_gobj)->dat_attrs_backup;

    ftCommon_Fall(vfp, tda->specials_taro_gravity, tda->specials_taro_terminal_vel);
    ftCommon_CalcSelfAccel_Deaccel(vfp, vfp->co_attrs.aerial_friction);
}

/* Held on the ground: the grab timer runs down (faster when mashing, which also speeds up the
 * struggle animation for a while). At zero the victim breaks free. The air version has no
 * timer on the console. */
void ftDd_SpecialSStickWaitTaro_Anim(HSD_GObj* gobj)
{
    Fighter* fp = GET_FIGHTER(gobj);
    ftCommonData* cd = p_ftCommonData;
    bool mashed;

    fp->grab_timer -= 1.0F;
    mashed = ftCommon_GrabMash(fp, cd->x3A8);
    if (!(fp->grab_timer > 0.0F)) {
        ftDd_SpecialS_BreakDetach(fp->victim_gobj, gobj, FtPart_XRotN, FtPart_XRotN);
        return;
    }
    if (mashed) {
        ftDd_MV(fp)->taro.mash_lock = (int) cd->x3B0;
        ftAnim_SetAnimRate(gobj, cd->shouldered_anim_rate);
    } else if (ftDd_MV(fp)->taro.mash_lock > 0) {
        if (--ftDd_MV(fp)->taro.mash_lock == 0) {
            ftAnim_SetAnimRate(gobj, 1.0F);
        }
    }
}

void ftDd_SpecialSStickWaitTaro_IASA(HSD_GObj* gobj) {}

void ftDd_SpecialSStickWaitTaro_Phys(HSD_GObj* gobj) {}

void ftDd_SpecialSStickWaitTaro_Coll(HSD_GObj* gobj)
{
    Fighter* fp = GET_FIGHTER(gobj);

    if (fp->x2226_b2) {
        return;
    }
    if (!ft_80082708(gobj)) {
        ftDd_SpecialS_TaroToAir(gobj, ftDd_MS_SpecialAirSStickWaitTaro);
    }
}

void ftDd_SpecialAirSStickWaitTaro_Anim(HSD_GObj* gobj) {}

void ftDd_SpecialAirSStickWaitTaro_IASA(HSD_GObj* gobj) {}

void ftDd_SpecialAirSStickWaitTaro_Phys(HSD_GObj* gobj)
{
    ftDd_SpecialS_TaroAirPhys(gobj);
}

void ftDd_SpecialAirSStickWaitTaro_Coll(HSD_GObj* gobj)
{
    if (ft_80081D0C(gobj)) {
        ftDd_SpecialS_TaroToGround(gobj, ftDd_MS_SpecialSStickWaitTaro);
    }
}

void ftDd_SpecialSStickJumpTaro_Anim(HSD_GObj* gobj) {}

void ftDd_SpecialSStickJumpTaro_IASA(HSD_GObj* gobj) {}

void ftDd_SpecialSStickJumpTaro_Phys(HSD_GObj* gobj) {}

void ftDd_SpecialSStickJumpTaro_Coll(HSD_GObj* gobj)
{
    Fighter* fp = GET_FIGHTER(gobj);

    if (fp->x2226_b2) {
        return;
    }
    if (!ft_80082708(gobj)) {
        ftDd_SpecialS_TaroToAir(gobj, ftDd_MS_SpecialAirSStickJumpTaro);
    }
}

void ftDd_SpecialAirSStickJumpTaro_Anim(HSD_GObj* gobj) {}

void ftDd_SpecialAirSStickJumpTaro_IASA(HSD_GObj* gobj) {}

void ftDd_SpecialAirSStickJumpTaro_Phys(HSD_GObj* gobj)
{
    ftDd_SpecialS_TaroAirPhys(gobj);
}

void ftDd_SpecialAirSStickJumpTaro_Coll(HSD_GObj* gobj)
{
    if (ft_80081D0C(gobj)) {
        ftDd_SpecialS_TaroToGround(gobj, ftDd_MS_SpecialSStickJumpTaro);
    }
}

void ftDd_SpecialSStickAttackTaro_Anim(HSD_GObj* gobj) {}

void ftDd_SpecialSStickAttackTaro_IASA(HSD_GObj* gobj) {}

void ftDd_SpecialSStickAttackTaro_Phys(HSD_GObj* gobj) {}

/* The attack victim swaps to the JUMP victim states (so on the console too). */
void ftDd_SpecialSStickAttackTaro_Coll(HSD_GObj* gobj)
{
    Fighter* fp = GET_FIGHTER(gobj);

    if (fp->x2226_b2) {
        return;
    }
    if (!ft_80082708(gobj)) {
        ftDd_SpecialS_TaroToAir(gobj, ftDd_MS_SpecialAirSStickJumpTaro);
    }
}

void ftDd_SpecialAirSStickAttackTaro_Anim(HSD_GObj* gobj) {}

void ftDd_SpecialAirSStickAttackTaro_IASA(HSD_GObj* gobj) {}

void ftDd_SpecialAirSStickAttackTaro_Phys(HSD_GObj* gobj)
{
    ftDd_SpecialS_TaroAirPhys(gobj);
}

void ftDd_SpecialAirSStickAttackTaro_Coll(HSD_GObj* gobj)
{
    if (ft_80081D0C(gobj)) {
        ftDd_SpecialS_TaroToGround(gobj, ftDd_MS_SpecialSStickJumpTaro);
    }
}
