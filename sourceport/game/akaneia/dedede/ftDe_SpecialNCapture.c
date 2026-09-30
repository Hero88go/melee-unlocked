/* King Dedede (Akaneia), native: the other side of the inhale.
 *
 * What happens to a fighter or item Dedede pulls in, holds and spits out. These are Dedede's own
 * copies of the common Kirby routines (ftCo_CaptureKirby.c, ftCo_CaptureWaitKirby.c,
 * ftCo_ThrownKirby.c, itkirby_2F23.c): the same logic, with every Kirby attribute read replaced by
 * the matching Dedede attribute. The victim stays in the common CaptureKirby / CaptureWaitKirby /
 * ThrownKirbyStar / ThrownKirby motion states; Dedede swaps in his own callbacks where the Kirby
 * ones would read Kirby's attributes.
 *
 * Source: PlDe.dat ftFunction: SpecialN_OnVictim, ASID_CaputureKirbyWait_InterruptCB,
 * CaptureWait_CheckJumping, Dedede_CheckIfEatWait, Dedede_CheckEnterSpecialAirN_EatWait,
 * Dedede_UseStopWalkMomentum, SpecialN_CaptureThinkAccessory, Fighter_KirbyResetScale,
 * AS_ItemGrabbed, ItemGrab_Accessory, AS_EnterStarSpitState, ThrownStar_Phys, ThrownStar_Coll,
 * AS_StarSpitEnd, Thrown_Phys, StarSpitEnd_AccessoryCB, ItemSpawn_StarSpit. */
#include "ftDe.h"

#include <string.h>

#include <melee/ft/fighter.h>
#include <melee/ft/ftanim.h>
#include <melee/ft/ftcoll.h>
#include <melee/ft/ftcommon.h>
#include <melee/ft/ftparts.h>
#include <melee/ft/inlines.h>
#include <melee/ft/kinds/ftCommon/ftCo_Attack100.h>
#include <melee/ft/kinds/ftCommon/ftCo_CaptureCut.h>
#include <melee/ft/kinds/ftCommon/ftCo_Fall.h>
#include <melee/ft/kinds/ftCommon/ftCo_Lift.h>
#include <melee/ft/kinds/ftCommon/ftCo_Throw.h>
#include <melee/ft/kinds/ftCommon/ftCo_ThrownKirby.h>
#include <melee/ft/kinds/ftCommon/types.h>
#include <melee/ft/kinds/ftKirby/ftkirby.h>
#include <melee/it/inlines.h>
#include <melee/it/it_26B1.h>
#include <melee/it/it_2725.h>
#include <melee/it/item.h>
#include <melee/it/ithitbox.h>
#include <melee/it/kinds/itkirby_2F23.h>
#include <melee/lb/lb_00B0.h>
#include <melee/lb/lbvector.h>
#include <melee/mp/mpcoll.h>
#include <MSL/math.h>
#include <sysdolphin/baselib/gobj.h>
#include <sysdolphin/baselib/jobj.h>

static HSD_GObj* ThrowerOf(Fighter* fp)
{
    return (HSD_GObj*) MU_Z(fp->mv.co.thrownkirby.thrower_gobj);
}

/* Kirby's pull-in and shrink, shared by the fighter and the item version: move `offset` (the
 * distance still to cover) at most `limit` per frame, and shrink the model inside `shrink.x`. */
static float PullShrinkFactor(float dist, Vec2* shrink)
{
    return (float) ((double) (dist / shrink->x * shrink->y) + (1.0 - (double) shrink->y));
}

static float PullStep(float offset, float limit)
{
    float abs_offset = offset < 0.0f ? -offset : offset;
    if (limit >= abs_offset) {
        return offset;
    }
    return offset >= 0.0f ? limit * 1.0f : limit * -1.0f;
}

/* ---------------------------------------------------------------- captured fighter */

