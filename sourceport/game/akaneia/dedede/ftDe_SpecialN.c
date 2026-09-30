/* King Dedede (Akaneia), native: Neutral B, Inhale.
 *
 * Kirby's inhale with Dedede's attributes: Start, a Loop held with B, End on release. A fighter or
 * item pulled into the mouth is "eaten" (Eat, then EatWait), after which Dedede can walk, turn,
 * jump and spit it back out as a star (Spit / SpitItem). There is no swallow: A or B always spits.
 * The victim side (capture, pull-in, the star) is in ftDe_SpecialNCapture.c.
 *
 * Source: PlDe.dat ftFunction, the SpecialN* and SpecialAirN* routines. */
#include "ftDe.h"

#include <melee/ef/eflib.h>
#include <melee/ef/efsync.h>
#include <melee/ft/fighter.h>
#include <melee/ft/ft_081B.h>
#include <melee/ft/ft_084E.h>
#include <melee/ft/ft_0892.h>
#include <melee/ft/ftanim.h>
#include <melee/ft/ftcoll.h>
#include <melee/ft/ftcommon.h>
#include <melee/ft/ftdata.h>
#include <melee/ft/ftparts.h>
#include <melee/ft/ftwalkcommon.h>
#include <melee/ft/inlines.h>
#include <melee/ft/kinds/ftCommon/ftCo_CaptureKirby.h>
#include <melee/ft/kinds/ftCommon/ftCo_CaptureWaitKirby.h>
#include <melee/ft/kinds/ftCommon/ftCo_Fall.h>
#include <melee/ft/kinds/ftCommon/ftCo_Jump.h>
#include <melee/ft/kinds/ftCommon/ftCo_KneeBend.h>
#include <melee/ft/kinds/ftCommon/ftCo_Throw.h>
#include <melee/ft/kinds/ftCommon/forward.h>
#include <melee/it/kinds/it_2F28.h>
#include <melee/it/kinds/itkirby_2F23.h>
#include <melee/lb/lb_00B0.h>
#include <melee/lb/lbanim.h>

/* Motion flags the m-ex code passes when a state carries on across a ground/air change. */
#define FTDE_MF_CARRY_START 0x1EDA96 /* Start and Loop falling or landing */
#define FTDE_MF_CARRY_END 0x1FFFF6   /* End falling or landing */
#define FTDE_MF_CARRY (ftCommon_GroundAirColl_MF | Ft_MF_KeepGfx | Ft_MF_SkipModel)
#define FTDE_MF_CARRY_LOOP_LAND (ftCommon_GroundAirColl_MF | 0xA1A)

static void SpecialNGrab_Enter(HSD_GObj* gobj);
static void SpecialAirNGrab_Enter(HSD_GObj* gobj);
static void SpecialN_OnItem(HSD_GObj* gobj);
static void SpecialNEatWait_Enter(HSD_GObj* gobj);
static void SpecialAirNEatWait_Enter(HSD_GObj* gobj);

/* Arms the grab box: a fighter in it calls ftDe_SpecialN_OnVictim, an item SpecialN_OnItem, and
 * the grab itself enters `grab_cb`. */
static void SpecialN_SetGrabCallbacks(Fighter* fp, HSD_GObjEvent grab_cb)
{
    ftCommon_8007E2D0(fp, 16, grab_cb, SpecialN_OnItem, ftDe_SpecialN_OnVictim);
    fp->x2225_b1 = true;
}

static void SpecialN_ChangeAndReset(HSD_GObj* gobj, FtMotionId msid, MotionFlags flags)
{
    Fighter* fp = GET_FIGHTER(gobj);
    Fighter_ChangeMotionState(gobj, msid, flags, 0.0f, 1.0f, 0.0f, NULL);
    ftAnim_8006EBA4(gobj);
    ftCommon_8007E2F4(fp, 0x1FF);
}

/* SpecialN_GetInhaleOffset: the point in front of Dedede everything is pulled toward. */
void ftDe_SpecialN_GetMouthPos(HSD_GObj* gobj, Vec3* out)
{
    Fighter* fp = GET_FIGHTER(gobj);
    ftDe_DatAttrs* da = ftDe_Attrs(fp);
    out->x = da->specialn_mouth_x * fp->facing_dir + fp->cur_pos.x;
    out->y = fp->cur_pos.y + da->specialn_mouth_y;
    out->z = fp->cur_pos.z;
}

/* ---------------------------------------------------------------- entry */

/* SpecialN_EnterAirOrGround (both SpecialN and SpecialAirN) */
void ftDe_SpecialN_Enter(HSD_GObj* gobj)
{
    Fighter* fp = GET_FIGHTER(gobj);
    ftDe_FighterVars* fv = ftDe_Vars(fp);
    ftDe_DatAttrs* da = ftDe_Attrs(fp);
    FtMotionId msid = fp->ground_or_air != GA_Ground ? ftDe_MS_SpecialAirNStart : ftDe_MS_SpecialNStart;

    fp->throw_flags = 0;
    fp->cmd_vars[0] = 0;
    fp->cmd_vars[1] = 0;
    fv->specialn_x0 = 4;
    fv->specialn_loop_timer = da->specialn_min_loop_frames;
    fv->eatwalk_slow_len = lbAnim_8001E8F8(ftData_80085E50(fp, FTDE_SM_EATWALK_SLOW));
    fv->eatwalk_middle_len = lbAnim_8001E8F8(ftData_80085E50(fp, FTDE_SM_EATWALK_MIDDLE));
    fv->eatwalk_fast_len = lbAnim_8001E8F8(ftData_80085E50(fp, FTDE_SM_EATWALK_FAST));

    Fighter_ChangeMotionState(gobj, msid, 0, 0.0f, 1.0f, 0.0f, NULL);
    ftAnim_8006EBA4(gobj);
    SpecialN_SetGrabCallbacks(fp, fp->ground_or_air != GA_Ground ? SpecialAirNGrab_Enter : SpecialNGrab_Enter);
}

