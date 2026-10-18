/* ACE's Shadow: neutral special, the homing attack (states 341 to 349).
 *
 * Start (ground or air; from the ground he is put in the air) -> Charge: curled up for 20 to 40
 * frames (B releases from 20) -> the search: the nearest opponent within 58 units (aimed 3 units
 * above the feet), else the nearest break-the-targets target -> Attack: a dash at it for 17
 * frames, the aim nudged by the stick, or AttackMiss: a dive forward and down. A hit bounces him
 * up (Hit), a wall, ceiling or floor bounces him back (Rebound), the end of the time or the point
 * reached ends in Cancel, then Landing.
 *
 * The steps are the Akaneia Sonic's, written again from this file's listing: where a compare is
 * one way round on the console it is kept that way (bge, ble and bne also branch on an
 * unordered compare), and every fmadds is MU_FMADDS. */
#include "shadow.h"

#include <math.h>

#include <melee/ft/fighter.h>
#include <melee/ft/ft_081B.h>
#include <melee/ft/ft_0892.h>
#include <melee/ft/ftanim.h>
#include <melee/ft/ftcliffcommon.h>
#include <melee/ft/ftcommon.h>
#include <melee/ft/inlines.h>
#include <melee/ft/kinds/ftCommon/ftCo_Fall.h>
#include <melee/ft/kinds/ftCommon/ftCo_Wait.h>
#include <melee/ft/types.h>
#include <melee/gm/gmvs.h>
#include <melee/it/forward.h>
#include <melee/it/inlines.h>
#include <melee/it/types.h>
#include <melee/mp/forward.h>
#include <melee/pl/player.h>
#include <sysdolphin/baselib/controller.h>
#include <sysdolphin/baselib/gobj.h>

static void ftSh_SpecialNAttack_Enter(HSD_GObj* gobj, float target_x, float target_y);
static void ftSh_SpecialNAttackMiss_Enter(HSD_GObj* gobj);
static void ftSh_SpecialNRebound_Enter(HSD_GObj* gobj, u32 env_flags);
static void ftSh_SpecialNHit_Enter(HSD_GObj* gobj);
static void ftSh_SpecialNMiss_OnHit(HSD_GObj* gobj);

/* The empty callbacks (`blr`: +0E90, +0E94, +0EDC, +0EE0, +0FB0, +0FB4, +11C4, +11C8, +1628,
 * +16E4, +1828, +19D0, +1AF0, +1AF4, +1E10, +1FC8, +2084, +21B8, +2274, +26D4, +2884, +29B0,
 * +2A50, +2B54, +2BFC, +3438, +3550, +370C, +37D8, +3874, +3878): one function. */
void ftSh_SpecialN_Empty(HSD_GObj* gobj) {}

/* ---- entry ---------------------------------------------------------------------------------- */

/* +388C (slots 4 and 5 are both a branch to it): Start, in the air animation when airborne,
 * else put in the air first; the animation's first frame; the charge count and the speed
 * cleared. */
void ftSh_SpecialN_Enter(HSD_GObj* gobj)
{
    Fighter* fp = GET_FIGHTER(gobj);

    if (fp->ground_or_air == GA_Air) {
        Fighter_ChangeMotionState(gobj, ftSh_MS_SpecialAirNStart, Ft_MF_None, 0.0F, 1.0F, 0.0F,
                                  NULL);
    } else {
        ftCommon_8007D5D4(fp);
        Fighter_ChangeMotionState(gobj, ftSh_MS_SpecialNStart, Ft_MF_None, 0.0F, 1.0F, 0.0F,
                                  NULL);
    }
    ftAnim_8006EBA4(gobj);
    ftSh_MV(fp)->specialn.charge_frames = 0;
    fp->self_vel.x = 0.0F;
    fp->self_vel.y = 0.0F;
}

/* ---- Start (341, 342) ----------------------------------------------------------------------- */

/* +0E00: at the end, the Charge (343) at the charge animation rate, the small rise, the spin
 * ball (+3F48). */
