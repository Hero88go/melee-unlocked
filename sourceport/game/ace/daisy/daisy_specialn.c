/* ACE's Daisy: the neutral special (states 365 to 368; m-ex "ftFunction" of PlDa.dat).
 *
 * Peach's Toad counter (ft/kinds/ftPeach/ftpeachspecialn.c) with these differences:
 *  - the Toad is her own article 3 and the spores are her own article 4 (item kinds from MxDt
 *    by index, m-ex MEX_GetFtItemID at 0x803D7088), where Peach passes her two retail kinds;
 *  - the Toad is attached to the bone the part table gives for part 49, where Peach names
 *    part 109 directly;
 *  - the counter bubble is a constant of the code (bone 3, offset 0, 1, 3.5, radius 6) where
 *    Peach reads it from her attributes (+AC), and on the ground the two shield numbers are
 *    the constant 5.0 where Peach reads +A8 (in the air hers reads +A8 too);
 *  - the death and damage callback is her own (daisy.c).
 * The four IASA and four physics callbacks are Peach's functions (same bodies in the file). */
#include "daisy.h"

#include <dolphin/os.h>

#include <melee/ft/fighter.h>
#include <melee/ft/ft_081B.h>
#include <melee/ft/ft_0892.h>
#include <melee/ft/ftanim.h>
#include <melee/ft/ftcoll.h>
#include <melee/ft/ftcommon.h>
#include <melee/ft/ftparts.h>
#include <melee/ft/inlines.h>
#include <melee/ft/kinds/ftCommon/ftCo_Fall.h>
#include <melee/ft/kinds/ftCommon/inlines.h>
#include <melee/ft/types.h>
#include <melee/it/kinds/itpeachtoad.h>
#include <melee/it/kinds/itpeachtoadspore.h>
#include <melee/lb/lb_00B0.h>
#include <melee/lb/types.h>
#include <sysdolphin/baselib/gobj.h>

/* The script variables, as in ftpeachspecialn.c: 0 physics step, 1 bubble step, 2 Toad made,
 * 3 "scatter the spores" (set by the hit animation's script). */
enum {
    ftDa_SpecialN_CmdPhys = 0,
    ftDa_SpecialN_CmdAnim = 1,
    ftDa_SpecialN_CmdToad = 2,
    ftDa_SpecialN_CmdHit = 3,
};

/* The part whose bone carries the Toad (code+0x1290: li r4, 0x31). */
#define FTDA_TOAD_PART ((Fighter_Part) 49)

static void ftDa_SpecialN_OnShieldHit(HSD_GObj* gobj);

/* The counter bubble (code+0xC8C to +0xCB8, built on the stack each time: the words 3, 0.0,
 * 1.0, 3.5 and 6.0). ftColl_8007B1B8 reads the bone, the offset and the radius only. */
static void ftDa_SpecialN_SetBubble(HSD_GObj* gobj)
{
    ShieldDesc bubble = { 0 };

    bubble.bone = 3;
    bubble.pos.x = 0.0f;
    bubble.pos.y = 1.0f;
    bubble.pos.z = 3.5f;
    bubble.radius = 6.0f;
    ftColl_8007B1B8(gobj, &bubble, ftDa_SpecialN_OnShieldHit);
}

/* code+0x1C50, the hitlag callback: the Toad freezes with her (Peach's static onEnterHitlag). */
static void ftDa_SpecialN_OnEnterHitlag(HSD_GObj* gobj)
{
    Fighter* fp = GET_FIGHTER(gobj);

    if (fp->u.pe.toad_gobj != NULL) {
        it_802BDFA0(fp->u.pe.toad_gobj);
    }
}

/* code+0x1C70, the end of hitlag (Peach's static onExitHitlag). */
static void ftDa_SpecialN_OnExitHitlag(HSD_GObj* gobj)
{
    Fighter* fp = GET_FIGHTER(gobj);

    if (fp->u.pe.toad_gobj != NULL) {
        it_802BDFC0(fp->u.pe.toad_gobj);
    }
}

/* The owner's side of the Toad going away: the tail of code+0x1BA4, and inlined in the
 * article's code (article 3 +0x7C, +0x1C0) and in code+0x2014. Peach's ftPe_SpecialN_DoDeath2. */
void ftDa_SpecialN_ToadGone(HSD_GObj* gobj)
{
    Fighter* fp = GET_FIGHTER(gobj);

    if (fp->u.pe.toad_gobj != NULL) {
        it_802BDFC0(fp->u.pe.toad_gobj);
    }
    fp->u.pe.toad_gobj = NULL;
    fp->death2_cb = NULL;
    fp->take_dmg_cb = NULL;
}