/* ---------------------------------------------------------------- Start / Loop / End */

/* The inhale effect, spawned on the frame the animation script sets cmd_vars[0]. */
static void SpecialN_SpawnInhaleEffect(HSD_GObj* gobj)
{
    Fighter* fp = GET_FIGHTER(gobj);
    if (fp->cmd_vars[0] != 0) {
        efSync_Spawn(0x1770, gobj, fp->parts[6].joint, &fp->facing_dir);
        fp->x2219_b0 = true;
        Fighter_SetEffectHitlagCallbacks(fp);
        fp->cmd_vars[0] = 0;
    }
}

/* SpecialNLoop_Enter */
static void SpecialNLoop_Enter(HSD_GObj* gobj)
{
    Fighter* fp = GET_FIGHTER(gobj);
    Fighter_ChangeMotionState(gobj, ftDe_MS_SpecialNLoop, 0x212, 0.0f, 1.0f, 0.0f, NULL);
    fp->x2222_b2 = true;
    Fighter_SetEffectHitlagCallbacks(fp);
    SpecialN_SetGrabCallbacks(fp, SpecialNGrab_Enter);
}

/* SpecialAirNLoop_Enter */
static void SpecialAirNLoop_Enter(HSD_GObj* gobj)
{
    Fighter* fp = GET_FIGHTER(gobj);
    Fighter_ChangeMotionState(gobj, ftDe_MS_SpecialAirNLoop, 0x212, 0.0f, 1.0f, 0.0f, NULL);
    fp->x2222_b2 = true;
    Fighter_SetEffectHitlagCallbacks(fp);
    SpecialN_SetGrabCallbacks(fp, SpecialAirNGrab_Enter);
}

void ftDe_SpecialNStart_Anim(HSD_GObj* gobj)
{
    SpecialN_SpawnInhaleEffect(gobj);
    if (!ftAnim_IsFramesRemaining(gobj)) {
        SpecialNLoop_Enter(gobj);
    }
}

void ftDe_SpecialAirNStart_Anim(HSD_GObj* gobj)
{
    SpecialN_SpawnInhaleEffect(gobj);
    if (!ftAnim_IsFramesRemaining(gobj)) {
        SpecialAirNLoop_Enter(gobj);
    }
}

void ftDe_SpecialNStart_IASA(HSD_GObj* gobj) {}
void ftDe_SpecialAirNStart_IASA(HSD_GObj* gobj) {}

void ftDe_SpecialNStart_Phys(HSD_GObj* gobj)
{
    ft_80084F3C(gobj);
}

void ftDe_SpecialAirNStart_Phys(HSD_GObj* gobj)
{
    ft_80084EEC(gobj);
}

/* Start and Loop falling off: both go to SpecialAirNStart at the current frame, and re-arm the
 * grab box with the ground grab callback (as the m-ex code does; SpecialNGrab_Coll moves a grab
 * that starts in the air into the air grab one frame later). */
static void SpecialNStartLoop_Fall(HSD_GObj* gobj)
{
    Fighter* fp = GET_FIGHTER(gobj);
    ftCommon_8007D5D4(fp);
    Fighter_ChangeMotionState(gobj, ftDe_MS_SpecialAirNStart, FTDE_MF_CARRY_START, fp->cur_anim_frame, 1.0f,
                              0.0f, NULL);
    Fighter_SetEffectHitlagCallbacks(fp);
    SpecialN_SetGrabCallbacks(fp, SpecialNGrab_Enter);
}

void ftDe_SpecialNStart_Coll(HSD_GObj* gobj)
{
    if (!ft_80082708(gobj)) {
        SpecialNStartLoop_Fall(gobj);
    }
}

/* SpecialAirNStart_CollPassLedge: landing during Start. */
static void SpecialAirNStart_Land(HSD_GObj* gobj)
{
    Fighter* fp = GET_FIGHTER(gobj);
    ftCommon_8007D7FC(fp);
    Fighter_ChangeMotionState(gobj, ftDe_MS_SpecialNStart, FTDE_MF_CARRY_START, fp->cur_anim_frame, 1.0f, 0.0f,
                              NULL);
    Fighter_SetEffectHitlagCallbacks(fp);
    SpecialN_SetGrabCallbacks(fp, SpecialNGrab_Enter);
}

/* The aerial inhale states keep the ECB from shrinking to the feet while they check for landing. */
static void SpecialAirN_CollWithFullEcb(HSD_GObj* gobj, HSD_GObjEvent on_land)
{
    Fighter* fp = GET_FIGHTER(gobj);
    u32 saved = fp->coll_data.x130_flags;
    fp->coll_data.x130_flags = saved | 0x10;
    fp->coll_data.desired_ecb.bottom.y = 0.0f; /* fp+780 */
    ft_80082C74(gobj, on_land);
    fp->coll_data.x130_flags = saved;
}

void ftDe_SpecialAirNStart_Coll(HSD_GObj* gobj)
{
    SpecialAirN_CollWithFullEcb(gobj, SpecialAirNStart_Land);
}