/* Fighter_KirbyResetScale (x21EC of the captured fighter) */
static void CaptureKirby_ResetScale(HSD_GObj* gobj)
{
    Fighter_UpdateModelScale(gobj);
}

/* SpecialN_CaptureThinkAccessory: each frame, pull the captured fighter toward Dedede's mouth
 * and shrink it as it gets close (ftCo_800BD39C with Dedede's mouth). */
static void CaptureKirby_Accessory(HSD_GObj* gobj)
{
    Fighter* fp = GET_FIGHTER(gobj);
    HSD_JObj* jobj = GET_JOBJ(gobj);
    float off_x = fp->mv.co.capturekirby.pos_offset.x;
    float off_y = fp->mv.co.capturekirby.pos_offset.y;
    float dist = sqrtf(off_x * off_x + off_y * off_y);
    Vec3 mouth;

    if (dist < fp->mv.co.capturekirby.x8.x) {
        float f = PullShrinkFactor(dist, &fp->mv.co.capturekirby.x8);
        Vec3 scale;
        scale.x = fp->mv.co.capturekirby.scale.x * f;
        scale.y = fp->mv.co.capturekirby.scale.y * f;
        scale.z = fp->mv.co.capturekirby.scale.z * f;
        HSD_JObjSetScale(jobj, &scale);
    }

    off_x = fp->mv.co.capturekirby.pos_offset.x - PullStep(off_x, fp->mv.co.capturekirby.x10.x);
    fp->mv.co.capturekirby.pos_offset.x = off_x;
    off_y = fp->mv.co.capturekirby.pos_offset.y - PullStep(off_y, fp->mv.co.capturekirby.x10.y);
    fp->mv.co.capturekirby.pos_offset.y = off_y;

    ftDe_SpecialN_GetMouthPos(fp->victim_gobj, &mouth);
    fp->cur_pos.x = mouth.x + off_x;
    fp->cur_pos.y = mouth.y + off_y;
    fp->cur_pos.z = mouth.z;
}

/* SpecialN_OnVictim: the grab box caught `gobj`; put it in CaptureKirby (ftCo_800BD1DC). */
void ftDe_SpecialN_OnVictim(HSD_GObj* gobj, HSD_GObj* dedede_gobj)
{
    Fighter* fp = GET_FIGHTER(gobj);
    Fighter* dfp = GET_FIGHTER(dedede_gobj);
    ftDe_DatAttrs* da;
    Vec3 mouth;

    ftCommon_8007DB58(gobj);
    ftCo_8009750C(gobj);
    ftCo_800DD168(gobj);
    fp->x1A5C = dedede_gobj;
    fp->victim_gobj = dedede_gobj;
    fp->x221B_b5 = false;
    fp->x221B_b7 = false;
    fp->facing_dir = -dfp->facing_dir;
    Fighter_ChangeMotionState(gobj, FTDE_CO_CAPTUREKIRBY, 0, 0.0f, 1.0f, 0.0f, NULL);
    ftCommon_8007D5D4(fp);

    da = ftDe_Attrs(dfp);
    ftDe_SpecialN_GetMouthPos(dedede_gobj, &mouth);
    fp->mv.co.capturekirby.pos_offset.x = fp->cur_pos.x - mouth.x;
    fp->mv.co.capturekirby.pos_offset.y = fp->cur_pos.y - mouth.y;
    fp->mv.co.capturekirby.x8.x = da->specialn_shrink_dist;
    fp->mv.co.capturekirby.x8.y = da->specialn_shrink_amount;
    fp->mv.co.capturekirby.x10.x = da->specialn_pull_max_x;
    fp->mv.co.capturekirby.x10.y = da->specialn_pull_max_y;
    HSD_JObjGetScale(GET_JOBJ(gobj), &fp->mv.co.capturekirby.scale);
    fp->x2226_b2 = true;

    ftCommon_InitGrab(fp, 0, da->specialn_hold_time);
    ftCommon_8007E2F4(fp, 0x1FF);
    ftAnim_8006EBA4(gobj);
    ftCommon_8007EBAC(fp, 1, 0);
    ftCommon_8007E2FC(gobj);
    fp->accessory1_cb = CaptureKirby_Accessory;
    fp->x21EC = CaptureKirby_ResetScale;
}