/* code+0x1260, the accessory callback of states 365 and 367: make the Toad, once. The spawn
 * (code+0x1AD4) builds the same request as retail it_802BDE18 and makes the same calls
 * (Item_80268B18, Item_8026AB54, db_80225DD8), so that function is called with her kind. */
static void ftDa_SpecialN_OnAccessory4(HSD_GObj* gobj)
{
    Fighter* fp = GET_FIGHTER(gobj);
    Fighter_Part bone;
    Vec3 pos;

    if (fp->cmd_vars[ftDa_SpecialN_CmdToad] != 0) {
        return;
    }
    fp->cmd_vars[ftDa_SpecialN_CmdToad] = 1;

    bone = ftParts_GetBoneIndex(fp, FTDA_TOAD_PART);
    lb_8000B1CC(fp->parts[bone].joint, NULL, &pos);
    fp->u.pe.toad_gobj = it_802BDE18(gobj, &pos, bone,
                                     mu_ak_article_kind(gobj, ftDa_Article_Toad),
                                     fp->facing_dir);
    fp->x1984_heldItemSpec = fp->u.pe.toad_gobj;
    if (fp->u.pe.toad_gobj != NULL) {
        fp->death2_cb = ftDa_Init_OnDeath2;
        fp->take_dmg_cb = ftDa_Init_OnDeath2;
    }
    fp->accessory4_cb = NULL;
    fp->pre_hitlag_cb = ftDa_SpecialN_OnEnterHitlag;
    fp->post_hitlag_cb = ftDa_SpecialN_OnExitHitlag;

    {
        /* Bring-up evidence (the first ones only). */
        static int logged;
        if (logged < 4) {
            logged++;
            OSReport("[ak] Daisy toad: item kind %d on bone %d, %s\n",
                     (int) mu_ak_article_kind(gobj, ftDa_Article_Toad), (int) bone,
                     fp->u.pe.toad_gobj != NULL ? "created" : "not created");
        }
    }
}

/* code+0x17D0, the accessory callback of the two hit states: scatter one spore from above the
 * hand (part 109, 2.5 up, on the plane). The spawn (code+0x1E3C) is retail it_802BE214 call
 * for call (Item_80268B18, it_802BE2E8, db_80225DD8). */
static void ftDa_SpecialNHit_OnAccessory4(HSD_GObj* gobj)
{
    Fighter* fp = GET_FIGHTER(gobj);
    Item_GObj* spore_gobj;
    Vec3 pos;

    lb_8000B1CC(fp->parts[FtPart_109].joint, NULL, &pos);
    pos.y += 2.5f;
    pos.z = 0.0f;
    spore_gobj = it_802BE214(gobj, &pos, mu_ak_article_kind(gobj, ftDa_Article_Spore),
                             fp->facing_dir);
    fp->accessory4_cb = NULL;

    {
        /* Bring-up evidence (the first ones only). */
        static int logged;
        if (logged < 4) {
            logged++;
            OSReport("[ak] Daisy spore: item kind %d, %s\n",
                     (int) mu_ak_article_kind(gobj, ftDa_Article_Spore),
                     spore_gobj != NULL ? "created" : "not created");
        }
    }
}

/* The script variables and the facing kept for the hit state, written by both entries
 * before the state change (code+0x5DC to +0x628, +0x6B0 to +0x6CC). */
static void ftDa_SpecialN_Reset(Fighter* fp)
{
    fp->cmd_vars[ftDa_SpecialN_CmdHit] = 0;
    fp->cmd_vars[ftDa_SpecialN_CmdToad] = 0;
    fp->cmd_vars[ftDa_SpecialN_CmdAnim] = 0;
    fp->cmd_vars[ftDa_SpecialN_CmdPhys] = 0;
    fp->mv.pe.specialn.facing_dir = fp->facing_dir;
}

/* code+0x5C8, [specialn]. Peach's ftPe_SpecialN_Enter with the order of the file: the
 * variables before the state change, the accessory callback after it. */
void ftDa_SpecialN_Enter(HSD_GObj* gobj)
{
    Fighter* fp = GET_FIGHTER(gobj);

    ftDa_SpecialN_Reset(fp);
    fp->self_vel.y = 0.0f;
    Fighter_ChangeMotionState(gobj, ftPe_MS_SpecialN, Ft_MF_None, 0.0f, 1.0f, 0.0f, NULL);
    ftAnim_8006EBA4(gobj);
    fp->accessory4_cb = ftDa_SpecialN_OnAccessory4;
}

