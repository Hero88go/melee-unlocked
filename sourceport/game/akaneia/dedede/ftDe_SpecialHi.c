/* King Dedede (Akaneia), native: Up B, Super Dedede Jump.
 *
 * Start (a facing-left and a facing-right animation), Jump, then Loop: a big leap steered by the
 * stick that slows (rise_decay) and falls. After specialhi_min_frames, holding down drops into Turn,
 * a fast fall that lands with FallSpecial lag or grabs a ledge. Landing from Loop is the
 * crash landing that throws two stars (article 3) left and right. Hitting a ceiling goes to Hit.
 *
 * Source: PlDe.dat ftFunction: SpecialHi, SpecialAirHi, SpecialHi_Enter, SpecialHi_Start_*,
 * SpecialHiJump_*, SpecialHiLoop_*, SpecialHiTurn_*, SpecialHiLanding_*, SpecialHiHit_*,
 * SpecialHi_SpawnStar and their Enter helpers. */
#include "ftDe.h"

#include <string.h>

#include <dolphin/mtx.h>
#include <melee/db/db.h>
#include <melee/ef/efasync.h>
#include <melee/ft/fighter.h>
#include <melee/ft/ft_081B.h>
#include <melee/ft/ft_084E.h>
#include <melee/ft/ft_0892.h>
#include <melee/ft/ftanim.h>
#include <melee/ft/ftcliffcommon.h>
#include <melee/ft/ftcommon.h>
#include <melee/ft/ftparts.h>
#include <melee/ft/inlines.h>
#include <melee/ft/kinds/ftCommon/ftCo_FallSpecial.h>
#include <melee/ft/kinds/ftCommon/ftCo_Landing.h>
#include <melee/it/inlines.h>
#include <melee/it/it_2725.h>
#include <melee/it/item.h>

#define FTDE_HALF_PI 1.5707964f /* 0x3FC90FDB */

/* SpecialHi_Enter (both SpecialHi and SpecialAirHi, anim_start 0) */
void ftDe_SpecialHi_Enter(HSD_GObj* gobj, float anim_start)
{
    Fighter* fp = GET_FIGHTER(gobj);
    ftDe_DatAttrs* da = ftDe_Attrs(fp);

    ftCommon_8007D5D4(fp);
    Fighter_ChangeMotionState(gobj, fp->facing_dir == 1.0f ? ftDe_MS_SpecialHiStartR : ftDe_MS_SpecialHiStartL, 0,
                              anim_start, 1.0f, 0.0f, NULL);
    fp->self_vel.x = 0.0f;
    fp->self_vel.y = da->specialhi_start_vel_y;
}

/* SpecialHiJump_Enter */
static void SpecialHiJump_Enter(HSD_GObj* gobj, float anim_start)
{
    Fighter* fp = GET_FIGHTER(gobj);
    fp->cmd_vars[0] = 0;
    Fighter_ChangeMotionState(gobj, ftDe_MS_SpecialHiJump, 0, anim_start, 1.0f, 0.0f, NULL);
    ftPartSetRotY(fp, 0, FTDE_HALF_PI);
}

/* The stick picks the facing direction; zero keeps it. */
static void SpecialHi_FaceStick(Fighter* fp)
{
    float stick_x = fp->input.lstick[0].x;
    if (stick_x < 0.0f) {
        fp->facing_dir = -1.0f;
    } else if (stick_x > 0.0f) {
        fp->facing_dir = 1.0f;
    }
}