/* Dedede_CheckIfEatWait: Dedede is standing or floating with a full mouth. */
static bool Dedede_IsEatWait(HSD_GObj* dedede_gobj)
{
    FtMotionId msid = GET_FIGHTER(dedede_gobj)->motion_id;
    return msid == ftDe_MS_SpecialNEatWait || msid == ftDe_MS_SpecialAirNEatWait;
}

/* Dedede_CheckEnterSpecialAirN_EatWait: the victim's struggle hops a grounded Dedede into the air
 * (dir +1) or through a platform (dir -1). */
static void Dedede_StruggleHop(HSD_GObj* dedede_gobj, int dir)
{
    Fighter* dfp = GET_FIGHTER(dedede_gobj);
    if (dfp->ground_or_air == GA_Ground) {
        ftDe_SpecialN_EatWait_Fall(dedede_gobj);
        dfp->self_vel.y = ftDe_Attrs(dfp)->specialn_struggle_speed * (float) dir;
    }
}

/* Dedede_UseStopWalkMomentum: the victim's struggle nudges Dedede sideways. */
static void Dedede_StruggleNudge(HSD_GObj* dedede_gobj, float dir)
{
    Fighter* dfp = GET_FIGHTER(dedede_gobj);
    float speed = ftDe_Attrs(dfp)->specialn_struggle_speed * dir;
    if (dfp->ground_or_air == GA_Ground) {
        dfp->gr_vel = speed;
    } else {
        dfp->self_vel.x = speed;
    }
}

/* CaptureWait_CheckJumping (ftCo_800BD6EC): tapping up hops Dedede up, tapping down drops him
 * through a platform. */
static void CaptureWaitKirby_Jumping(HSD_GObj* gobj)
{
    Fighter* fp = GET_FIGHTER(gobj);
    ftCommonData* cd = p_ftCommonData;
    float stick_y = fp->input.lstick[0].y;

    if (stick_y >= cd->tap_jump_threshold && cd->tap_jump_window > fp->active_timer.lstick.y) {
        fp->active_timer.lstick.y = 254;
        Dedede_StruggleHop(fp->victim_gobj, +1);
        return;
    }
    if (stick_y <= cd->x464 && (float) fp->active_timer.lstick.y < cd->x468 &&
        ftKb_SpecialN_800F597C(fp->victim_gobj))
    {
        fp->active_timer.lstick.y = 254;
        Dedede_StruggleHop(fp->victim_gobj, -1);
        mpUpdateFloorSkip(&GET_FIGHTER(fp->victim_gobj)->coll_data);
    }
}

/* ASID_CaputureKirbyWait_InterruptCB: the captured fighter's IASA inside Dedede's mouth
 * (ftCo_CaptureWaitKirby_IASA with Dedede's attributes). */
void ftDe_CaptureWaitKirby_IASA(HSD_GObj* gobj)
{
    Fighter* fp = GET_FIGHTER(gobj);
    HSD_JObj* jobj = GET_JOBJ(gobj);
    Fighter* dfp = GET_FIGHTER(fp->victim_gobj);
    ftDe_DatAttrs* da = ftDe_Attrs(dfp);

    if (Dedede_IsEatWait(fp->victim_gobj)) {
        ftCommonData* cd = p_ftCommonData;
        float stick_x = fp->input.lstick[0].x;
        float abs_x = stick_x < 0.0f ? -stick_x : stick_x;

        CaptureWaitKirby_Jumping(gobj);
        if (abs_x >= cd->dash_smash_stick_threshold &&
            (float) fp->active_timer.lstick.x < (float) cd->dash_smash_window + cd->x44)
        {
            fp->active_timer.lstick.x = 254;
            Dedede_StruggleNudge(fp->victim_gobj, stick_x >= 0.0f ? 1.0f : -1.0f);
        }
    }

    fp->grab_timer -= da->specialn_hold_decay;
    /* m-ex stores the mash result converted to a float (1.0 or 0.0); the flag reads the same. */
    fp->mv.co.capturekirby.x18 = ftCommon_GrabMash(fp, da->specialn_mash_value);
    if (fp->grab_timer <= 0.0f) {
        HSD_GObj* dedede_gobj = fp->victim_gobj;
        fp->facing_dir = -dfp->facing_dir;
        HSD_JObjSetScale(jobj, &fp->mv.co.capturekirby.scale);
        ftCo_CaptureCut_Enter(gobj);
        ftCo_800DA698(dedede_gobj, false);
    }
}