void ftDe_SpecialNLoop_Anim(HSD_GObj* gobj) {}
void ftDe_SpecialAirNLoop_Anim(HSD_GObj* gobj) {}

/* SpecialNEnd_Enter */
static void SpecialNEnd_Enter(HSD_GObj* gobj)
{
    Fighter_ChangeMotionState(gobj, ftDe_MS_SpecialNEnd, 0, 0.0f, 1.0f, 0.0f, NULL);
}

/* SpecialAirNEnd_Enter */
static void SpecialAirNEnd_Enter(HSD_GObj* gobj)
{
    Fighter_ChangeMotionState(gobj, ftDe_MS_SpecialAirNEnd, 0, 0.0f, 1.0f, 0.0f, NULL);
}

/* The loop runs at least specialn_min_loop_frames, then ends as soon as B is let go. */
void ftDe_SpecialNLoop_IASA(HSD_GObj* gobj)
{
    Fighter* fp = GET_FIGHTER(gobj);
    ftDe_FighterVars* fv = ftDe_Vars(fp);
    if (fv->specialn_loop_timer != 0) {
        fv->specialn_loop_timer--;
        return;
    }
    if ((fp->input.held_buttons[0] & 0x200) == 0) {
        SpecialNEnd_Enter(gobj);
    }
}

void ftDe_SpecialAirNLoop_IASA(HSD_GObj* gobj)
{
    Fighter* fp = GET_FIGHTER(gobj);
    ftDe_FighterVars* fv = ftDe_Vars(fp);
    if (fv->specialn_loop_timer != 0) {
        fv->specialn_loop_timer--;
        return;
    }
    if ((fp->input.held_buttons[0] & 0x200) == 0) {
        SpecialAirNEnd_Enter(gobj);
    }
}

void ftDe_SpecialNLoop_Phys(HSD_GObj* gobj)
{
    ft_80084F3C(gobj);
}

void ftDe_SpecialAirNLoop_Phys(HSD_GObj* gobj)
{
    ft_80084EEC(gobj);
}

void ftDe_SpecialNLoop_Coll(HSD_GObj* gobj)
{
    if (!ft_80082708(gobj)) {
        SpecialNStartLoop_Fall(gobj);
    }
}

/* SpecialAirNLoop_CollPassLedge: landing during the loop. */
static void SpecialAirNLoop_Land(HSD_GObj* gobj)
{
    Fighter* fp = GET_FIGHTER(gobj);
    ftCommon_8007D7FC(fp);
    Fighter_ChangeMotionState(gobj, ftDe_MS_SpecialNLoop, FTDE_MF_CARRY_LOOP_LAND, fp->cur_anim_frame, 1.0f,
                              0.0f, NULL);
    Fighter_SetEffectHitlagCallbacks(fp);
    SpecialN_SetGrabCallbacks(fp, SpecialNGrab_Enter);
}

void ftDe_SpecialAirNLoop_Coll(HSD_GObj* gobj)
{
    SpecialAirN_CollWithFullEcb(gobj, SpecialAirNLoop_Land);
}

void ftDe_SpecialNEnd_Anim(HSD_GObj* gobj)
{
    if (!ftAnim_IsFramesRemaining(gobj)) {
        ft_8008A2BC(gobj);
    }
}

void ftDe_SpecialAirNEnd_Anim(HSD_GObj* gobj)
{
    if (!ftAnim_IsFramesRemaining(gobj)) {
        ftCo_Fall_Enter(gobj);
    }
}

void ftDe_SpecialNEnd_IASA(HSD_GObj* gobj) {}
void ftDe_SpecialAirNEnd_IASA(HSD_GObj* gobj) {}

void ftDe_SpecialNEnd_Phys(HSD_GObj* gobj)
{
    ft_80084F3C(gobj);
}

void ftDe_SpecialAirNEnd_Phys(HSD_GObj* gobj)
{
    ft_80084EEC(gobj);
}

void ftDe_SpecialNEnd_Coll(HSD_GObj* gobj)
{
    Fighter* fp = GET_FIGHTER(gobj);
    if (!ft_80082708(gobj)) {
        ftCommon_8007D5D4(fp);
        Fighter_ChangeMotionState(gobj, ftDe_MS_SpecialAirNEnd, FTDE_MF_CARRY_END, fp->cur_anim_frame, 1.0f, 0.0f,
                                  NULL);
    }
}

/* SpecialAirNEnd_CollPassLedge */
static void SpecialAirNEnd_Land(HSD_GObj* gobj)
{
    Fighter* fp = GET_FIGHTER(gobj);
    ftCommon_8007D7FC(fp);
    Fighter_ChangeMotionState(gobj, ftDe_MS_SpecialNEnd, FTDE_MF_CARRY_END, fp->cur_anim_frame, 1.0f, 0.0f, NULL);
}

void ftDe_SpecialAirNEnd_Coll(HSD_GObj* gobj)
{
    SpecialAirN_CollWithFullEcb(gobj, SpecialAirNEnd_Land);
}

/* ---------------------------------------------------------------- Grab (pulling in) */

/* SpecialNGrab_Enter / SpecialAirNGrab_Enter: the grab box touched a fighter. */
static void SpecialN_GrabEnter(HSD_GObj* gobj, FtMotionId msid)
{
    Fighter* fp = GET_FIGHTER(gobj);
    Fighter_ChangeMotionState(gobj, msid, 0x212, 0.0f, 1.0f, 0.0f, NULL);
    fp->x2222_b2 = true;
    ftDe_Vars(fp)->captured_item = false;
    ftCommon_8007E2F4(fp, 0x1FF);
}

