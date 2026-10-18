/* ACE's Sonic BM: the neutral special, a homing attack (states 343, 344, 364, 366, 367; 365 is
 * the retail LandingAir row), the file's own code.
 *
 * Wind up (343 from the ground, 344 in the air; both enter the air at once from the ground):
 * the first frame makes his afterimage chain (article 1). At its end: the dash (364) toward a
 * point 30 units ahead and 15 down, then every other player whose hip bone is nearer than the
 * best so far (45 at first) becomes the target, and each new target gets a mark (article 2).
 * The dash moves at 3 units a frame toward the target, follows a target player while he stays
 * within 45, and ends in a miss (367, special fall) within 2 units or after 30 frames. A hit
 * (his deal damage callback) bounces him up and back (366). Once per air time (fp+22F0). */
#include "sonicbm.h"

#include <dolphin/mtx.h>
#include <dolphin/os.h>

#include <math.h>

#include <melee/ft/fighter.h>
#include <melee/ft/ft_081B.h>
#include <melee/ft/ft_084E.h>
#include <melee/ft/ftanim.h>
#include <melee/ft/ftcommon.h>
#include <melee/ft/ftparts.h>
#include <melee/ft/inlines.h>
#include <melee/ft/kinds/ftCommon/ftCo_Fall.h>
#include <melee/ft/kinds/ftCommon/ftCo_FallSpecial.h>
#include <melee/ft/kinds/ftCommon/ftCo_Landing.h>
#include <melee/ft/types.h>
#include <melee/gm/gmvs.h>
#include <melee/lb/lb_00B0.h>
#include <melee/pl/player.h>
#include <sysdolphin/baselib/gobj.h>
#include <sysdolphin/baselib/jobj.h>

/* +05B4 (slot 4, specialn): once per air time. The first throw flag cleared, the flag set, into
 * the air, state 343. */
void ftSc_SpecialN_Enter(HSD_GObj* gobj)
{
    Fighter* fp = GET_FIGHTER(gobj);

    if (ftSc_FV(fp)->neutral_used == 1) {
        return;
    }
    fp->throw_flags_b0 = false;
    ftSc_FV(fp)->neutral_used = 1;
    ftCommon_8007D5D4(fp);
    Fighter_ChangeMotionState(gobj, ftSc_MS_SpecialN, Ft_MF_None, 0.0f, 1.0f, 0.0f, NULL);
    ftAnim_8006EBA4(gobj);
}

/* +063C (slot 5, specialairn): state 344. When the move was used already the press only sets
 * his facing from his model's Y rotation (gobj->hsd_obj, rotate.y over 0.0: right) and he stays
 * in the state he is in (kept from the file). */
void ftSc_SpecialAirN_Enter(HSD_GObj* gobj)
{
    Fighter* fp = GET_FIGHTER(gobj);

    if (ftSc_FV(fp)->neutral_used != 1) {
        fp->throw_flags_b0 = false;
        ftSc_FV(fp)->neutral_used = 1;
        Fighter_ChangeMotionState(gobj, ftSc_MS_SpecialAirN, Ft_MF_None, 0.0f, 1.0f, 0.0f,
                                  NULL);
        ftAnim_8006EBA4(gobj);
        return;
    }
    if (!(GET_JOBJ(gobj)->rotate.y <= 0.0f)) {
        fp->facing_dir = 1.0f;
    } else {
        fp->facing_dir = -1.0f;
    }
}

/* +3328: the hip bone position (bone of part 4) of the fighter of player `slot` into *out,
 * true when there is one that is not him and not his teammate in a team match. No check of the
 * target's state (a dead or sleeping fighter is a target). */
static bool ftSc_SpecialN_TargetPos(Fighter* fp, int slot, Vec3* out)
{
    HSD_GObj* target_gobj;
    Fighter* target;

    if (Player_GetEntity(slot) == NULL) {
        return false;
    }
    target_gobj = Player_GetEntity(slot);
    if (fp->player_idx == slot) {
        return false;
    }
    target = GET_FIGHTER(target_gobj);
    if (gm_8016B168() && target->team == fp->team) {
        return false;
    }
    lb_8000B1CC(target->parts[ftParts_GetBoneIndex(target, FtPart_HipN)].joint, NULL, out);
    return true;
}