/* ---------------------------------------------------------------- captured item */

/* The fields of it_802F23EC's item vars this code uses (itKirby2F23_ItemVars). */
#define KB_ITEM(ip) ((ip)->xDD4_itemVar.kirby2f23)

/* ItemGrab_Accessory: it_802F258C with Dedede's mouth. */
static void ItemCaptured_Accessory(HSD_GObj* gobj)
{
    Item* ip = GET_ITEM(gobj);
    HSD_JObj* jobj = GET_JOBJ(gobj);
    float off_x = KB_ITEM(ip).x1C8;
    float off_y = KB_ITEM(ip).x1CC;
    float dist = sqrtf(off_x * off_x + off_y * off_y);
    Vec3 mouth;

    if (dist < KB_ITEM(ip).x1D0) {
        Vec2 shrink = { KB_ITEM(ip).x1D0, KB_ITEM(ip).x1D4 };
        float f = PullShrinkFactor(dist, &shrink);
        Vec3 scale;
        scale.x = KB_ITEM(ip).x1E8.x * f;
        scale.y = KB_ITEM(ip).x1E8.y * f;
        scale.z = KB_ITEM(ip).x1E8.z * f;
        HSD_JObjSetScale(jobj, &scale);
    }

    off_x = KB_ITEM(ip).x1C8 - PullStep(off_x, KB_ITEM(ip).x1D8);
    KB_ITEM(ip).x1C8 = off_x;
    off_y = KB_ITEM(ip).x1CC - PullStep(off_y, KB_ITEM(ip).x1DC);
    KB_ITEM(ip).x1CC = off_y;

    ftDe_SpecialN_GetMouthPos(ip->grab_victim, &mouth);
    ip->pos.x = mouth.x + off_x;
    ip->pos.y = mouth.y + off_y;
    ip->pos.z = mouth.z;
}

/* AS_ItemGrabbed: it_802F23EC with Dedede's attributes. */
void ftDe_SpecialN_ItemCaptured(Item_GObj* gobj, HSD_GObj* dedede_gobj, float facing_dir)
{
    Item* ip = GET_ITEM(gobj);
    HSD_JObj* jobj = GET_JOBJ(gobj);
    ftDe_DatAttrs* da = ftDe_Attrs(GET_FIGHTER(dedede_gobj));
    Vec3 mouth;

    /* m-ex writes the owner to ip+4 here where it_802F23EC writes atk_victim (ip+D04); ip+4 is
     * the item's own gobj back pointer, so the native code follows it_802F23EC. */
    ip->atk_victim = dedede_gobj;
    ip->grab_victim = dedede_gobj;
    it_8026C220(gobj, dedede_gobj);
    ip->xDD0_flag.b1 = false;
    ip->facing_dir = facing_dir;
    ip->xBC_itemStateContainer = it_803F9450;
    Item_80268E5C(gobj, 0, 0);
    it_802762BC(ip);

    ftDe_SpecialN_GetMouthPos(dedede_gobj, &mouth);
    KB_ITEM(ip).x1C8 = ip->pos.x - mouth.x;
    KB_ITEM(ip).x1CC = ip->pos.y - mouth.y;
    KB_ITEM(ip).x1D0 = da->specialn_shrink_dist;
    KB_ITEM(ip).x1D4 = da->specialn_shrink_amount;
    KB_ITEM(ip).x1D8 = da->specialn_pull_max_x;
    KB_ITEM(ip).x1DC = da->specialn_pull_max_y;
    /* m-ex: hold time at ip+FC4 and the scale at ip+FC8..FD3, which runs 8 bytes past the end of
     * the 0xFCC-byte item. Native keeps the scale in x1E8 (Kirby's slot) and the time in x1F4. */
    KB_ITEM(ip).x1F4 = da->specialn_hold_time;
    HSD_JObjGetScale(jobj, &KB_ITEM(ip).x1E8);

    it_802756D0(gobj);
    it_80274ECC(gobj, true);
    it_80274C88(gobj);
    it_8026BD54(gobj);
    it_80273408(gobj);
    ip->on_accessory = ItemCaptured_Accessory;
}

