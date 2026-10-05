/* Akaneia's Sonic: neutral special, the homing attack.
 *
 * Start (ground or air, always airborne afterwards) -> Charge: curled up, floating up slightly,
 * for 15..30 frames (B releases early) -> search the nearest opponent (then any break-the-targets
 * target) within range -> Attack: dash straight at it for up to 13 frames, re-aimed by the stick,
 * or AttackMiss: a short dive forward. A hit bounces him off (Hit), touching a wall/floor bounces
 * him back (Rebound); running out of time or reaching the point ends in Cancel.
 * Hand-written from PlSn.dat's ftFunction code; see NOTES.md. */
#include "sonic.h"

#include <math.h>

#include <dolphin/gx.h>
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
#include <melee/it/forward.h>
#include <melee/it/inlines.h>
#include <melee/it/types.h>
#include <melee/mp/forward.h>
#include <melee/pl/player.h>
#include <sysdolphin/baselib/cobj.h>
#include <sysdolphin/baselib/gobj.h>
#include <sysdolphin/baselib/pobj.h>
#include <sysdolphin/baselib/state.h>
#include <sysdolphin/baselib/tev.h>

#define ftSn_TAU 6.283185307179586
#define ftSn_FLT_MAX 3.40282001837565598e+38F /* 0x7F7FFFEE, the m-ex constant */

static void ftSn_SpecialNCharge_Enter(HSD_GObj* gobj);
static void ftSn_SpecialN_SearchTarget_EnterAttack(HSD_GObj* gobj);
static void ftSn_SpecialNAttackMiss_Enter(HSD_GObj* gobj);
static void ftSn_SpecialNAttack_Enter(HSD_GObj* gobj, float target_x, float target_y);
static void ftSn_SpecialNCancel_Enter(HSD_GObj* gobj);
static void ftSn_SpecialNLanding_Enter(HSD_GObj* gobj);
static void ftSn_SpecialNRebound_Enter(HSD_GObj* gobj, u32 env_flags);
static void ftSn_SpecialNRebound_OnEnter(HSD_GObj* gobj);
static void ftSn_SpecialNHit_Enter(HSD_GObj* gobj);

/* ---- entry ---------------------------------------------------------------------------------- */

/* SpecialNStart_Enter: specialn and specialairn */
void ftSn_SpecialNStart_Enter(HSD_GObj* gobj)
{
    Fighter* fp = GET_FIGHTER(gobj);

    if (fp->ground_or_air == GA_Air) {
        Fighter_ChangeMotionState(gobj, ftSn_MS_SpecialAirNStart, Ft_MF_None, 0.0F, 1.0F,
                                  0.0F, NULL);
    } else {
        ftCommon_8007D5D4(fp);
        Fighter_ChangeMotionState(gobj, ftSn_MS_SpecialNStart, Ft_MF_None, 0.0F, 1.0F, 0.0F,
                                  NULL);
    }
    ftAnim_8006EBA4(gobj);
    fp->self_vel.x = 0.0F;
    fp->self_vel.y = 0.0F;
    ftSn_MV(fp)->specialn.charge_frames = 0;
}

/* ---- Start ---------------------------------------------------------------------------------- */

void ftSn_SpecialNStart_Anim(HSD_GObj* gobj)
{
    if (!ftAnim_IsFramesRemaining(gobj)) {
        ftSn_SpecialNCharge_Enter(gobj);
    }
}

void ftSn_SpecialNStart_IASA(HSD_GObj* gobj) {}

void ftSn_SpecialNStart_Phys(HSD_GObj* gobj) {}

void ftSn_SpecialNStart_Coll(HSD_GObj* gobj)
{
    ft_80081D0C(gobj);
}

/* ---- Charge --------------------------------------------------------------------------------- */

static void ftSn_SpecialNCharge_Enter(HSD_GObj* gobj)
{
    Fighter* fp = GET_FIGHTER(gobj);
    ftSonic_DatAttrs* da = ftSn_DA(fp);

    Fighter_ChangeMotionState(gobj, ftSn_MS_SpecialNCharge, Ft_MF_None, 0.0F,
                              da->specialn_charge_anim_rate, 0.0F, NULL);
    fp->self_vel.y = da->specialn_charge_vel_y;
    fp->accessory4_cb = ftSn_GFXSpin;
}