static void SpecialNGrab_Enter(HSD_GObj* gobj)
{
    SpecialN_GrabEnter(gobj, ftDe_MS_SpecialNGrab);
}

static void SpecialAirNGrab_Enter(HSD_GObj* gobj)
{
    SpecialN_GrabEnter(gobj, ftDe_MS_SpecialAirNGrab);
}

/* SpecialN_OnItem: the grab box touched an item. */
static void SpecialN_OnItem(HSD_GObj* gobj)
{
    Fighter* fp = GET_FIGHTER(gobj);
    ftDe_SpecialN_ItemCaptured(fp->target_item_gobj, gobj, -fp->facing_dir);
    Fighter_ChangeMotionState(gobj, fp->ground_or_air == GA_Air ? ftDe_MS_SpecialAirNGrabItem : ftDe_MS_SpecialNGrabItem,
                              0x212, 0.0f, 1.0f, 0.0f, NULL);
    fp->x2222_b2 = true;
    ftDe_Vars(fp)->captured_item = true;
    ftCommon_8007E2F4(fp, 0x1FF);
}

/* Once the pulled fighter or item is close enough to the mouth, Dedede eats it. */
static void SpecialN_EnterEat(HSD_GObj* gobj)
{
    Fighter* fp = GET_FIGHTER(gobj);
    FtMotionId msid = fp->ground_or_air == GA_Air ? ftDe_MS_SpecialAirNEat : ftDe_MS_SpecialNEat;
    Fighter_ChangeMotionState(gobj, msid, 0x10, 0.0f, 1.0f, 0.0f, NULL);
    ftCommon_8007E2F4(fp, 0x1FF);
}

/* SpecialNGrab_Anim (also SpecialAirNGrab_Anim) */
void ftDe_SpecialNGrab_Anim(HSD_GObj* gobj)
{
    Fighter* fp = GET_FIGHTER(gobj);
    ftDe_DatAttrs* da = ftDe_Attrs(fp);
    float range = da->specialn_inhale_range;
    Vec3 mouth;

    ftDe_SpecialN_GetMouthPos(gobj, &mouth);
    if (ftCo_800BD19C(fp->victim_gobj, &mouth) < range * range) {
        ftCo_800BD620(fp->victim_gobj);
        GET_FIGHTER(fp->victim_gobj)->input_cb = ftDe_CaptureWaitKirby_IASA;
        SpecialN_EnterEat(gobj);
    }
}

void ftDe_SpecialAirNGrab_Anim(HSD_GObj* gobj)
{
    ftDe_SpecialNGrab_Anim(gobj);
}

/* SpecialNGrabItem_Anim (also SpecialAirNGrabItem_Anim) */
void ftDe_SpecialNGrabItem_Anim(HSD_GObj* gobj)
{
    Fighter* fp = GET_FIGHTER(gobj);
    ftDe_DatAttrs* da = ftDe_Attrs(fp);
    float range = da->specialn_inhale_range;
    Vec3 mouth;

    ftDe_SpecialN_GetMouthPos(gobj, &mouth);
    if (it_802F23AC(fp->target_item_gobj, &mouth) < range * range) {
        it_802F2810(fp->target_item_gobj);
        SpecialN_EnterEat(gobj);
    }
}

void ftDe_SpecialAirNGrabItem_Anim(HSD_GObj* gobj)
{
    ftDe_SpecialNGrabItem_Anim(gobj);
}

void ftDe_SpecialNGrab_IASA(HSD_GObj* gobj) {}
void ftDe_SpecialAirNGrab_IASA(HSD_GObj* gobj) {}
void ftDe_SpecialNGrabItem_IASA(HSD_GObj* gobj) {}
void ftDe_SpecialAirNGrabItem_IASA(HSD_GObj* gobj) {}

void ftDe_SpecialNGrab_Phys(HSD_GObj* gobj)
{
    ft_80084F3C(gobj);
}

void ftDe_SpecialNGrabItem_Phys(HSD_GObj* gobj)
{
    ft_80084F3C(gobj);
}

void ftDe_SpecialAirNGrab_Phys(HSD_GObj* gobj)
{
    ft_80084EEC(gobj);
}

void ftDe_SpecialAirNGrabItem_Phys(HSD_GObj* gobj)
{
    ft_80084EEC(gobj);
}

/* Ground/air hand-offs that keep the current frame. `ground` true lands, false falls. */
static void SpecialN_Carry(HSD_GObj* gobj, FtMotionId msid, MotionFlags flags, bool ground, bool reset_cmd)
{
    Fighter* fp = GET_FIGHTER(gobj);
    if (ground) {
        ftCommon_8007D7FC(fp);
    } else {
        ftCommon_8007D5D4(fp);
    }
    Fighter_ChangeMotionState(gobj, msid, flags, fp->cur_anim_frame, 1.0f, 0.0f, NULL);
    if (reset_cmd) {
        ftCommon_8007E2F4(fp, 0x1FF);
    }
}

/* SpecialNGrab_PassLedgeCB */
static void SpecialNGrab_Fall(HSD_GObj* gobj)
{
    SpecialN_Carry(gobj, ftDe_MS_SpecialAirNGrab, FTDE_MF_CARRY, false, true);
}