void ftSh_SpecialNStart_Anim(HSD_GObj* gobj)
{
    Fighter* fp;
    ftSh_DatAttrs* da;

    if (ftAnim_IsFramesRemaining(gobj)) {
        return;
    }
    fp = GET_FIGHTER(gobj);
    da = ftSh_DA(fp);
    Fighter_ChangeMotionState(gobj, ftSh_MS_SpecialNCharge, Ft_MF_None, 0.0F,
                              da->specialn_charge_anim_rate, 0.0F, NULL);
    fp->self_vel.y = da->specialn_charge_vel_y;
    fp->accessory4_cb = ftSh_GFXSpin;
}

/* +0E98: `b ft_80081D0C` */
void ftSh_SpecialNStart_Coll(HSD_GObj* gobj)
{
    ft_80081D0C(gobj);
}

/* ---- Charge (343) ----------------------------------------------------------------------------- */

static void ftSh_SpecialN_SearchTarget_EnterAttack(HSD_GObj* gobj);

/* +0E9C: at the maximum, or past the minimum with B pressed, the search (+3FF0); else one more
 * frame. */
void ftSh_SpecialNCharge_Anim(HSD_GObj* gobj)
{
    Fighter* fp = GET_FIGHTER(gobj);
    ftSh_DatAttrs* da = ftSh_DA(fp);
    ftSh_SpecialNVars* mv = &ftSh_MV(fp)->specialn;

    if (mv->charge_frames >= da->specialn_max_charge_frames ||
        (mv->charge_frames >= da->specialn_min_charge_frames &&
         (fp->input.pressed_buttons & HSD_PAD_B)))
    {
        ftSh_SpecialN_SearchTarget_EnterAttack(gobj);
        return;
    }
    mv->charge_frames++;
}

/* +0EE4: the air collision with the box of the attributes; a ceiling stops the rise. */
void ftSh_SpecialNCharge_Coll(HSD_GObj* gobj)
{
    Fighter* fp = GET_FIGHTER(gobj);
    ftCollisionBox box = ftSh_CollBox(fp);

    ft_800824A0(gobj, &box);
    if (fp->coll_data.env_flags & Collide_CeilingMask) {
        fp->self_vel.y = 0.0F;
    }
}

/* ---- the search ------------------------------------------------------------------------------ */

/* +3FF0's loops: the six player slots (not himself, not an empty slot, not a team mate in a team
 * match), the distance squared as fmadds(dz, dz, fmadds(dx, dx, dy * dy)); kept when under the
 * best so far (FLT_MAX at the start, +3AB8) and under the range squared; the point is the
 * opponent's feet raised by the offset. With no opponent, the same over the item list for items
 * of kind 0xD1 (It_Kind_Mato, break-the-targets), at the target's own point. The compares are the
 * console's (bge / ble skip on an unordered value too, so a NaN distance is never kept). Also the hat's (hat +0CFC),
 * which passes its own values. */