/* +0D18: states 343 and 344, animation. While the first throw flag is clear: set it and make the
 * afterimage chain (article 1, link 0, parented to him, at his depth). At the end: the dash
 * (364) and its target. The default point is 30 units ahead (facing * 30 + x, fused in double)
 * and 15 down, best distance 45, timer 30, no slot; then players 0 to 3 are tried: each one
 * nearer than the best becomes the target, and a mark (article 2) is put at its x, y, depth 4.0
 * (one per new best, so a later nearer player adds a second mark). */
void ftSc_SpecialN_Anim(HSD_GObj* gobj)
{
    Fighter* fp = GET_FIGHTER(gobj);
    ftSc_MotionVars* mv;
    Vec3 pos;
    int slot;

    if (!fp->throw_flags_b0) {
        fp->throw_flags_b0 = true;
        itSc_Trail_Spawn(gobj, gobj, mu_ak_article_kind(gobj, ftSc_Article_Trail), 0, 0,
                         fp->cur_pos.z);
    }
    if (ftAnim_IsFramesRemaining(gobj)) {
        return;
    }

    Fighter_ChangeMotionState(gobj, ftSc_MS_SpecialNDash, Ft_MF_None, 0.0f, 1.0f, 0.0f, NULL);
    mv = ftSc_MV(fp);
    mv->target.z = fp->cur_pos.z;
    mv->timer = 30;
    mv->target.y = fp->cur_pos.y - 15.0f;
    mv->target.x =
        (float) __builtin_fma((double) fp->facing_dir, 30.0, (double) fp->cur_pos.x);
    mv->best_dist = 45.0f;
    mv->slot = -1;

    for (slot = 0; slot < 4; slot++) {
        float dist;
        Vec3 mark;

        if (!ftSc_SpecialN_TargetPos(fp, slot, &pos)) {
            continue;
        }
        dist = ftSc_Distance(fp->cur_pos.x, fp->cur_pos.y, pos.x, pos.y);
        if (mv->best_dist <= dist) {
            continue;
        }
        mv->target = pos;
        mv->best_dist = dist;
        mv->slot = slot;
        mark.x = mv->target.x;
        mark.y = mv->target.y;
        mark.z = 4.0f;
        itSc_Mark_Spawn(gobj, mu_ak_article_kind(gobj, ftSc_Article_Mark), &mark);
    }
}

/* +0EF4: states 343 and 344, physics: a branch to the retail routine. */
void ftSc_SpecialN_Phys(HSD_GObj* gobj)
{
    ft_80085134(gobj);
}

/* +0EF8: states 343 and 344, collision: a branch to the retail air collision (the result is
 * not used: the wind up does not land). */
void ftSc_SpecialN_Coll(HSD_GObj* gobj)
{
    ft_80081D0C(gobj);
}

/* +242C: state 364, animation. Within 2 units of the target, or when the 30 frames are out:
 * stop and the miss state (367). */
void ftSc_SpecialNDash_Anim(HSD_GObj* gobj)
{
    Fighter* fp = GET_FIGHTER(gobj);
    ftSc_MotionVars* mv = ftSc_MV(fp);
    float dist = ftSc_Distance(fp->cur_pos.x, fp->cur_pos.y, mv->target.x, mv->target.y);

    if (!(dist < 2.0f)) {
        mv->timer -= 1;
        if (mv->timer > 0) {
            return;
        }
    }
    fp->self_vel.x = 0.0f;
    fp->self_vel.y = 0.0f;
    Fighter_ChangeMotionState(gobj, ftSc_MS_SpecialNMiss, Ft_MF_None, 0.0f, 1.0f, 0.0f, NULL);
}

/* +3A14: the dash's deal damage callback. Facing the target, then the rebound speed: x =
 * cos(0.85) * -2 * facing (in double), y = 2 * sin(0.85); state 366. */
static void ftSc_SpecialNDash_OnDealDamage(HSD_GObj* gobj)
{
    Fighter* fp = GET_FIGHTER(gobj);
    float s;
    float c;
    double back;

    if (!(ftSc_MV(fp)->target.x <= fp->cur_pos.x)) {
        fp->facing_dir = 1.0f;
    } else {
        fp->facing_dir = -1.0f;
    }
    s = sinf(0.85f);
    back = (double) -fp->facing_dir + (double) -fp->facing_dir;
    fp->self_vel.y = s + s;
    c = cosf(0.85f);
    fp->self_vel.x = (float) ((double) c * back);
    Fighter_ChangeMotionState(gobj, ftSc_MS_SpecialNRebound, Ft_MF_None, 0.0f, 1.0f, 0.0f,
                              NULL);
}