/* SpecialNGrabItem_PassLedgeCB */
static void SpecialNGrabItem_Fall(HSD_GObj* gobj)
{
    SpecialN_Carry(gobj, ftDe_MS_SpecialAirNGrabItem, FTDE_MF_CARRY, false, true);
}

/* SpecialAirNGrab_CollPassLedge */
static void SpecialAirNGrab_Land(HSD_GObj* gobj)
{
    SpecialN_Carry(gobj, ftDe_MS_SpecialNGrab, FTDE_MF_CARRY, true, true);
}

/* SpecialAirNGrabItem_CollPassLedge */
static void SpecialAirNGrabItem_Land(HSD_GObj* gobj)
{
    SpecialN_Carry(gobj, ftDe_MS_SpecialNGrabItem, FTDE_MF_CARRY, true, true);
}

void ftDe_SpecialNGrab_Coll(HSD_GObj* gobj)
{
    ft_8008403C(gobj, SpecialNGrab_Fall);
}

void ftDe_SpecialNGrabItem_Coll(HSD_GObj* gobj)
{
    ft_8008403C(gobj, SpecialNGrabItem_Fall);
}

void ftDe_SpecialAirNGrab_Coll(HSD_GObj* gobj)
{
    SpecialAirN_CollWithFullEcb(gobj, SpecialAirNGrab_Land);
}

/* The item grab does not hold the ECB open. */
void ftDe_SpecialAirNGrabItem_Coll(HSD_GObj* gobj)
{
    ft_80082C74(gobj, SpecialAirNGrabItem_Land);
}

/* ---------------------------------------------------------------- Eat */

void ftDe_SpecialNEat_Anim(HSD_GObj* gobj)
{
    if (!ftAnim_IsFramesRemaining(gobj)) {
        SpecialNEatWait_Enter(gobj);
    }
}

void ftDe_SpecialAirNEat_Anim(HSD_GObj* gobj)
{
    Fighter* fp = GET_FIGHTER(gobj);
    if (!ftAnim_IsFramesRemaining(gobj)) {
        ftCommon_8007D5D4(fp);
        SpecialN_ChangeAndReset(gobj, ftDe_MS_SpecialAirNEatWait, 0x10);
    }
}

void ftDe_SpecialNEat_IASA(HSD_GObj* gobj) {}
void ftDe_SpecialAirNEat_IASA(HSD_GObj* gobj) {}

void ftDe_SpecialNEat_Phys(HSD_GObj* gobj)
{
    ft_80084F3C(gobj);
}

void ftDe_SpecialAirNEat_Phys(HSD_GObj* gobj)
{
    ft_80084EEC(gobj);
}

/* SpecialNEat_CollTransition */
static void SpecialNEat_Fall(HSD_GObj* gobj)
{
    SpecialN_Carry(gobj, ftDe_MS_SpecialAirNEat, FTDE_MF_CARRY, false, true);
}

/* SpecialAirNEat_CollTransition */
static void SpecialAirNEat_Land(HSD_GObj* gobj)
{
    SpecialN_Carry(gobj, ftDe_MS_SpecialNEat, FTDE_MF_CARRY, true, true);
}

void ftDe_SpecialNEat_Coll(HSD_GObj* gobj)
{
    ft_8008403C(gobj, SpecialNEat_Fall);
}

void ftDe_SpecialAirNEat_Coll(HSD_GObj* gobj)
{
    ft_80082C74(gobj, SpecialAirNEat_Land);
}

/* ---------------------------------------------------------------- EatWait and friends */

/* SpecialNEatWait_Enter */
static void SpecialNEatWait_Enter(HSD_GObj* gobj)
{
    SpecialN_ChangeAndReset(gobj, ftDe_MS_SpecialNEatWait, 0x10);
}

/* SpecialAirNEatWait_Enter: also the fall callback of the grounded full-mouth states. */
static void SpecialAirNEatWait_Enter(HSD_GObj* gobj)
{
    Fighter* fp = GET_FIGHTER(gobj);
    ftCommon_8007D5D4(fp);
    SpecialN_ChangeAndReset(gobj, ftDe_MS_SpecialAirNEatWait, 0x90);
}

/* SpecialNEatWait_CollPassLedge: walking off an edge with a full mouth. Also used by the
 * captured fighter's struggle (ftDe_SpecialNCapture.c) to hop Dedede off a platform. */
void ftDe_SpecialN_EatWait_Fall(HSD_GObj* gobj)
{
    SpecialN_Carry(gobj, ftDe_MS_SpecialAirNEatWait, ftCommon_GroundAirColl_MF | Ft_MF_SkipModel, false, true);
}

/* SpecialNEatLanding_Enter */
static void SpecialNEatLanding_Enter(HSD_GObj* gobj)
{
    Fighter* fp = GET_FIGHTER(gobj);
    ftCommon_8007D7FC(fp);
    Fighter_ChangeMotionState(gobj, ftDe_MS_SpecialNEatLanding, 0x12, 0.0f, 1.0f, 0.0f, NULL);
    ftAnim_8006EBA4(gobj);
}