void ftSn_SpecialNCharge_Anim(HSD_GObj* gobj)
{
    Fighter* fp = GET_FIGHTER(gobj);
    ftSonic_DatAttrs* da = ftSn_DA(fp);
    ftSonic_SpecialNVars* mv = &ftSn_MV(fp)->specialn;

    if (mv->charge_frames >= da->specialn_max_charge_frames ||
        (mv->charge_frames >= da->specialn_min_charge_frames &&
         (fp->input.pressed_buttons & HSD_PAD_B)))
    {
        ftSn_SpecialN_SearchTarget_EnterAttack(gobj);
        return;
    }
    mv->charge_frames++;
}

void ftSn_SpecialNCharge_IASA(HSD_GObj* gobj) {}

void ftSn_SpecialNCharge_Phys(HSD_GObj* gobj) {}

void ftSn_SpecialNCharge_Coll(HSD_GObj* gobj)
{
    Fighter* fp = GET_FIGHTER(gobj);
    ftCollisionBox box = ftSn_CollBox(fp);

    ft_800824A0(gobj, &box);
    if (fp->coll_data.env_flags & Collide_CeilingMask) {
        fp->self_vel.y = 0.0F;
    }
}

/* ---- target search -------------------------------------------------------------------------- */

/* The search of SpecialN_SearchTarget_EnterAttack: the nearest opponent within range (aimed a
 * little above the feet); failing that, the nearest break-the-targets target. Returns false when
 * there is neither. @p da is where the range and the offset come from: Sonic's attributes, or
 * the copy of them in Kirby's hat file (sonic_kirby.c), whose search is this routine. */
bool ftSn_SpecialN_SearchTarget(HSD_GObj* gobj, ftSonic_DatAttrs* da, float* out_x, float* out_y)
{
    Fighter* fp = GET_FIGHTER(gobj);
    float best = ftSn_FLT_MAX;
    float target_x = 0.0F;
    float target_y = 0.0F;
    bool found = false;
    int slot;

    for (slot = 0; slot < 6; slot++) {
        HSD_GObj* other = Player_GetEntity(slot);
        Fighter* ofp;
        float dist2;

        if (other == NULL || other == gobj) {
            continue;
        }
        if (ftSn_CheckSameTeam(fp->team, slot)) {
            continue;
        }
        ofp = GET_FIGHTER(other);
        dist2 = ftSn_Vec3_DistSquared(&fp->cur_pos, &ofp->cur_pos);
        if (dist2 >= best) {
            continue;
        }
        if (!(da->specialn_search_range * da->specialn_search_range > dist2)) {
            continue;
        }
        target_x = ofp->cur_pos.x;
        target_y = da->specialn_target_offset_y + ofp->cur_pos.y;
        best = dist2;
        found = true;
    }

    if (!found) {
        HSD_GObj* item_gobj;

        for (item_gobj = HSD_GObjPLinkHead[HSD_GOBJ_PLINK_ITEM]; item_gobj != NULL;
             item_gobj = item_gobj->next)
        {
            Item* ip = GET_ITEM(item_gobj);
            float dist2;

            if (ip->kind != It_Kind_Mato) {
                continue;
            }
            dist2 = ftSn_Vec3_DistSquared(&fp->cur_pos, &ip->pos);
            if (dist2 >= best) {
                continue;
            }
            if (!(da->specialn_search_range * da->specialn_search_range > dist2)) {
                continue;
            }
            target_x = ip->pos.x;
            target_y = ip->pos.y;
            best = dist2;
            found = true;
        }
    }

    *out_x = target_x;
    *out_y = target_y;
    return found;
}

/* SpecialN_SearchTarget_EnterAttack: dash at what the search found, or dive when it found
 * nothing. The control stick nudges the aim point. */