/* +24DC: state 364, interrupt: (re)sets the deal damage callback every frame. */
void ftSc_SpecialNDash_IASA(HSD_GObj* gobj)
{
    GET_FIGHTER(gobj)->deal_dmg_cb = ftSc_SpecialNDash_OnDealDamage;
}

/* +24F0: state 364, physics. With a target player: its hip bone is read again and, while it is
 * within 45 units, becomes the target. Then the speed is 3 units a frame toward the target in
 * the plane (the file normalizes his speed twice into the result first, which the third call
 * overwrites; kept).
 *
 * The file ignores whether the bone could be read: when the target player is gone it measures
 * from whatever its stack held there. Here the point is zero then (assumed, see NOTES.md). */
void ftSc_SpecialNDash_Phys(HSD_GObj* gobj)
{
    Fighter* fp = GET_FIGHTER(gobj);
    ftSc_MotionVars* mv = ftSc_MV(fp);
    Vec3 dir;
    Vec3 out;
    float tx;
    float ty;

    if (mv->slot != -1) {
        Vec3 pos = { 0.0f, 0.0f, 0.0f };
        float dist;
        ftSc_SpecialN_TargetPos(fp, mv->slot, &pos);
        dist = ftSc_Distance(fp->cur_pos.x, fp->cur_pos.y, pos.x, pos.y);
        if (!(dist >= 45.0f)) {
            mv->target = pos;
        }
    }

    /* +2518 */
    tx = mv->target.x;
    ty = mv->target.y;
    PSVECNormalize(&fp->self_vel, &out);
    dir.x = tx - fp->cur_pos.x;
    dir.y = ty - fp->cur_pos.y;
    dir.z = 0.0f;
    PSVECNormalize(&fp->self_vel, &out);
    PSVECNormalize(&dir, &out);
    fp->self_vel.x = out.x * 3.0f;
    fp->self_vel.y = out.y * 3.0f;
}

/* +25F4: state 364, collision. On landing after frame 4.0: state 365 (the retail LandingAir
 * callbacks). Before that the landing does nothing. */
void ftSc_SpecialNDash_Coll(HSD_GObj* gobj)
{
    Fighter* fp = GET_FIGHTER(gobj);

    if (!ft_80081D0C(gobj)) {
        return;
    }
    if (fp->cur_anim_frame > 4.0f) {
        Fighter_ChangeMotionState(gobj, ftSc_MS_SpecialNLanding, Ft_MF_None, 0.0f, 1.0f, 0.0f,
                                  NULL);
    }
}

/* +2684: state 366, animation: at the end, the retail fall. */
void ftSc_SpecialNRebound_Anim(HSD_GObj* gobj)
{
    if (!ftAnim_IsFramesRemaining(gobj)) {
        ftCo_Fall_Enter(gobj);
    }
}

/* +26D0 (and +27B0, +2810's first call): a branch to the retail air physics. */
void ftSc_SpecialNRebound_Phys(HSD_GObj* gobj)
{
    ft_80084EEC(gobj);
}

/* +26D4: state 366, collision: on landing, the special landing with 28 frames. */
void ftSc_SpecialNRebound_Coll(HSD_GObj* gobj)
{
    if (ft_80081D0C(gobj)) {
        ftCo_LandingFallSpecial_Enter(gobj, false, 28.0f);
    }
}

/* +272C: state 367, animation: at the end, the special fall (mobility 1.0, landing 28) and no
 * jumps left. */
void ftSc_SpecialNMiss_Anim(HSD_GObj* gobj)
{
    Fighter* fp = GET_FIGHTER(gobj);

    if (ftAnim_IsFramesRemaining(gobj)) {
        return;
    }
    ftCo_80096900(gobj, 0, 1, true, 1.0f, 28.0f);
    fp->x1968_jumpsUsed = (u8) fp->co_attrs.max_jumps;
}

/* +27B0: state 367, physics: a branch to the retail air physics. */
void ftSc_SpecialNMiss_Phys(HSD_GObj* gobj)
{
    ft_80084EEC(gobj);
}

/* +27B4: state 367, collision: the retail ledge and landing check with no ledge test and the
 * retail landing (ft_80082B1C). */
void ftSc_SpecialNMiss_Coll(HSD_GObj* gobj)
{
    ft_800831CC(gobj, NULL, ft_80082B1C);
}