MU_NO_CONTRACT bool ftSh_SpecialN_SearchTarget(HSD_GObj* gobj, float search_range,
                                               float target_offset_y, float* out_x, float* out_y)
{
    Fighter* fp = GET_FIGHTER(gobj);
    float best = FTSH_FLT_MAX;
    float target_x = 0.0F;
    float target_y = 0.0F;
    bool found = false;
    int slot;

    for (slot = 0; slot < 6; slot++) {
        HSD_GObj* other = Player_GetEntity(slot);
        Fighter* ofp;
        u8 team;
        float dx;
        float dy;
        float dz;
        float dist2;
        float range2;

        if (other == NULL || other == gobj) {
            continue;
        }
        team = fp->team;
        if (gm_8016B168()) {
            if (Player_GetTeam(slot) == (int) team) {
                continue;
            }
        }
        ofp = GET_FIGHTER(other);
        dy = fp->cur_pos.y - ofp->cur_pos.y;
        dx = fp->cur_pos.x - ofp->cur_pos.x;
        dz = fp->cur_pos.z - ofp->cur_pos.z;
        dy = dy * dy;
        dist2 = MU_FMADDS(dz, dz, MU_FMADDS(dx, dx, dy));
        if (!(dist2 < best)) {
            continue;
        }
        range2 = search_range * search_range;
        if (!(range2 > dist2)) {
            continue;
        }
        target_x = ofp->cur_pos.x;
        best = dist2;
        found = true;
        target_y = ofp->cur_pos.y + target_offset_y;
    }

    if (!found) {
        HSD_GObj* item_gobj;

        for (item_gobj = HSD_GObjPLinkHead[HSD_GOBJ_PLINK_ITEM]; item_gobj != NULL;
             item_gobj = item_gobj->next)
        {
            Item* ip = GET_ITEM(item_gobj);
            float dx;
            float dy;
            float dz;
            float dist2;
            float range2;

            if (ip->kind != It_Kind_Mato) {
                continue;
            }
            dy = fp->cur_pos.y - ip->pos.y;
            dx = fp->cur_pos.x - ip->pos.x;
            dz = fp->cur_pos.z - ip->pos.z;
            dy = dy * dy;
            dist2 = MU_FMADDS(dz, dz, MU_FMADDS(dx, dx, dy));
            if (!(best > dist2)) {
                continue;
            }
            range2 = search_range * search_range;
            if (!(range2 > dist2)) {
                continue;
            }
            target_y = ip->pos.y;
            target_x = ip->pos.x;
            found = true;
            best = dist2;
        }
    }

    *out_x = target_x;
    *out_y = target_y;
    return found;
}

/* +3FF0: the search, then the dash at the point nudged by the stick (two fmadds,
 * attributes +40 and +44, tail call of +5754), or the dive (+41F8, a tail call of +5838). */
static void ftSh_SpecialN_SearchTarget_EnterAttack(HSD_GObj* gobj)
{
    Fighter* fp = GET_FIGHTER(gobj);
    ftSh_DatAttrs* da = ftSh_DA(fp);
    float target_x;
    float target_y;

    if (!ftSh_SpecialN_SearchTarget(gobj, da->specialn_search_range,
                                    da->specialn_target_offset_y, &target_x, &target_y))
    {
        ftSh_SpecialNAttackMiss_Enter(gobj);
        return;
    }
    ftSh_SpecialNAttack_Enter(gobj,
                              MU_FMADDS(da->specialn_aim_stick_x, fp->input.lstick[0].x, target_x),
                              MU_FMADDS(da->specialn_aim_stick_y, fp->input.lstick[0].y, target_y));
}

/* ---- AttackMiss (344): the dive ------------------------------------------------------------- */

/* +5838 */
static void ftSh_SpecialNAttackMiss_Enter(HSD_GObj* gobj)
{
    Fighter* fp = GET_FIGHTER(gobj);
    ftSh_DatAttrs* da = ftSh_DA(fp);
    float speed;

    Fighter_ChangeMotionState(gobj, ftSh_MS_SpecialNAttackMiss, Ft_MF_None, 0.0F,
                              da->specialn_attack_anim_rate, 0.0F, NULL);
    speed = da->specialn_miss_speed;
    ftSh_MV(fp)->specialn.timer = da->specialn_attack_frames;
    fp->self_vel.y = -speed;
    fp->accessory4_cb = ftSh_GFXSpinAndTrail;
    fp->self_vel.x = fp->facing_dir * speed;
    fp->deal_dmg_cb = ftSh_SpecialNMiss_OnHit;
}

/* +0F2C: the timer, then Cancel (346; the speed down cleared, cmd_vars[0] cleared). */
void ftSh_SpecialNAttackMiss_Anim(HSD_GObj* gobj)
{
    Fighter* fp = GET_FIGHTER(gobj);
    ftSh_SpecialNVars* mv = &ftSh_MV(fp)->specialn;

    if (mv->timer > 0) {
        mv->timer--;
        return;
    }
    Fighter_ChangeMotionState(gobj, ftSh_MS_SpecialNCancel, Ft_MF_None, 0.0F, 1.0F, 0.0F, NULL);
    fp->self_vel.y = 0.0F;
    fp->cmd_vars[0] = 0;
}

/* +0FB8 (also 345's, +11CC branches here): the air collision; a wall, ceiling or floor
 * (env & 0x1EFFF, the bit 0x1000 left out) rebounds (+4234); else a ledge in reach is grabbed. */