static void ftSn_SpecialN_SearchTarget_EnterAttack(HSD_GObj* gobj)
{
    Fighter* fp = GET_FIGHTER(gobj);
    ftSonic_DatAttrs* da = ftSn_DA(fp);
    float target_x;
    float target_y;

    if (!ftSn_SpecialN_SearchTarget(gobj, da, &target_x, &target_y)) {
        ftSn_SpecialNAttackMiss_Enter(gobj);
        return;
    }

    ftSn_SpecialNAttack_Enter(gobj, da->specialn_aim_stick_x * fp->input.lstick[0].x + target_x,
                              da->specialn_aim_stick_y * fp->input.lstick[0].y + target_y);
}

/* ---- AttackMiss ----------------------------------------------------------------------------- */

static void ftSn_SpecialNAttackMiss_Enter(HSD_GObj* gobj)
{
    Fighter* fp = GET_FIGHTER(gobj);
    ftSonic_DatAttrs* da = ftSn_DA(fp);

    Fighter_ChangeMotionState(gobj, ftSn_MS_SpecialNAttackMiss, Ft_MF_None, 0.0F,
                              da->specialn_attack_anim_rate, 0.0F, NULL);
    fp->self_vel.x = da->specialn_miss_speed * fp->facing_dir;
    fp->self_vel.y = -da->specialn_miss_speed;
    ftSn_MV(fp)->specialn.timer = da->specialn_attack_frames;
    fp->accessory4_cb = ftSn_GFXSpinAndTrail;
    fp->deal_dmg_cb = ftSn_SpecialNRebound_OnEnter;
}

void ftSn_SpecialNAttackMiss_Anim(HSD_GObj* gobj)
{
    Fighter* fp = GET_FIGHTER(gobj);
    ftSonic_SpecialNVars* mv = &ftSn_MV(fp)->specialn;

    if (mv->timer > 0) {
        mv->timer--;
        return;
    }
    ftSn_SpecialNCancel_Enter(gobj);
}

void ftSn_SpecialNAttackMiss_IASA(HSD_GObj* gobj) {}

void ftSn_SpecialNAttackMiss_Phys(HSD_GObj* gobj) {}

/* Shared by AttackMiss and Attack: touching a wall, ceiling or floor rebounds; otherwise grab
 * a ledge if one is in reach. */
static void ftSn_SpecialNDash_Coll(HSD_GObj* gobj)
{
    Fighter* fp = GET_FIGHTER(gobj);
    ftCollisionBox box = ftSn_CollBox(fp);
    u32 env;

    ft_800824A0(gobj, &box);
    env = fp->coll_data.env_flags;
    if (env & (Collide_WallMask | Collide_CeilingMask | Collide_FloorMask)) {
        ftSn_SpecialNRebound_Enter(gobj, env);
        return;
    }
    if (ftCliffCommon_80081298(gobj)) {
        ftCliffCommon_80081370(gobj);
    }
}

void ftSn_SpecialNAttackMiss_Coll(HSD_GObj* gobj)
{
    ftSn_SpecialNDash_Coll(gobj);
}

/* ---- Attack --------------------------------------------------------------------------------- */

static void ftSn_SpecialNAttack_Enter(HSD_GObj* gobj, float target_x, float target_y)
{
    Fighter* fp = GET_FIGHTER(gobj);
    ftSonic_DatAttrs* da = ftSn_DA(fp);
    ftSonic_SpecialNVars* mv = &ftSn_MV(fp)->specialn;
    float angle;

    mv->target.x = target_x;
    mv->target.y = target_y;
    mv->timer = da->specialn_attack_frames;
    angle = atan2f(target_y - fp->cur_pos.y, target_x - fp->cur_pos.x);
    mv->angle = angle;
    fp->self_vel.x = da->specialn_attack_speed * cosf(angle);
    fp->self_vel.y = da->specialn_attack_speed * sinf(mv->angle);

    Fighter_ChangeMotionState(gobj, ftSn_MS_SpecialNAttack, Ft_MF_None, 0.0F,
                              da->specialn_attack_anim_rate, 0.0F, NULL);
    fp->accessory4_cb = ftSn_GFXSpinAndTrailVelocityDirection;
    fp->deal_dmg_cb = ftSn_SpecialNHit_Enter;
}

/* Keep the dash at full speed along its angle; stop when the time runs out or the target
 * point is within one frame's travel. (After the time runs out the distance test still runs
 * and can enter Cancel a second time, as shipped.) */