/* ---------------------------------------------------------------- the spat star */

static void ThrownStar_Phys(HSD_GObj* gobj);
static void ThrownStar_Coll(HSD_GObj* gobj);
static void StarSpit_End(HSD_GObj* gobj);

/* AS_EnterStarSpitState: ftCo_800BDB58 with Dedede's attributes. The victim flies out inside
 * the star model (article 0) and hits whoever it touches. */
void ftDe_SpecialN_EnterStarSpit(HSD_GObj* gobj, HSD_GObj* dedede_gobj)
{
    Fighter* fp = GET_FIGHTER(gobj);
    Fighter* dfp = GET_FIGHTER(dedede_gobj);
    ftDe_DatAttrs* da = ftDe_Attrs(dfp);
    DISC_PTR(Article)* articles = (void*) DP(dfp->ft_data->x48_items);
    Article* star = DP(articles[ftDe_Article_StarModel]);
    ItemModelDesc* star_model = DP(star->x10_modelDesc);
    float scale_value;
    Vec3 scale;

    fp->facing_dir = -dfp->facing_dir;
    fp->self_vel.x = da->star_speed * dfp->facing_dir;
    fp->self_vel.y = 0.0f;
    fp->self_vel.z = 0.0f;
    fp->mv.co.thrownkirby.thrower_gobj = mu_addr32(dedede_gobj);
    fp->mv.co.thrownkirby.x4 = da->star_decel;
    fp->mv.co.thrownkirby.x8 = 0.0f;
    Fighter_UpdateModelScale(gobj);
    HSD_JObjGetScale(GET_JOBJ(gobj), &fp->mv.co.thrownkirby.scale);

    Fighter_ChangeMotionState(gobj, FTDE_CO_THROWNKIRBYSTAR, Ft_MF_SkipThrowException, 0.0f, 1.0f, 0.0f,
                              dedede_gobj);
    fp->phys_cb = ThrownStar_Phys;
    fp->coll_cb = ThrownStar_Coll;
    fp->take_dmg_2_cb = Fighter_UpdateModelScale;
    fp->take_dmg_cb = Fighter_UpdateModelScale;
    ftCommon_8007E2F4(fp, 0x1FF);
    fp->invisible = true;
    ftColl_8007B62C(gobj, 2);
    ftCommon_8007EFC0(fp, 1);
    ftCommon_InitGrab(fp, 0, da->star_time);
    ftAnim_8006EBA4(gobj);
    ftCommon_8007D5D4(fp);
    fp->mv.co.thrownkirby.x18_b0 = false;
    fp->mv.co.thrownkirby.x18_b1 = false;

    ftCommon_SetAccessory(fp, DP(star_model->x0_joint));
    scale_value = fp->co_attrs.xDC;
    scale.x = scale.y = scale.z = scale_value;
    HSD_JObjSetScale(fp->x20A0_accessory, &scale);
    lb_8000C2F8(fp->x20A0_accessory, fp->parts[ftParts_GetBoneIndex(fp, FtPart_YRotN)].joint);
    ftColl_8007ABD0(&fp->x914[0], (s32) fp->co_attrs.kirby_b_star_damage, gobj);

    fp->mv.co.thrownkirby.coll_box.top = da->star_coll_box[0] * scale_value;
    fp->mv.co.thrownkirby.coll_box.bottom = da->star_coll_box[1] * scale_value;
    fp->mv.co.thrownkirby.coll_box.left.x = da->star_coll_box[2] * scale_value;
    fp->mv.co.thrownkirby.coll_box.left.y = da->star_coll_box[3] * scale_value;
    fp->mv.co.thrownkirby.coll_box.right.x = da->star_coll_box[4] * scale_value;
    fp->mv.co.thrownkirby.coll_box.right.y = da->star_coll_box[5] * scale_value;
}