void ftSh_SpecialNDash_Coll(HSD_GObj* gobj)
{
    Fighter* fp = GET_FIGHTER(gobj);
    ftCollisionBox box = ftSh_CollBox(fp);
    u32 env;

    ft_800824A0(gobj, &box);
    env = fp->coll_data.env_flags;
    if (env & (Collide_WallMask | Collide_CeilingMask | Collide_FloorMask)) {
        ftSh_SpecialNRebound_Enter(gobj, env);
        return;
    }
    if (ftCliffCommon_80081298(gobj)) {
        ftCliffCommon_80081370(gobj);
    }
}

/* ---- Attack (345): the homing dash ---------------------------------------------------------- */

/* +5754 */
static void ftSh_SpecialNAttack_Enter(HSD_GObj* gobj, float target_x, float target_y)
{
    Fighter* fp = GET_FIGHTER(gobj);
    ftSh_DatAttrs* da = ftSh_DA(fp);
    ftSh_SpecialNVars* mv = &ftSh_MV(fp)->specialn;
    float dx = target_x - fp->cur_pos.x;
    float dy = target_y - fp->cur_pos.y;
    float angle;
    float speed;
    float vel_y;

    mv->target.x = target_x;
    mv->timer = da->specialn_attack_frames;
    mv->target.y = target_y;
    angle = atan2f(dy, dx);
    mv->angle = angle;
    speed = da->specialn_attack_speed;
    fp->self_vel.x = speed * cosf(angle);
    speed = da->specialn_attack_speed;
    vel_y = speed * sinf(mv->angle);
    fp->self_vel.y = vel_y;
    Fighter_ChangeMotionState(gobj, ftSh_MS_SpecialNAttack, Ft_MF_None, 0.0F,
                              da->specialn_attack_anim_rate, 0.0F, NULL);
    fp->accessory4_cb = ftSh_GFXSpinAndTrailVelocityDirection;
    fp->deal_dmg_cb = ftSh_SpecialNHit_Enter;
}

/* +1048: the speed set again along the angle every frame; the timer, then Cancel; then, every
 * frame (also right after that Cancel, which can so be entered twice), Cancel when the distance
 * squared to the target (fmadds as the search, its z the never written mv word +2354) is not
 * over the speed squared. */
MU_NO_CONTRACT void ftSh_SpecialNAttack_Anim(HSD_GObj* gobj)
{
    Fighter* fp = GET_FIGHTER(gobj);
    ftSh_DatAttrs* da = ftSh_DA(fp);
    ftSh_SpecialNVars* mv = &ftSh_MV(fp)->specialn;
    float speed;
    float speed2;
    float dx;
    float dy;
    float dz;
    float dist2;

    speed = da->specialn_attack_speed;
    fp->self_vel.x = speed * cosf(mv->angle);
    speed = da->specialn_attack_speed;
    fp->self_vel.y = speed * sinf(mv->angle);

    if (mv->timer > 0) {
        mv->timer--;
    } else {
        Fighter_ChangeMotionState(gobj, ftSh_MS_SpecialNCancel, Ft_MF_None, 0.0F, 1.0F, 0.0F,
                                  NULL);
        fp->self_vel.y = 0.0F;
        fp->cmd_vars[0] = 0;
    }

    dy = fp->cur_pos.y - mv->target.y;
    dx = fp->cur_pos.x - mv->target.x;
    dz = fp->cur_pos.z - mv->target.z;
    dy = dy * dy;
    speed2 = da->specialn_attack_speed;
    speed2 = speed2 * speed2;
    dist2 = MU_FMADDS(dz, dz, MU_FMADDS(dx, dx, dy));
    /* cror lt|un, bne: no Cancel when under or unordered */
    if (speed2 >= dist2) {
        Fighter_ChangeMotionState(gobj, ftSh_MS_SpecialNCancel, Ft_MF_None, 0.0F, 1.0F, 0.0F,
                                  NULL);
        fp->self_vel.y = 0.0F;
        fp->cmd_vars[0] = 0;
    }
}