void ftSn_SpecialNAttack_Anim(HSD_GObj* gobj)
{
    Fighter* fp = GET_FIGHTER(gobj);
    ftSonic_DatAttrs* da = ftSn_DA(fp);
    ftSonic_SpecialNVars* mv = &ftSn_MV(fp)->specialn;
    float dist2;

    fp->self_vel.x = da->specialn_attack_speed * cosf(mv->angle);
    fp->self_vel.y = da->specialn_attack_speed * sinf(mv->angle);

    if (mv->timer > 0) {
        mv->timer--;
    } else {
        ftSn_SpecialNCancel_Enter(gobj);
    }

    dist2 = ftSn_Vec3_DistSquared(&fp->cur_pos, &mv->target);
    if (!(dist2 <= da->specialn_attack_speed * da->specialn_attack_speed)) {
        return;
    }
    ftSn_SpecialNCancel_Enter(gobj);
}

void ftSn_SpecialNAttack_IASA(HSD_GObj* gobj) {}

void ftSn_SpecialNAttack_Phys(HSD_GObj* gobj) {}

void ftSn_SpecialNAttack_Coll(HSD_GObj* gobj)
{
    ftSn_SpecialNDash_Coll(gobj);
}

/* ---- Cancel --------------------------------------------------------------------------------- */

static void ftSn_SpecialNCancel_Enter(HSD_GObj* gobj)
{
    Fighter* fp = GET_FIGHTER(gobj);

    Fighter_ChangeMotionState(gobj, ftSn_MS_SpecialNCancel, Ft_MF_None, 0.0F, 1.0F, 0.0F,
                              NULL);
    fp->self_vel.y = 0.0F;
    fp->cmd_vars[0] = 0;
}

void ftSn_SpecialNCancel_Anim(HSD_GObj* gobj)
{
    if (!ftAnim_IsFramesRemaining(gobj)) {
        ftCo_Fall_Enter(gobj);
    }
}

/* cmd_vars[0] is the animation script's interrupt window. */
void ftSn_SpecialNCancel_IASA(HSD_GObj* gobj)
{
    Fighter* fp = GET_FIGHTER(gobj);

    if (fp->cmd_vars[0] == 1) {
        ftCo_Fall_IASA_Inner(gobj);
    }
}

void ftSn_SpecialNCancel_Phys(HSD_GObj* gobj)
{
    Fighter* fp = GET_FIGHTER(gobj);
    ftSonic_DatAttrs* da = ftSn_DA(fp);

    ft_80084DB0(gobj);
    ftCommon_CalcSelfAccel_Deaccel(fp, da->specialn_cancel_air_decel);
}

void ftSn_SpecialNCancel_Coll(HSD_GObj* gobj)
{
    Fighter* fp = GET_FIGHTER(gobj);

    if (ft_CheckGroundAndLedge(gobj, (int) fp->facing_dir)) {
        ftSn_SpecialNLanding_Enter(gobj);
    }
}

/* ---- Landing -------------------------------------------------------------------------------- */

static void ftSn_SpecialNLanding_Enter(HSD_GObj* gobj)
{
    Fighter* fp = GET_FIGHTER(gobj);

    ftCommon_8007D6A4(fp);
    Fighter_ChangeMotionState(gobj, ftSn_MS_SpecialNLanding, Ft_MF_None, 0.0F, 1.0F, 0.0F,
                              NULL);
    fp->self_vel.y = 0.0F;
    fp->cmd_vars[0] = 0;
}

void ftSn_SpecialNLanding_Anim(HSD_GObj* gobj)
{
    if (!ftAnim_IsFramesRemaining(gobj)) {
        ft_8008A2BC(gobj);
    }
}

void ftSn_SpecialNLanding_IASA(HSD_GObj* gobj)
{
    Fighter* fp = GET_FIGHTER(gobj);

    if (fp->cmd_vars[0] == 1) {
        ftCo_Wait_IASA(gobj);
    }
}

void ftSn_SpecialNLanding_Phys(HSD_GObj* gobj)
{
    Fighter* fp = GET_FIGHTER(gobj);

    ftCommon_CalcGroundAccel_Deaccel(fp, ftSn_DA(fp)->specialn_landing_decel);
    ftCommon_SetSelfMovementFromGroundedMovement(gobj);
}