/* SpecialHiLoop_Enter: the leap. */
static void SpecialHiLoop_Enter(HSD_GObj* gobj)
{
    Fighter* fp = GET_FIGHTER(gobj);
    ftDe_DatAttrs* da = ftDe_Attrs(fp);
    ftDe_MotionVars* mv = ftDe_MV(fp);
    float stick_x, range, dir;

    ftCommon_8007D60C(fp);
    SpecialHi_FaceStick(fp);
    Fighter_ChangeMotionState(gobj, ftDe_MS_SpecialHiLoop, 0, 0.0f, 1.0f, 0.0f, NULL);
    ftPartSetRotY(fp, 0, FTDE_HALF_PI);

    stick_x = fp->input.lstick[0].x;
    range = da->specialhi_stick_range;
    if (stick_x > range) {
        dir = 1.0f;
    } else if (stick_x < -range) {
        dir = -1.0f;
    } else {
        dir = stick_x / range;
    }
    fp->self_vel.x = da->specialhi_vel_x * dir;
    fp->self_vel.y = da->specialhi_vel_y;
    mv->specialhi.falling = 0;
    mv->specialhi.frames = 0;
}

/* SpecialHiTurn_Enter: down during the leap, turn over and dive. */
static void SpecialHiTurn_Enter(HSD_GObj* gobj)
{
    Fighter* fp = GET_FIGHTER(gobj);
    ftCommon_8007D5D4(fp);
    Fighter_ChangeMotionState(gobj, fp->facing_dir == 1.0f ? ftDe_MS_SpecialHiTurnR : ftDe_MS_SpecialHiTurnL, 0,
                              0.0f, 1.0f, 0.0f, NULL);
    ftCommon_8007E2FC(gobj);
    fp->cmd_vars[0] = 0;
}

/* SpecialHiHit_Enter: bonked a ceiling. */
static void SpecialHiHit_Enter(HSD_GObj* gobj)
{
    Fighter* fp = GET_FIGHTER(gobj);
    Vec3 offset;

    fp->throw_flags = 0;
    offset.x = 0.0f;
    offset.y = fp->coll_data.ecb.top.y; /* fp+798 */
    offset.z = 0.0f;
    efAsync_Spawn(gobj, &fp->x60C, 2, 0x49E, fp->parts[0].joint, &offset);
    fp->facing_dir = 1.0f;
    fp->cmd_vars[0] = 0;
    Fighter_ChangeMotionState(gobj, ftDe_MS_SpecialHiHit, 0, 0.0f, 1.0f, 0.0f, NULL);
    /* m-ex calls this through a one-argument pointer; the float it takes is unused. */
    ft_80082D40(gobj, 0.0f);
    fp->self_vel.z = 0.0f;
    fp->self_vel.y = 0.0f;
    fp->self_vel.x = 0.0f;
}

/* SpecialHi_SpawnStar: one landing star, moving along `facing_dir`. */
static Item_GObj* SpecialHi_SpawnStar(HSD_GObj* gobj, Vec3* pos, ItemKind kind, float facing_dir)
{
    SpawnItem spawn;
    Item_GObj* item_gobj;

    memset(&spawn, 0, sizeof(spawn));
    spawn.x0_parent_gobj = gobj;
    spawn.x4_parent_gobj2 = gobj;
    spawn.kind = kind;
    spawn.pos = *pos;
    spawn.prev_pos = *pos;
    spawn.vel.x = spawn.vel.y = spawn.vel.z = 0.0f;
    spawn.facing_dir = facing_dir;
    spawn.x3C_damage = 0;
    spawn.x40 = 0;
    spawn.x44_flag.b0 = true;

    item_gobj = Item_80268B18(&spawn);
    if (item_gobj != NULL) {
        Item* ip = GET_ITEM(item_gobj);
        itDe_HiStarAttrs* attrs = DP(ip->xC4_article_data->x4_specialAttributes);
        Item_80268E5C(item_gobj, 0, 2);
        it_80275158(item_gobj, attrs->lifetime);
        ip->x40_vel.x = attrs->speed_x * facing_dir;
        ip->x40_vel.y = attrs->speed_y;
        db_80225DD8(item_gobj, gobj);
    }
    return item_gobj;
}