/* ThrownStar_Phys (ftCo_ThrownKirbyStar_Phys) */
static void ThrownStar_Phys(HSD_GObj* gobj)
{
    Fighter* fp = GET_FIGHTER(gobj);
    ftDe_DatAttrs* da = ftDe_Attrs(GET_FIGHTER(ThrowerOf(fp)));
    float vx = fp->self_vel.x;
    float vy = fp->self_vel.y;
    float speed = sqrtf(vx * vx + vy * vy);

    if (fp->mv.co.thrownkirby.x4 < speed) {
        float left = speed - fp->mv.co.thrownkirby.x4;
        fp->self_vel.x = vx * left / speed;
        fp->self_vel.y = vy * left / speed;
        fp->facing_dir = fp->self_vel.y < 0.0f ? -1.0f : 1.0f;
    } else {
        fp->self_vel.x = 0.0f;
    }

    fp->grab_timer -= da->star_time_decay;
    fp->mv.co.thrownkirby.x14 = ftCommon_GrabMash(fp, da->star_mash_value);
    if (fp->grab_timer <= 0.0f) {
        StarSpit_End(gobj);
    }
}

/* ThrownStar_Coll (ftCo_ThrownKirbyStar_Coll): the star bounces off walls and breaks when a
 * bounce turns it around. */
static void ThrownStar_Coll(HSD_GObj* gobj)
{
    Fighter* fp = GET_FIGHTER(gobj);
    Vec3 normal = { 0.0f, 0.0f, 0.0f };

    ftCo_800BDA74(gobj, &normal);
    if (normal.x != 0.0f || normal.y != 0.0f || normal.z != 0.0f) {
        float old_x = fp->self_vel.x;
        float old_y = fp->self_vel.y;
        lbVector_Mirror(&fp->self_vel, &normal);
        if (old_x * fp->self_vel.x + old_y * fp->self_vel.y < 0.0f) {
            StarSpit_End(gobj);
            fp->mv.co.thrownkirby.x8 = normal.x < 0.0f ? -1.0f : 1.0f;
        }
    }
}

/* StarSpitEnd_AccessoryCB (ftCo_800BE6AC): grow back to full size while tumbling. */
static void StarSpitEnd_Accessory(HSD_GObj* gobj)
{
    Fighter* fp = GET_FIGHTER(gobj);
    float total = fp->mv.co.thrownkirby.x10;
    float amount = fp->mv.co.thrownkirby.xC;
    float f = (float) ((double) ((total - fp->grab_timer) / total * amount) + (1.0 - (double) amount));
    Vec3 scale;
    scale.x = fp->mv.co.thrownkirby.scale.x * f;
    scale.y = fp->mv.co.thrownkirby.scale.y * f;
    scale.z = fp->mv.co.thrownkirby.scale.z * f;
    HSD_JObjSetScale(GET_JOBJ(gobj), &scale);
}