/* SpecialNEatWait_CheckSpit: A or B spits the mouth's content. Returns true when it did. */
static bool SpecialNEatWait_CheckSpit(HSD_GObj* gobj)
{
    Fighter* fp = GET_FIGHTER(gobj);
    bool air = fp->ground_or_air == GA_Air;
    FtMotionId msid;

    if ((fp->input.pressed_buttons & 0x300) == 0) {
        return false;
    }
    if (!ftDe_Vars(fp)->captured_item) {
        if (fp->victim_gobj == NULL) {
            return false;
        }
        msid = air ? ftDe_MS_SpecialAirNSpit : ftDe_MS_SpecialNSpit;
    } else {
        if (fp->target_item_gobj == NULL) {
            return false;
        }
        msid = air ? ftDe_MS_SpecialAirNSpitItem : ftDe_MS_SpecialNSpitItem;
    }
    Fighter_ChangeMotionState(gobj, msid, 0x12, 0.0f, 1.0f, 0.0f, NULL);
    fp->x2222_b2 = true;
    ftAnim_8006EBA4(gobj);
    ftCommon_8007E2F4(fp, 0x1FF);
    return true;
}

/* SpecialN_ASWalk: the walk with a full mouth (callback of ftWalkCommon_800DFEC8 too). */
static void SpecialN_Walk(HSD_GObj* gobj, float anim_start)
{
    Fighter* fp = GET_FIGHTER(gobj);
    ftDe_FighterVars* fv = ftDe_Vars(fp);
    ftDe_DatAttrs* da = ftDe_Attrs(fp);
    ftWalkCommon_800DFCA4(gobj, ftDe_MS_SpecialNEatWalkSlow, 0x10, anim_start, fv->eatwalk_slow_len,
                          fv->eatwalk_middle_len, fv->eatwalk_fast_len, fp->co_attrs.slow_walk_max,
                          fp->co_attrs.mid_walk_point, fp->co_attrs.fast_walk_min, da->specialn_walk_speed);
    ftCommon_8007E2F4(fp, 0x1FF);
}

/* SpecialNEatJump1_Enter */
static void SpecialNEatJump1_Enter(HSD_GObj* gobj)
{
    Fighter* fp = GET_FIGHTER(gobj);
    ftDe_MotionVars* mv = ftDe_MV(fp);
    mv->specialn.x0 = 0;
    mv->specialn.jump_input = ftCo_Jump_GetInput(gobj);
    SpecialN_ChangeAndReset(gobj, ftDe_MS_SpecialNEatJump1, 0x92);
}

/* Stick past specialn_turn_stick against the facing direction turns around. */
static bool SpecialN_WantsTurn(Fighter* fp)
{
    ftDe_DatAttrs* da = ftDe_Attrs(fp);
    float stick_x = fp->input.lstick[0].x;
    float abs_x = stick_x < 0.0f ? -stick_x : stick_x;
    if (!(abs_x >= da->specialn_turn_stick)) {
        return false;
    }
    return (stick_x < 0.0f && fp->facing_dir == 1.0f) || (stick_x > 0.0f && fp->facing_dir == -1.0f);
}

void ftDe_SpecialNEatWait_Anim(HSD_GObj* gobj) {}
void ftDe_SpecialAirNEatWait_Anim(HSD_GObj* gobj) {}

void ftDe_SpecialNEatWait_IASA(HSD_GObj* gobj)
{
    Fighter* fp = GET_FIGHTER(gobj);
    if (SpecialNEatWait_CheckSpit(gobj)) {
        return;
    }
    if (SpecialN_WantsTurn(fp)) {
        SpecialN_ChangeAndReset(gobj, ftDe_MS_SpecialNEatTurn, 0x92); /* SpecialNEatTurn_Enter */
        return;
    }
    if (ftCo_Jump_GetInput(gobj) != 0) {
        SpecialNEatJump1_Enter(gobj);
        return;
    }
    if (ftWalkCommon_800DFC70(gobj)) {
        SpecialN_Walk(gobj, 0.0f);
    }
}

void ftDe_SpecialAirNEatWait_IASA(HSD_GObj* gobj)
{
    Fighter* fp = GET_FIGHTER(gobj);
    if (SpecialNEatWait_CheckSpit(gobj)) {
        return;
    }
    if (SpecialN_WantsTurn(fp)) {
        SpecialN_ChangeAndReset(gobj, ftDe_MS_SpecialAirNEatTurn, 0x92);
    }
}

void ftDe_SpecialNEatWait_Phys(HSD_GObj* gobj)
{
    ft_80084F3C(gobj);
}

void ftDe_SpecialAirNEatWait_Phys(HSD_GObj* gobj)
{
    ft_80084EEC(gobj);
}

void ftDe_SpecialNEatWait_Coll(HSD_GObj* gobj)
{
    ft_8008403C(gobj, ftDe_SpecialN_EatWait_Fall);
}

void ftDe_SpecialAirNEatWait_Coll(HSD_GObj* gobj)
{
    ft_80082C74(gobj, SpecialNEatLanding_Enter);
}

/* ---------------------------------------------------------------- EatTurn */

void ftDe_SpecialNEatTurn_Anim(HSD_GObj* gobj)
{
    Fighter* fp = GET_FIGHTER(gobj);
    if (!ftAnim_IsFramesRemaining(gobj)) {
        fp->facing_dir = -fp->facing_dir;
        SpecialNEatWait_Enter(gobj);
    }
}

void ftDe_SpecialAirNEatTurn_Anim(HSD_GObj* gobj)
{
    Fighter* fp = GET_FIGHTER(gobj);
    if (!ftAnim_IsFramesRemaining(gobj)) {
        fp->facing_dir = -fp->facing_dir;
        SpecialAirNEatWait_Enter(gobj);
    }
}

void ftDe_SpecialNEatTurn_IASA(HSD_GObj* gobj) {}
void ftDe_SpecialAirNEatTurn_IASA(HSD_GObj* gobj) {}