/* SpecialHiLanding_Enter: the crash landing and its two stars. */
static void SpecialHiLanding_Enter(HSD_GObj* gobj)
{
    Fighter* fp = GET_FIGHTER(gobj);
    ftDe_DatAttrs* da = ftDe_Attrs(fp);
    Vec3 offset, pos;

    ftCommon_8007D7FC(fp);
    SpecialHi_FaceStick(fp);
    Fighter_ChangeMotionState(gobj, fp->facing_dir == 1.0f ? ftDe_MS_SpecialHiLandingR : ftDe_MS_SpecialHiLandingL,
                              0, 0.0f, 1.0f, 0.0f, NULL);
    ftPartSetRotY(fp, 0, FTDE_HALF_PI);
    ftCommon_8007E2FC(gobj);

    offset = BE_VEC3(da->specialhi_star_offset);
    PSVECAdd(&fp->cur_pos, &offset, &pos);
    SpecialHi_SpawnStar(gobj, &pos, mu_ak_article_kind(gobj, ftDe_Article_HiStar), 1.0f);
    pos.x = fp->cur_pos.x - offset.x;
    SpecialHi_SpawnStar(gobj, &pos, mu_ak_article_kind(gobj, ftDe_Article_HiStar), -1.0f);
}

/* ---------------------------------------------------------------- Start */

void ftDe_SpecialHiStart_Anim(HSD_GObj* gobj)
{
    if (!ftAnim_IsFramesRemaining(gobj)) {
        SpecialHiJump_Enter(gobj, 0.0f);
    }
}

void ftDe_SpecialHiStart_IASA(HSD_GObj* gobj) {}

void ftDe_SpecialHiStart_Phys(HSD_GObj* gobj)
{
    Fighter* fp = GET_FIGHTER(gobj);
    ftDe_DatAttrs* da = ftDe_Attrs(fp);
    ftCommon_CalcSelfAccel_DriftSimple_NoFriction(fp, da->specialhi_start_drift[0], da->specialhi_start_drift[1],
                                                  da->specialhi_start_drift[2]);
}

void ftDe_SpecialHiStart_Coll(HSD_GObj* gobj)
{
    ft_80081D0C(gobj);
}

/* ---------------------------------------------------------------- Jump */

void ftDe_SpecialHiJump_Anim(HSD_GObj* gobj)
{
    Fighter* fp = GET_FIGHTER(gobj);
    if (fp->cmd_vars[0] == 1) {
        SpecialHiLoop_Enter(gobj);
    }
}

void ftDe_SpecialHiJump_IASA(HSD_GObj* gobj) {}

void ftDe_SpecialHiJump_Phys(HSD_GObj* gobj)
{
    ftDe_SpecialHiStart_Phys(gobj);
}

void ftDe_SpecialHiJump_Coll(HSD_GObj* gobj)
{
    ft_80081D0C(gobj);
}

/* ---------------------------------------------------------------- Loop */

void ftDe_SpecialHiLoop_Anim(HSD_GObj* gobj)
{
    ftDe_MV(GET_FIGHTER(gobj))->specialhi.frames++;
}

void ftDe_SpecialHiLoop_IASA(HSD_GObj* gobj)
{
    Fighter* fp = GET_FIGHTER(gobj);
    ftDe_DatAttrs* da = ftDe_Attrs(fp);
    if (ftDe_MV(fp)->specialhi.frames > da->specialhi_min_frames &&
        fp->input.lstick[0].y < -p_ftCommonData->x88)
    {
        SpecialHiTurn_Enter(gobj);
    }
}

/* Rising: vertical speed decays until it drops under specialhi_rise_min, then ordinary falling. */
void ftDe_SpecialHiLoop_Phys(HSD_GObj* gobj)
{
    Fighter* fp = GET_FIGHTER(gobj);
    ftDe_DatAttrs* da = ftDe_Attrs(fp);
    ftDe_MotionVars* mv = ftDe_MV(fp);

    if (mv->specialhi.falling == 0) {
        fp->self_vel.y *= da->specialhi_rise_decay;
        if (fp->self_vel.y < da->specialhi_rise_min) {
            mv->specialhi.falling = 1;
        }
    } else if (mv->specialhi.falling == 1) {
        ftCommon_Fall(fp, da->specialhi_fall_gravity, da->specialhi_fall_terminal);
    }
}