void ftSn_SpecialNLanding_Coll(HSD_GObj* gobj)
{
    ft_800827A0(gobj);
}

/* ---- Rebound / Hit -------------------------------------------------------------------------- */

/* The end of SpecialNRebound_Enter and SpecialNHit_Enter. @p da as in
 * ftSn_SpecialN_SearchTarget: Kirby's copy clamps with the values of its hat file. */
void ftSn_SpecialN_ClampReboundVel(Fighter* fp, ftSonic_DatAttrs* da, float vel_y)
{
    if (fp->self_vel.x > da->specialn_rebound_max_vel_x) {
        fp->self_vel.x = da->specialn_rebound_max_vel_x;
    }
    if (fp->self_vel.x < -da->specialn_rebound_max_vel_x) {
        fp->self_vel.x = -da->specialn_rebound_max_vel_x;
    }
    fp->self_vel.y = da->specialn_rebound_max_vel_y < vel_y ? da->specialn_rebound_max_vel_y
                                                            : vel_y;
    fp->cmd_vars[0] = 0;
}

/* SpecialNRebound_Enter: bounce back off whatever was touched. A wall or ceiling reverses and
 * damps x, a floor only damps it; y always flips. */
static void ftSn_SpecialNRebound_Enter(HSD_GObj* gobj, u32 env_flags)
{
    Fighter* fp = GET_FIGHTER(gobj);
    ftSonic_DatAttrs* da = ftSn_DA(fp);

    Fighter_ChangeMotionState(gobj, ftSn_MS_SpecialNRebound, Ft_MF_None, 0.0F, 1.0F, 0.0F,
                              NULL);
    if (env_flags & (Collide_WallMask | Collide_CeilingMask)) {
        fp->self_vel.x = fp->self_vel.x * -da->specialn_rebound_mul_x;
    } else if (env_flags & Collide_FloorMask) {
        fp->self_vel.x = fp->self_vel.x * da->specialn_rebound_mul_x;
    }
    ftSn_SpecialN_ClampReboundVel(fp, da, -fp->self_vel.y);
}

/* SpecialNRebound_OnEnter: the miss dive's deal_dmg_cb (it hit a shield or an opponent). */
static void ftSn_SpecialNRebound_OnEnter(HSD_GObj* gobj)
{
    ftSn_SpecialNRebound_Enter(gobj, 0);
}

/* SpecialNHit_Enter: the homing dash's deal_dmg_cb. Pops up at least 2 units/frame. */
static void ftSn_SpecialNHit_Enter(HSD_GObj* gobj)
{
    Fighter* fp = GET_FIGHTER(gobj);
    float vel_y;

    Fighter_ChangeMotionState(gobj, ftSn_MS_SpecialNHit, Ft_MF_None, 0.0F, 1.0F, 0.0F, NULL);
    vel_y = fp->self_vel.y;
    if (vel_y < 0.0F) {
        vel_y = -vel_y;
    }
    if (vel_y < 2.0F) {
        vel_y = 2.0F;
    }
    fp->self_vel.y = vel_y;
    ftSn_SpecialN_ClampReboundVel(fp, ftSn_DA(fp), vel_y);
}

void ftSn_SpecialNRebound_Anim(HSD_GObj* gobj)
{
    if (!ftAnim_IsFramesRemaining(gobj)) {
        ftCo_Fall_Enter(gobj);
    }
}

void ftSn_SpecialNRebound_IASA(HSD_GObj* gobj)
{
    Fighter* fp = GET_FIGHTER(gobj);

    if (fp->cmd_vars[0] == 1) {
        ftCo_Fall_IASA_Inner(gobj);
    }
}

void ftSn_SpecialNRebound_Phys(HSD_GObj* gobj)
{
    Fighter* fp = GET_FIGHTER(gobj);
    ftSonic_DatAttrs* da = ftSn_DA(fp);

    ftCommon_CalcSelfAccel_Deaccel(fp, da->specialn_rebound_air_decel);
    ftCommon_Fall(fp, da->specialn_rebound_gravity, da->specialn_rebound_terminal_vel);
}