void ftDe_SpecialNEatTurn_Phys(HSD_GObj* gobj)
{
    ft_80084F3C(gobj);
}

void ftDe_SpecialAirNEatTurn_Phys(HSD_GObj* gobj)
{
    ft_80084EEC(gobj);
}

/* SpecialAirNEatTurn_Trans: falling during the turn (no command reset, as in the m-ex code). */
static void SpecialNEatTurn_Fall(HSD_GObj* gobj)
{
    SpecialN_Carry(gobj, ftDe_MS_SpecialAirNEatTurn, FTDE_MF_CARRY, false, false);
}

/* SpecialNEatTurn_Trans: landing during the aerial turn. */
static void SpecialAirNEatTurn_Land(HSD_GObj* gobj)
{
    SpecialN_Carry(gobj, ftDe_MS_SpecialNEatTurn, FTDE_MF_CARRY, true, true);
}

void ftDe_SpecialNEatTurn_Coll(HSD_GObj* gobj)
{
    ft_8008403C(gobj, SpecialNEatTurn_Fall);
}

void ftDe_SpecialAirNEatTurn_Coll(HSD_GObj* gobj)
{
    ft_80082C74(gobj, SpecialAirNEatTurn_Land);
}

/* ---------------------------------------------------------------- EatWalk */

void ftDe_SpecialNEatWalk_Anim(HSD_GObj* gobj)
{
    ftWalkCommon_800DFDDC(gobj);
}

void ftDe_SpecialNEatWalk_IASA(HSD_GObj* gobj)
{
    if (ftCo_Jump_GetInput(gobj) != 0) {
        SpecialNEatJump1_Enter(gobj);
    } else if (!ft_8008A1FC(gobj)) {
        ftWalkCommon_800DFEC8(gobj, SpecialN_Walk);
    } else {
        SpecialN_ChangeAndReset(gobj, ftDe_MS_SpecialNEatWait, 0x10);
    }
}

void ftDe_SpecialNEatWalk_Phys(HSD_GObj* gobj)
{
    ftWalkCommon_800E0060(gobj);
}

void ftDe_SpecialNEatWalk_Coll(HSD_GObj* gobj)
{
    ft_8008403C(gobj, SpecialAirNEatWait_Enter);
}

/* ---------------------------------------------------------------- EatJump / EatLanding */

/* SpecialNEatJump2_Enter */
static void SpecialNEatJump2_Enter(HSD_GObj* gobj)
{
    Fighter* fp = GET_FIGHTER(gobj);
    ftDe_DatAttrs* da = ftDe_Attrs(fp);
    ftCommon_8007D5D4(fp);
    Fighter_ChangeMotionState(gobj, ftDe_MS_SpecialNEatJump2, 0x92, 0.0f, 1.0f, 0.0f, NULL);
    ftAnim_8006EBA4(gobj);
    ftCo_800CB110(gobj, true, da->specialn_jump_height);
}

void ftDe_SpecialNEatJump1_Anim(HSD_GObj* gobj)
{
    if (!ftAnim_IsFramesRemaining(gobj)) {
        SpecialNEatJump2_Enter(gobj);
    }
}

void ftDe_SpecialNEatJump1_IASA(HSD_GObj* gobj)
{
    ftCo_KneeBend_Check_ShortHop(gobj);
}

void ftDe_SpecialNEatJump1_Phys(HSD_GObj* gobj)
{
    ft_80084F3C(gobj);
}

void ftDe_SpecialNEatJump1_Coll(HSD_GObj* gobj)
{
    ft_8008403C(gobj, SpecialAirNEatWait_Enter);
}

void ftDe_SpecialNEatJump2_Anim(HSD_GObj* gobj) {}

void ftDe_SpecialNEatJump2_IASA(HSD_GObj* gobj)
{
    SpecialNEatWait_CheckSpit(gobj);
}

/* The first frame of the rise skips air physics. */
void ftDe_SpecialNEatJump2_Phys(HSD_GObj* gobj)
{
    Fighter* fp = GET_FIGHTER(gobj);
    ftDe_MotionVars* mv = ftDe_MV(fp);
    if (mv->specialn.jump_input == 0) {
        mv->specialn.jump_input = 1;
        return;
    }
    ft_80084DB0(gobj);
}

void ftDe_SpecialNEatJump2_Coll(HSD_GObj* gobj)
{
    ft_80082C74(gobj, SpecialNEatLanding_Enter);
}

void ftDe_SpecialNEatLanding_Anim(HSD_GObj* gobj)
{
    if (!ftAnim_IsFramesRemaining(gobj)) {
        SpecialNEatWait_Enter(gobj);
    }
}

void ftDe_SpecialNEatLanding_IASA(HSD_GObj* gobj) {}

void ftDe_SpecialNEatLanding_Phys(HSD_GObj* gobj)
{
    ft_80084F3C(gobj);
}

void ftDe_SpecialNEatLanding_Coll(HSD_GObj* gobj)
{
    ft_8008403C(gobj, SpecialAirNEatWait_Enter);
}

/* ---------------------------------------------------------------- Spit */

/* SpecialNSpit_CheckToSpawnStarSpit: on the script's frame, the held fighter becomes a star. */
static void SpecialNSpit_Release(HSD_GObj* gobj)
{
    Fighter* fp = GET_FIGHTER(gobj);
    HSD_GObj* victim_gobj;

    if (fp->cmd_vars[0] == 0) {
        return;
    }
    victim_gobj = fp->victim_gobj;
    if (victim_gobj == NULL) {
        return;
    }
    ftCommon_8007E2F4(fp, 0);
    ftCo_800DE2CC(gobj, victim_gobj);
    ftDe_SpecialN_EnterStarSpit(victim_gobj, gobj);
    ftColl_8007B8CC(GET_FIGHTER(victim_gobj), gobj);
    fp->cmd_vars[0] = 0;
}