/* code+0x65C, [specialairn]. */
void ftDa_SpecialAirN_Enter(HSD_GObj* gobj)
{
    Fighter* fp = GET_FIGHTER(gobj);
    ftPe_DatAttrs* da = fp->dat_attrs;

    ftDa_SpecialN_Reset(fp);
    fp->self_vel.x /= da->specialairn_vel_x_div;
    Fighter_ChangeMotionState(gobj, ftPe_MS_SpecialAirN, Ft_MF_None, 0.0f, 1.0f, 0.0f, NULL);
    ftAnim_8006EBA4(gobj);
    fp->accessory4_cb = ftDa_SpecialN_OnAccessory4;
}

/* code+0xC60, the animation callback of 365: the bubble comes up when the script asks, and
 * the two shield numbers are 5.0. */
void ftDa_SpecialN_Anim(HSD_GObj* gobj)
{
    Fighter* fp = GET_FIGHTER(gobj);

    if (fp->cmd_vars[ftDa_SpecialN_CmdAnim] == 1) {
        fp->cmd_vars[ftDa_SpecialN_CmdAnim] = 2;
        ftDa_SpecialN_SetBubble(gobj);
        fp->x221B_b3 = true;
        fp->shield_unk0 = 5.0f;
        fp->shield_unk1 = 5.0f;
    } else if (fp->cmd_vars[ftDa_SpecialN_CmdAnim] == 0) {
        fp->x221B_b0 = false;
    }
    if (!ftAnim_IsFramesRemaining(gobj)) {
        ft_8008A2BC(gobj);
    }
}

/* code+0xEC4, the animation callback of 367: as on the ground, with the two shield numbers
 * from her attributes (+A8), as Peach's. */
void ftDa_SpecialAirN_Anim(HSD_GObj* gobj)
{
    Fighter* fp = GET_FIGHTER(gobj);
    ftPe_DatAttrs* da = fp->dat_attrs;

    if (fp->cmd_vars[ftDa_SpecialN_CmdAnim] == 1) {
        fp->cmd_vars[ftDa_SpecialN_CmdAnim] = 2;
        ftDa_SpecialN_SetBubble(gobj);
        fp->x221B_b3 = true;
        fp->shield_unk0 = da->xA8;
        fp->shield_unk1 = da->xA8;
    } else if (fp->cmd_vars[ftDa_SpecialN_CmdAnim] == 0) {
        fp->x221B_b0 = false;
    }
    if (!ftAnim_IsFramesRemaining(gobj)) {
        ftCo_Fall_Enter(gobj);
    }
}

/* What both ground and air transitions of 365 and 367 put back (inlined at code+0x1704 and
 * +0x1960; Peach's static setupColl). */
static void ftDa_SpecialN_SetupColl(HSD_GObj* gobj)
{
    Fighter* fp = GET_FIGHTER(gobj);

    if (fp->u.pe.toad_gobj != NULL) {
        fp->death2_cb = ftDa_Init_OnDeath2;
        fp->take_dmg_cb = ftDa_Init_OnDeath2;
    }
    fp->accessory4_cb = ftDa_SpecialN_OnAccessory4;
    fp->pre_hitlag_cb = ftDa_SpecialN_OnEnterHitlag;
    fp->post_hitlag_cb = ftDa_SpecialN_OnExitHitlag;
    if (fp->cmd_vars[ftDa_SpecialN_CmdAnim] == 2) {
        ftDa_SpecialN_SetBubble(gobj);
        fp->x221B_b3 = true;
    }
}

/* What the transitions of the two hit states put back (inlined at code+0x18B0 and +0x1A84;
 * Peach's static setupHitColl). */
static void ftDa_SpecialN_SetupHitColl(HSD_GObj* gobj)
{
    Fighter* fp = GET_FIGHTER(gobj);

    if (fp->u.pe.toad_gobj != NULL) {
        fp->death2_cb = ftDa_Init_OnDeath2;
        fp->take_dmg_cb = ftDa_Init_OnDeath2;
    }
    fp->pre_hitlag_cb = ftDa_SpecialN_OnEnterHitlag;
    fp->post_hitlag_cb = ftDa_SpecialN_OnExitHitlag;
}

/* code+0x1698: off the ground during 365. */
static void ftDa_SpecialN_GroundToAir(HSD_GObj* gobj)
{
    Fighter* fp = GET_FIGHTER(gobj);

    ftCommon_GroundToAirStateChange(gobj, fp, ftPe_MS_SpecialAirN, FTDA_SPECIALN_COLL_MF);
    if (fp->cmd_vars[ftDa_SpecialN_CmdPhys] == 1) {
        fp->cmd_vars[ftDa_SpecialN_CmdPhys] = 2;
    }
    ftDa_SpecialN_SetupColl(gobj);
}