/* ---- Cancel (346) ----------------------------------------------------------------------------- */

/* +11D0 */
void ftSh_SpecialNCancel_Anim(HSD_GObj* gobj)
{
    if (!ftAnim_IsFramesRemaining(gobj)) {
        ftCo_Fall_Enter(gobj);
    }
}

/* +121C: the script's cmd_vars[0] opens the fall's interrupts. */
void ftSh_SpecialNCancel_IASA(HSD_GObj* gobj)
{
    if (GET_FIGHTER(gobj)->cmd_vars[0] == 1) {
        ftCo_Fall_IASA_Inner(gobj);
    }
}

/* +1230 */
void ftSh_SpecialNCancel_Phys(HSD_GObj* gobj)
{
    Fighter* fp = GET_FIGHTER(gobj);
    ftSh_DatAttrs* da = ftSh_DA(fp);

    ft_80084DB0(gobj);
    ftCommon_CalcSelfAccel_Deaccel(fp, da->specialn_cancel_air_decel);
}

/* +1270: landing goes to Landing (347). */
void ftSh_SpecialNCancel_Coll(HSD_GObj* gobj)
{
    Fighter* fp = GET_FIGHTER(gobj);

    if (!ft_CheckGroundAndLedge(gobj, (int) fp->facing_dir)) {
        return;
    }
    ftCommon_8007D6A4(fp);
    Fighter_ChangeMotionState(gobj, ftSh_MS_SpecialNLanding, Ft_MF_None, 0.0F, 1.0F, 0.0F, NULL);
    fp->self_vel.y = 0.0F;
    fp->cmd_vars[0] = 0;
}

/* ---- Landing (347) ---------------------------------------------------------------------------- */

/* +1324 */
void ftSh_SpecialNLanding_Anim(HSD_GObj* gobj)
{
    if (!ftAnim_IsFramesRemaining(gobj)) {
        ft_8008A2BC(gobj);
    }
}

/* +1370 */
void ftSh_SpecialNLanding_IASA(HSD_GObj* gobj)
{
    if (GET_FIGHTER(gobj)->cmd_vars[0] == 1) {
        ftCo_Wait_IASA(gobj);
    }
}

/* +1384 */
void ftSh_SpecialNLanding_Phys(HSD_GObj* gobj)
{
    Fighter* fp = GET_FIGHTER(gobj);

    ftCommon_CalcGroundAccel_Deaccel(fp, ftSh_DA(fp)->specialn_landing_decel);
    ftCommon_SetSelfMovementFromGroundedMovement(gobj);
}

/* +13C0: `b ft_800827A0` */
void ftSh_SpecialNLanding_Coll(HSD_GObj* gobj)
{
    ft_800827A0(gobj);
}

/* ---- Rebound (348) and Hit (349) ------------------------------------------------------------- */

/* +4234: bounce off what was touched. A wall or a ceiling (env & 0x6FFF) turns and damps x, a
 * floor (env & 0x18000) only damps it; then x into [-max, max] and y flipped and capped. The
 * clamps are the console's (an unordered x or y is left as it is). */
static void ftSh_SpecialNRebound_Enter(HSD_GObj* gobj, u32 env_flags)
{
    Fighter* fp = GET_FIGHTER(gobj);
    ftSh_DatAttrs* da = ftSh_DA(fp);
    float vx;
    float vy;
    float max;

    Fighter_ChangeMotionState(gobj, ftSh_MS_SpecialNRebound, Ft_MF_None, 0.0F, 1.0F, 0.0F, NULL);
    vx = fp->self_vel.x;
    if (env_flags & (Collide_WallMask | Collide_CeilingMask)) {
        vx = vx * -da->specialn_rebound_mul_x;
        fp->self_vel.x = vx;
    } else if (env_flags & Collide_FloorMask) {
        vx = vx * da->specialn_rebound_mul_x;
        fp->self_vel.x = vx;
    }
    max = da->specialn_rebound_max_vel_x;
    vy = -fp->self_vel.y;
    if (max < vx) {
        vx = max;
        fp->self_vel.x = max;
    }
    if (-max > vx) {
        fp->self_vel.x = -max;
    }
    max = da->specialn_rebound_max_vel_y;
    if (vy > max) {
        vy = max;
    }
    fp->self_vel.y = vy;
    fp->cmd_vars[0] = 0;
}