void ftDe_SpecialHiLoop_Coll(HSD_GObj* gobj)
{
    Fighter* fp = GET_FIGHTER(gobj);
    if (ft_80081D0C(gobj)) {
        SpecialHiLanding_Enter(gobj);
    } else if (fp->coll_data.env_flags & 0x4000) {
        SpecialHiHit_Enter(gobj);
    }
}

/* ---------------------------------------------------------------- Turn */

void ftDe_SpecialHiTurn_Anim(HSD_GObj* gobj)
{
    Fighter* fp = GET_FIGHTER(gobj);
    ftDe_DatAttrs* da = ftDe_Attrs(fp);
    if (!ftAnim_IsFramesRemaining(gobj)) {
        ftCo_80096900(gobj, 1, 0, false, da->specialhi_fall_mobility, da->specialhi_landing_lag);
    }
}

void ftDe_SpecialHiTurn_IASA(HSD_GObj* gobj) {}

void ftDe_SpecialHiTurn_Phys(HSD_GObj* gobj)
{
    Fighter* fp = GET_FIGHTER(gobj);
    ftDe_DatAttrs* da = ftDe_Attrs(fp);
    ftCommon_Fall(fp, da->specialhi_turn_gravity, da->specialhi_turn_terminal);
    ftCommon_CalcSelfAccel_DriftSimple(fp, 0.0f, da->specialhi_turn_drift_accel, da->specialhi_turn_drift_max);
}

/* Once the script sets cmd_vars[0] the dive may land (with special landing lag) or catch a
 * ledge; before that it only collides. */
void ftDe_SpecialHiTurn_Coll(HSD_GObj* gobj)
{
    Fighter* fp = GET_FIGHTER(gobj);
    ftDe_DatAttrs* da = ftDe_Attrs(fp);
    if (fp->cmd_vars[0] == 1) {
        if (ft_CheckGroundAndLedge(gobj, 0)) {
            ftCo_LandingFallSpecial_Enter(gobj, false, da->specialhi_landing_lag);
        } else {
            ftCliffCommon_80081298(gobj);
        }
    } else {
        ft_80081D0C(gobj);
    }
}

/* ---------------------------------------------------------------- Landing */

void ftDe_SpecialHiLanding_Anim(HSD_GObj* gobj)
{
    if (!ftAnim_IsFramesRemaining(gobj)) {
        ft_8008A2BC(gobj);
    }
}

void ftDe_SpecialHiLanding_IASA(HSD_GObj* gobj) {}

void ftDe_SpecialHiLanding_Phys(HSD_GObj* gobj)
{
    ft_80084F3C(gobj);
}

void ftDe_SpecialHiLanding_Coll(HSD_GObj* gobj)
{
    ft_80084104(gobj);
}

/* ---------------------------------------------------------------- Hit */

void ftDe_SpecialHiHit_Anim(HSD_GObj* gobj)
{
    Fighter* fp = GET_FIGHTER(gobj);
    ftDe_DatAttrs* da = ftDe_Attrs(fp);
    if (!ftAnim_IsFramesRemaining(gobj)) {
        ftCo_80096900(gobj, 1, 0, false, da->specialhi_fall_mobility, da->specialhi_landing_lag);
    }
}

void ftDe_SpecialHiHit_IASA(HSD_GObj* gobj) {}

/* Frozen against the ceiling until the script sets cmd_vars[0]. */
void ftDe_SpecialHiHit_Phys(HSD_GObj* gobj)
{
    Fighter* fp = GET_FIGHTER(gobj);
    if (fp->cmd_vars[0] != 0) {
        ft_80084DB0(gobj);
    }
}

/* Uses the fighter's own StopCeil collision (through its common state table, as m-ex does). */
void ftDe_SpecialHiHit_Coll(HSD_GObj* gobj)
{
    Fighter* fp = GET_FIGHTER(gobj);
    fp->x1C_actionStateList[FTDE_CO_STOPCEIL].coll_cb(gobj);
}