/* SpecialNSpitItem_CheckToSpawnStarSpit: an eaten item comes back out as a star projectile. */
static void SpecialNSpitItem_Release(HSD_GObj* gobj)
{
    Fighter* fp = GET_FIGHTER(gobj);
    ftDe_DatAttrs* da;
    Vec3 pos, vel;

    if (fp->cmd_vars[0] == 0 || fp->target_item_gobj == NULL) {
        return;
    }
    da = ftDe_Attrs(fp);
    ftCommon_8007E2F4(fp, 0);
    /* Fighter_Part 52 (FtPart_TransN2 in the decomp's enum) */
    lb_8000B1CC(fp->parts[ftParts_GetBoneIndex(fp, 52)].joint, NULL, &pos);
    vel.x = fp->facing_dir * da->star_speed;
    vel.y = 0.0f;
    vel.z = 0.0f;
    ftDe_SpecialN_SpawnSpitStar(gobj, &pos, &vel, 30.0f, 0.13f);
    it_802F28C8(fp->target_item_gobj, 0, 0.0f);
    fp->x1A64 = NULL;
    fp->target_item_gobj = NULL;
    fp->cmd_vars[0] = 0;
}

void ftDe_SpecialNSpit_Anim(HSD_GObj* gobj)
{
    SpecialNSpit_Release(gobj);
    if (!ftAnim_IsFramesRemaining(gobj)) {
        ft_8008A2BC(gobj);
    }
}

void ftDe_SpecialAirNSpit_Anim(HSD_GObj* gobj)
{
    SpecialNSpit_Release(gobj);
    if (!ftAnim_IsFramesRemaining(gobj)) {
        ftCo_Fall_Enter(gobj);
    }
}

void ftDe_SpecialNSpitItem_Anim(HSD_GObj* gobj)
{
    SpecialNSpitItem_Release(gobj);
    if (!ftAnim_IsFramesRemaining(gobj)) {
        ft_8008A2BC(gobj);
    }
}

void ftDe_SpecialAirNSpitItem_Anim(HSD_GObj* gobj)
{
    SpecialNSpitItem_Release(gobj);
    if (!ftAnim_IsFramesRemaining(gobj)) {
        ftCo_Fall_Enter(gobj);
    }
}

void ftDe_SpecialNSpit_IASA(HSD_GObj* gobj) {}
void ftDe_SpecialAirNSpit_IASA(HSD_GObj* gobj) {}
void ftDe_SpecialNSpitItem_IASA(HSD_GObj* gobj) {}
void ftDe_SpecialAirNSpitItem_IASA(HSD_GObj* gobj) {}

void ftDe_SpecialNSpit_Phys(HSD_GObj* gobj)
{
    ft_80084F3C(gobj);
}

void ftDe_SpecialNSpitItem_Phys(HSD_GObj* gobj)
{
    ft_80084F3C(gobj);
}

void ftDe_SpecialAirNSpit_Phys(HSD_GObj* gobj)
{
    ft_80084EEC(gobj);
}

void ftDe_SpecialAirNSpitItem_Phys(HSD_GObj* gobj)
{
    ft_80084EEC(gobj);
}

/* SpecialNSpit_PassLedgeCB / SpecialNSpitItem_PassLedgeCB */
static void SpecialNSpit_Fall(HSD_GObj* gobj)
{
    SpecialN_Carry(gobj, ftDe_MS_SpecialAirNSpit, FTDE_MF_CARRY, false, true);
}

static void SpecialNSpitItem_Fall(HSD_GObj* gobj)
{
    SpecialN_Carry(gobj, ftDe_MS_SpecialAirNSpitItem, FTDE_MF_CARRY, false, true);
}

/* SpecialAirNSpit_PassLedgeCB / SpecialAirNSpitItem_PassLedgeCB: landing during a spit. The fighter
 * spit lands into SpecialNSpit (0x161); the item spit names 0x172, its own aerial state, so it stays
 * SpecialAirNSpitItem while grounded (an m-ex slip, kept as found; see NOTES.md). */
static void SpecialAirNSpit_Land(HSD_GObj* gobj)
{
    SpecialN_Carry(gobj, ftDe_MS_SpecialNSpit, FTDE_MF_CARRY, true, true);
}

static void SpecialAirNSpitItem_Land(HSD_GObj* gobj)
{
    SpecialN_Carry(gobj, ftDe_MS_SpecialAirNSpitItem, FTDE_MF_CARRY, true, true);
}

void ftDe_SpecialNSpit_Coll(HSD_GObj* gobj)
{
    ft_8008403C(gobj, SpecialNSpit_Fall);
}

void ftDe_SpecialNSpitItem_Coll(HSD_GObj* gobj)
{
    ft_8008403C(gobj, SpecialNSpitItem_Fall);
}

void ftDe_SpecialAirNSpit_Coll(HSD_GObj* gobj)
{
    ft_80082C74(gobj, SpecialAirNSpit_Land);
}

void ftDe_SpecialAirNSpitItem_Coll(HSD_GObj* gobj)
{
    ft_80082C74(gobj, SpecialAirNSpitItem_Land);
}