void ftSn_SpecialNRebound_Coll(HSD_GObj* gobj)
{
    if (ft_80081D0C(gobj)) {
        ftSn_SpecialNLanding_Enter(gobj);
    }
}

/* ---- debug drawing (GXLink_Sonic, fighter debug display only) ------------------------------- */

static void ftSn_GXLink_Init(void)
{
    GXSetColorUpdate(GX_TRUE);
    GXSetAlphaUpdate(GX_FALSE);
    GXSetBlendMode(GX_BM_BLEND, GX_BL_SRCALPHA, GX_BL_INVSRCALPHA, GX_LO_NOOP);
    GXSetAlphaCompare(GX_GREATER, 0, GX_AOP_AND, GX_GREATER, 0);
    GXSetZMode(GX_FALSE, GX_LEQUAL, GX_FALSE);
    GXSetZCompLoc(GX_FALSE);
    GXSetNumTexGens(0);
    GXSetNumTevStages(1);
    GXSetTevOrder(GX_TEVSTAGE0, GX_TEXCOORD_NULL, GX_TEXMAP_NULL, GX_COLOR0A0);
    GXSetTevOp(GX_TEVSTAGE0, GX_PASSCLR);
    GXSetNumChans(1);
    GXSetChanCtrl(GX_COLOR0A0, GX_FALSE, GX_SRC_REG, GX_SRC_VTX, GX_LIGHT_NULL, GX_DF_NONE,
                  GX_AF_NONE);
    GXSetCullMode(GX_CULL_NONE);
    GXClearVtxDesc();
    GXSetVtxAttrFmt(GX_VTXFMT0, GX_VA_POS, GX_POS_XYZ, GX_F32, 0);
    GXSetVtxAttrFmt(GX_VTXFMT0, GX_VA_CLR0, GX_CLR_RGBA, GX_RGBA8, 0);
    GXSetVtxDesc(GX_VA_POS, GX_DIRECT);
    GXSetVtxDesc(GX_VA_CLR0, GX_DIRECT);
    GXLoadPosMtxImm(HSD_CObjGetViewingMtxPtrDirect(HSD_CObjGetCurrent()), GX_PNMTX0);
    GXSetCurrentMtx(GX_PNMTX0);
}

static void ftSn_GXLink_End(void)
{
    HSD_StateInvalidate(HSD_STATE_ALL);
    HSD_StateInitTev();
    HSD_ClearVtxDesc();
}

/* GXLink_SonicDrawTarget: a small light red circle (radius 5) around the homing target. */
void ftSn_GXLink_DrawTarget(HSD_GObj* gobj)
{
    Fighter* fp = GET_FIGHTER(gobj);
    ftSonic_SpecialNVars* mv = &ftSn_MV(fp)->specialn;
    int i;

    ftSn_GXLink_Init();
    GXBegin(GX_LINESTRIP, GX_VTXFMT0, 9);
    for (i = 0; i != 9; i++) {
        float angle = (float) ((float) i * ftSn_TAU * 0.125F);
        float x = cosf(angle) * 5.0F + mv->target.x;
        float y = sinf(angle) * 5.0F + mv->target.y;
        GXPosition3f32(x, y, 0.0F);
        GXColor4u8(0xFF, 0xB4, 0xB4, 0xFF);
    }
    ftSn_GXLink_End();
}

/* GXLink_SonicDrawRadius: the search range around the fighter, in white. */
void ftSn_GXLink_DrawRadius(HSD_GObj* gobj)
{
    Fighter* fp = GET_FIGHTER(gobj);
    float radius;
    int i;

    ftSn_GXLink_Init();
    radius = (float) (int) ftSn_DA(fp)->specialn_search_range;
    GXBegin(GX_LINESTRIP, GX_VTXFMT0, 33);
    for (i = 0; i != 33; i++) {
        float angle = (float) ((float) i * ftSn_TAU * 0.03125F);
        float x = radius * cosf(angle) + fp->cur_pos.x;
        float y = radius * sinf(angle) + fp->cur_pos.y;
        GXPosition3f32(x, y, fp->cur_pos.z);
        GXColor4u8(0xFF, 0xFF, 0xFF, 0xFF);
    }
    ftSn_GXLink_End();
}