/* +5EA8: the dive's deal_dmg_cb (it hit an opponent or a shield): Rebound (348) with x clamped as
 * it is and y flipped and capped. */
static void ftSh_SpecialNMiss_OnHit(HSD_GObj* gobj)
{
    Fighter* fp = GET_FIGHTER(gobj);
    ftSh_DatAttrs* da = ftSh_DA(fp);
    float vx;
    float vy;
    float max;

    Fighter_ChangeMotionState(gobj, ftSh_MS_SpecialNRebound, Ft_MF_None, 0.0F, 1.0F, 0.0F, NULL);
    vx = fp->self_vel.x;
    max = da->specialn_rebound_max_vel_x;
    vy = -fp->self_vel.y;
    if (vx > max) {
        vx = max;
        fp->self_vel.x = max;
    }
    if (-max > vx) {
        fp->self_vel.x = -max;
    }
    max = da->specialn_rebound_max_vel_y;
    if (vy > max) {
        vy = max;
    }
    fp->self_vel.y = vy;
    fp->cmd_vars[0] = 0;
}

/* +5D04: the dash's deal_dmg_cb: Hit (349), up at least 2 a frame (+3AA8), x clamped, y capped. */
static void ftSh_SpecialNHit_Enter(HSD_GObj* gobj)
{
    Fighter* fp = GET_FIGHTER(gobj);
    ftSh_DatAttrs* da = ftSh_DA(fp);
    float vx;
    float vy;
    float max;

    Fighter_ChangeMotionState(gobj, ftSh_MS_SpecialNHit, Ft_MF_None, 0.0F, 1.0F, 0.0F, NULL);
    vy = fp->self_vel.y;
    if (vy < 0.0F) {
        vy = -vy;
    }
    if (vy < 2.0F) {
        vy = 2.0F;
    }
    fp->self_vel.y = vy;
    vx = fp->self_vel.x;
    max = da->specialn_rebound_max_vel_x;
    if (vx > max) {
        vx = max;
        fp->self_vel.x = max;
    }
    if (-max > vx) {
        fp->self_vel.x = -max;
    }
    max = da->specialn_rebound_max_vel_y;
    if (max < vy) {
        vy = max;
    }
    fp->self_vel.y = vy;
    fp->cmd_vars[0] = 0;
}

/* +13C4 (rows 7 and 8) */
void ftSh_SpecialNRebound_Anim(HSD_GObj* gobj)
{
    if (!ftAnim_IsFramesRemaining(gobj)) {
        ftCo_Fall_Enter(gobj);
    }
}

/* +1410 */
void ftSh_SpecialNRebound_IASA(HSD_GObj* gobj)
{
    if (GET_FIGHTER(gobj)->cmd_vars[0] == 1) {
        ftCo_Fall_IASA_Inner(gobj);
    }
}

/* +1424 */
void ftSh_SpecialNRebound_Phys(HSD_GObj* gobj)
{
    Fighter* fp = GET_FIGHTER(gobj);
    ftSh_DatAttrs* da = ftSh_DA(fp);

    ftCommon_CalcSelfAccel_Deaccel(fp, da->specialn_rebound_air_decel);
    ftCommon_Fall(fp, da->specialn_rebound_gravity, da->specialn_rebound_terminal_vel);
}

/* +1470: landing goes to Landing (347). */
void ftSh_SpecialNRebound_Coll(HSD_GObj* gobj)
{
    Fighter* fp = GET_FIGHTER(gobj);

    if (!ft_80081D0C(gobj)) {
        return;
    }
    ftCommon_8007D6A4(fp);
    Fighter_ChangeMotionState(gobj, ftSh_MS_SpecialNLanding, Ft_MF_None, 0.0F, 1.0F, 0.0F, NULL);
    fp->self_vel.y = 0.0F;
    fp->cmd_vars[0] = 0;
}