/* code+0x1900: landed during 367. */
static void ftDa_SpecialN_AirToGround(HSD_GObj* gobj)
{
    Fighter* fp = GET_FIGHTER(gobj);

    fp->u.pe.specialairn_used = false;
    ftCommon_AirToGroundStateChange(gobj, fp, ftPe_MS_SpecialN, FTDA_SPECIALN_COLL_MF);
    ftDa_SpecialN_SetupColl(gobj);
}

/* code+0x1860: off the ground during 366. */
static void ftDa_SpecialNHit_GroundToAir(HSD_GObj* gobj)
{
    Fighter* fp = GET_FIGHTER(gobj);

    ftCommon_GroundToAirStateChange(gobj, fp, ftPe_MS_SpecialAirNHit, FTDA_SPECIALN_COLL_MF);
    ftDa_SpecialN_SetupHitColl(gobj);
}

/* code+0x1A2C: landed during 368. */
static void ftDa_SpecialNHit_AirToGround(HSD_GObj* gobj)
{
    Fighter* fp = GET_FIGHTER(gobj);

    fp->u.pe.specialairn_used = false;
    ftCommon_AirToGroundStateChange(gobj, fp, ftPe_MS_SpecialNHit, FTDA_SPECIALN_COLL_MF);
    ftDa_SpecialN_SetupHitColl(gobj);
}

/* code+0xDB0 */
void ftDa_SpecialN_Coll(HSD_GObj* gobj)
{
    if (!ft_800827A0(gobj)) {
        ftDa_SpecialN_GroundToAir(gobj);
    }
}

/* code+0x10D8 */
void ftDa_SpecialAirN_Coll(HSD_GObj* gobj)
{
    if (ft_80081D0C(gobj)) {
        ftDa_SpecialN_AirToGround(gobj);
    }
}

/* code+0xDFC, the animation callback of 366: when the script asks, the next accessory step
 * scatters a spore. */
void ftDa_SpecialNHit_Anim(HSD_GObj* gobj)
{
    Fighter* fp = GET_FIGHTER(gobj);

    if (fp->cmd_vars[ftDa_SpecialN_CmdHit] != 0) {
        fp->cmd_vars[ftDa_SpecialN_CmdHit] = 0;
        fp->accessory4_cb = ftDa_SpecialNHit_OnAccessory4;
    }
    if (!ftAnim_IsFramesRemaining(gobj)) {
        ft_8008A2BC(gobj);
    }
}

/* code+0x1124, the animation callback of 368. */
void ftDa_SpecialAirNHit_Anim(HSD_GObj* gobj)
{
    Fighter* fp = GET_FIGHTER(gobj);

    if (fp->cmd_vars[ftDa_SpecialN_CmdHit] != 0) {
        fp->cmd_vars[ftDa_SpecialN_CmdHit] = 0;
        fp->accessory4_cb = ftDa_SpecialNHit_OnAccessory4;
    }
    if (!ftAnim_IsFramesRemaining(gobj)) {
        ftCo_Fall_Enter(gobj);
    }
}

/* code+0xE78 */
void ftDa_SpecialNHit_Coll(HSD_GObj* gobj)
{
    if (!ft_800827A0(gobj)) {
        ftDa_SpecialNHit_GroundToAir(gobj);
    }
}

/* code+0x11E8 */
void ftDa_SpecialAirNHit_Coll(HSD_GObj* gobj)
{
    if (ft_80081D0C(gobj)) {
        ftDa_SpecialNHit_AirToGround(gobj);
    }
}

/* code+0x15A0, the bubble was hit: the hit state of the ground or the air from frame 9, and
 * the Toad goes to its second state. That step (code+0x1DA8) is retail it_802BE100 call for
 * call (it ends in it_80274594, which is all retail it_80274574 does). */
static void ftDa_SpecialN_OnShieldHit(HSD_GObj* gobj)
{
    Fighter* fp = GET_FIGHTER(gobj);
    FtMotionId msid;

    fp->mv.pe.specialn.facing_dir = fp->specialn_facing_dir;
    if (fp->ground_or_air == GA_Ground) {
        msid = ftPe_MS_SpecialNHit;
    } else {
        msid = ftPe_MS_SpecialAirNHit;
    }
    Fighter_ChangeMotionState(gobj, msid, Ft_MF_None, 9.0f, 1.0f, 0.0f, NULL);
    ftAnim_8006EBA4(gobj);
    if (fp->u.pe.toad_gobj != NULL) {
        it_802BE100(fp->u.pe.toad_gobj);
    }
    ftDa_SpecialN_SetupHitColl(gobj);
}