/* Thrown_Phys (ftCo_ThrownKirby_Phys) */
static void StarSpitEnd_Phys(HSD_GObj* gobj)
{
    Fighter* fp = GET_FIGHTER(gobj);
    ftDe_DatAttrs* da = ftDe_Attrs(GET_FIGHTER(ThrowerOf(fp)));
    fp->grab_timer -= da->star_time_decay;
    if (fp->mv.co.thrownkirby.x18_b0 || fp->grab_timer <= 0.0f) {
        Fighter_UpdateModelScale(gobj);
        ftCo_Fall_Enter(gobj);
    }
}

/* AS_StarSpitEnd (ftCo_800BE494): the star breaks and the fighter tumbles out. */
static void StarSpit_End(HSD_GObj* gobj)
{
    Fighter* fp = GET_FIGHTER(gobj);
    ftDe_DatAttrs* da = ftDe_Attrs(GET_FIGHTER(ThrowerOf(fp)));

    Fighter_ChangeMotionState(gobj, FTDE_CO_THROWNKIRBY, Ft_MF_Unk06, 0.0f, 1.0f, 0.0f, NULL);
    fp->phys_cb = StarSpitEnd_Phys;
    fp->take_dmg_2_cb = Fighter_UpdateModelScale;
    fp->take_dmg_cb = Fighter_UpdateModelScale;
    ftCommon_8007E2F4(fp, 0x1FF);
    fp->invisible = false;
    ftColl_8007B62C(gobj, 0);

    if (!fp->mv.co.thrownkirby.x18_b0) {
        float dir = fp->mv.co.thrownkirby.x8;
        if (dir == 0.0f) {
            dir = fp->self_vel.x >= 0.0f ? 1.0f : -1.0f;
        }
        fp->self_vel.x = dir * da->star_end_vel_x;
        fp->self_vel.y = da->star_end_vel_y;
    } else if (fp->kind == Ft_Kind_Kirby && fp->mv.co.thrownkirby.x18_b1) {
        ftKb_SpecialN_800F190C(gobj, fp->u.kb.hat.kind);
        ftKb_SpecialN_800EEEC4(gobj, fp->u.kb.hat.kind);
        fp->u.kb.hat.kind = Ft_Kind_Kirby;
    }

    fp->mv.co.thrownkirby.x10 = da->star_end_time;
    ftCommon_InitGrab(fp, 0, da->star_end_time);
    fp->mv.co.thrownkirby.xC = da->specialn_shrink_amount;
    ftAnim_8006EBA4(gobj);
    ftCommon_8007D5D4(fp);
    fp->accessory4_cb = StarSpitEnd_Accessory;
}

/* ---------------------------------------------------------------- the spat item star */

/* ItemSpawn_StarSpit: an eaten item comes back out as article 1. */
void ftDe_SpecialN_SpawnSpitStar(HSD_GObj* gobj, Vec3* pos, Vec3* vel, float lifetime, float decel)
{
    SpawnItem spawn;
    Item_GObj* item_gobj;

    memset(&spawn, 0, sizeof(spawn));
    spawn.x0_parent_gobj = gobj;
    spawn.x4_parent_gobj2 = gobj;
    spawn.kind = mu_ak_article_kind(gobj, ftDe_Article_SpitStar);
    spawn.pos.x = pos->x;
    spawn.pos.y = pos->y;
    spawn.pos.z = 0.0f;
    spawn.prev_pos = spawn.pos;
    spawn.facing_dir = -1.0f;
    spawn.x3C_damage = 0;
    spawn.vel = *vel;
    spawn.x44_flag.b0 = true;
    spawn.x40 = 0;

    item_gobj = Item_80268B18(&spawn);
    if (item_gobj != NULL) {
        Item* ip = GET_ITEM(item_gobj);
        ((itDe_SpitStarVars*) &ip->xDD4_itemVar)->decel = decel;
        it_80275158(item_gobj, lifetime);
        it_8026B3A8(item_gobj);
        Item_80268E5C(item_gobj, 0, 2);
        ip->xDCE_flag.b7 = false;
        it_802758D4(item_gobj);
    }
}
