/* PlFy.dat ftFunction +0A20/+0A70, +21E8..+2E64, +3F8C..+4244 and +470C.
 * Literal native reflector callbacks. Retail callbacks in the state table intentionally keep
 * their retail state numbers; these callbacks use the numbers encoded in PlFy.dat. */
#include "fay.h"

#include <melee/ef/eflib.h>
#include <melee/ef/efsync.h>
#include <melee/ft/fighter.h>
#include <melee/ft/ft_081B.h>
#include <melee/ft/ft_084E.h>
#include <melee/ft/ft_0892.h>
#include <melee/ft/ftanim.h>
#include <melee/ft/ftcoll.h>
#include <melee/ft/ftcommon.h>
#include <melee/ft/inlines.h>
#include <melee/ft/kinds/ftCommon/ftCo_Fall.h>
#include <melee/ft/kinds/ftCommon/ftCo_Jump.h>
#include <melee/ft/kinds/ftCommon/ftCo_JumpAerial.h>
#include <melee/ft/kinds/ftCommon/ftCo_Pass.h>
#include <melee/lb/types.h>
#include <sysdolphin/baselib/jobj.h>
#include <sysdolphin/baselib/controller.h>

static void ftFy_SpecialLw_OnReflect(HSD_GObj* gobj);

static void ftFy_SpecialLw_Change(HSD_GObj* gobj, int state)
{
    Fighter_ChangeMotionState(gobj, state, 0, 0.0f, 1.0f, 0.0f, NULL);
}

/* +3F8C and +470C differ only in the requested effect. */
static void ftFy_SpecialLw_Effect(HSD_GObj* gobj, int effect)
{
    Fighter* fp = GET_FIGHTER(gobj);
    if (!fp->x2219_b0) {
        efSync_Spawn(effect, gobj, fp->parts[3].joint);
        fp->x2219_b0 = true;
    }
    fp->pre_hitlag_cb = efLib_PauseAll;
    fp->post_hitlag_cb = efLib_ResumeAll;
    fp->accessory4_cb = NULL;
}

static void ftFy_SpecialLw_GFX(HSD_GObj* gobj)
{
    ftFy_SpecialLw_Effect(gobj, FTFY_EFFECT_REFLECT);
}

static void ftFy_SpecialLw_HitGFX(HSD_GObj* gobj)
{
    ftFy_SpecialLw_Effect(gobj, FTFY_EFFECT_REFLECT_HIT);
}

/* The 0x24-byte description at +4028, converted to host order. */
static void ftFy_SpecialLw_Bubble(HSD_GObj* gobj)
{
    Fighter* fp = GET_FIGHTER(gobj);
    if (fp->cmd_vars[3] != 1) {
        ReflectDesc desc = { 1, 50, { 0.0f, 8.0f, 0.0f }, 8.5f, 1.5f, 1.0f, 0 };
        fp->cmd_vars[3] = 1;
        ftColl_CreateReflectHit(gobj, &desc, ftFy_SpecialLw_OnReflect);
    }
    ftColl_8007AEF8(gobj);
}

void ftFy_SpecialLw_Enter(HSD_GObj* gobj)
{
    ftFy_SpecialLw_Change(gobj, ftFy_MS_SpecialLwStart);
    ftAnim_8006EBA4(gobj);
}

void ftFy_SpecialAirLw_Enter(HSD_GObj* gobj)
{
    Fighter* fp = GET_FIGHTER(gobj);
    fp->self_vel.y = 0.0f;
    fp->self_vel.x *= 0.5f;
    ftFy_SpecialLw_Change(gobj, ftFy_MS_SpecialAirLwStart);
    ftAnim_8006EBA4(gobj);
}

/* +21E8 and +284C. Start does not create the bubble until its animation finishes. */
static void ftFy_SpecialLw_StartAnim(HSD_GObj* gobj, int loop)
{
    Fighter* fp = GET_FIGHTER(gobj);
    if (!ftAnim_IsFramesRemaining(gobj)) {
        ftFy_SpecialLw_Change(gobj, loop);
        ftAnim_8006EBA4(gobj);
        fp->accessory4_cb = ftFy_SpecialLw_GFX;
        fp->cmd_vars[2] = 1;
        fp->take_dmg_cb = efLib_DestroyAll;
        fp->death2_cb = efLib_DestroyAll;
    }
}

void ftFy_SpecialLwStart_Anim(HSD_GObj* gobj)
{
    ftFy_SpecialLw_StartAnim(gobj, ftFy_MS_SpecialLwLoop);
}

void ftFy_SpecialAirLwStart_Anim(HSD_GObj* gobj)
{
    ftFy_SpecialLw_StartAnim(gobj, ftFy_MS_SpecialAirLwLoop);
}

void ftFy_SpecialLwStart_IASA(HSD_GObj* gobj)
{
    if (ftCo_80099F1C(gobj)) {
        ftCo_8009A184(gobj, ftFy_MS_SpecialAirLwStart, 0,
                      GET_FIGHTER(gobj)->cur_anim_frame);
    }
}

/* +22F0/+28F8. The ground turn writes 45 degrees every frame; the air turn adds it. */
static void ftFy_SpecialLw_TurnAnim(HSD_GObj* gobj, bool air)
{
    Fighter* fp = GET_FIGHTER(gobj);
    ftFy_FighterVars* fv = ftFy_FV(fp);
    bool release;
    ftFy_SpecialLw_Bubble(gobj);
    release = !(fp->input.held_buttons[0] & HSD_PAD_B);
    if (release) {
        fp->cmd_vars[1] = 1;
    }
    if (fv->lw_turn != 0) {
        HSD_JObj* joint = fp->parts[3].joint;
        fv->lw_turn--;
        if (air) {
            joint->rotate.y = (float) ((double) joint->rotate.y + 0.7853981633974483);
        } else {
            joint->rotate.y = 0.7853981852531433f;
        }
        /* The console writes the rotation directly, without setting matrix dirty. */
        return;
    }
    if (release || fp->cmd_vars[1] == 1) {
        ftFy_SpecialLw_Change(gobj, air ? ftFy_MS_SpecialAirLwEnd : ftFy_MS_SpecialLwEnd);
        ftCommon_8007DB24(gobj);
    } else {
        ftFy_SpecialLw_Change(gobj, air ? ftFy_MS_SpecialAirLwLoop : ftFy_MS_SpecialLwLoop);
    }
    ftAnim_8006EBA4(gobj);
    fp->take_dmg_cb = efLib_DestroyAll;
    fp->death2_cb = efLib_DestroyAll;
}

void ftFy_SpecialLwTurn_Anim(HSD_GObj* gobj) { ftFy_SpecialLw_TurnAnim(gobj, false); }
void ftFy_SpecialAirLwTurn_Anim(HSD_GObj* gobj) { ftFy_SpecialLw_TurnAnim(gobj, true); }

void ftFy_SpecialLw_JumpIASA(HSD_GObj* gobj)
{
    if (ftCo_Jump_CheckInput(gobj) == 1) {
        ftCommon_8007DB24(gobj);
    }
}

void ftFy_SpecialAirLw_JumpIASA(HSD_GObj* gobj)
{
    if (ftCo_800CB870(gobj) == 1) {
        ftCommon_8007DB24(gobj);
    }
}

/* +2490/+2AF4: after a reflection the release latch selects end or loop. */
static void ftFy_SpecialLw_HitAnim(HSD_GObj* gobj, bool air)
{
    Fighter* fp = GET_FIGHTER(gobj);
    ftFy_SpecialLw_Bubble(gobj);
    if (!(fp->input.held_buttons[0] & HSD_PAD_B)) {
        fp->cmd_vars[1] = 1;
    }
    if (!ftAnim_IsFramesRemaining(gobj)) {
        if (fp->cmd_vars[1] == 1) {
            ftFy_SpecialLw_Change(gobj, air ? ftFy_MS_SpecialAirLwEnd : ftFy_MS_SpecialLwEnd);
        } else {
            ftFy_SpecialLw_Change(gobj, air ? ftFy_MS_SpecialAirLwLoop : ftFy_MS_SpecialLwLoop);
            fp->accessory4_cb = ftFy_SpecialLw_GFX;
        }
        ftAnim_8006EBA4(gobj);
    }
}

void ftFy_SpecialLwHit_Anim(HSD_GObj* gobj) { ftFy_SpecialLw_HitAnim(gobj, false); }
void ftFy_SpecialAirLwHit_Anim(HSD_GObj* gobj) { ftFy_SpecialLw_HitAnim(gobj, true); }

void ftFy_SpecialLwEnd_Anim(HSD_GObj* gobj)
{
    if (!ftAnim_IsFramesRemaining(gobj)) {
        ft_8008A2BC(gobj);
    }
}

void ftFy_SpecialAirLwEnd_Anim(HSD_GObj* gobj)
{
    if (!ftAnim_IsFramesRemaining(gobj)) {
        ftCo_Fall_Enter(gobj);
    }
}

/* +2600/+2C64. On release the first 17 frames are held by cmd_vars[2]. Turning has no
 * animation reset call; this is the order in the file. Unordered comparisons are retained. */
static void ftFy_SpecialLw_LoopAnim(HSD_GObj* gobj, bool air)
{
    Fighter* fp = GET_FIGHTER(gobj);
    bool early = !(fp->cur_anim_frame >= 17.0f);
    ftFy_SpecialLw_Bubble(gobj);
    if (!(fp->input.held_buttons[0] & HSD_PAD_B)) {
        fp->cmd_vars[1] = 1;
        if (!early) {
            fp->cmd_vars[2] = 0;
            goto end;
        }
        if (fp->cmd_vars[2] == 0) {
            goto end;
        }
    } else if (early) {
        if (fp->cmd_vars[1] == 1 && fp->cmd_vars[2] == 0) {
            goto end;
        }
    } else {
        fp->cmd_vars[2] = 0;
        if (fp->cmd_vars[1] == 1) {
            goto end;
        }
    }
    if ((fp->facing_dir > 0.0f && fp->input.lstick[0].x <= -0.1) ||
        (fp->facing_dir < 0.0f && fp->input.lstick[0].x >= 0.1)) {
        ftCommon_UpdateFacing(fp);
        ftFy_FV(fp)->lw_turn = 4;
        ftFy_SpecialLw_Change(gobj, air ? ftFy_MS_SpecialAirLwTurn : ftFy_MS_SpecialLwTurn);
    }
    fp->take_dmg_cb = efLib_DestroyAll;
    fp->death2_cb = efLib_DestroyAll;
    return;
end:
    ftFy_SpecialLw_Change(gobj, air ? ftFy_MS_SpecialAirLwEnd : ftFy_MS_SpecialLwEnd);
    ftAnim_8006EBA4(gobj);
    ftCommon_8007DB24(gobj);
}

void ftFy_SpecialLwLoop_Anim(HSD_GObj* gobj) { ftFy_SpecialLw_LoopAnim(gobj, false); }
void ftFy_SpecialAirLwLoop_Anim(HSD_GObj* gobj) { ftFy_SpecialLw_LoopAnim(gobj, true); }

void ftFy_SpecialAirLw_Phys(HSD_GObj* gobj)
{
    Fighter* fp = GET_FIGHTER(gobj);
    float vel = fp->self_vel.y;
    if (!(vel <= -1.6)) {
        vel = (float) ((double) vel - 0.05);
        fp->self_vel.y = vel;
    }
    if (!(vel >= -1.6)) {
        fp->self_vel.y = -1.600000023841858f;
    }
    ftCommon_CalcSelfAccel_Deaccel(fp, fp->co_attrs.aerial_friction);
}

static void ftFy_SpecialLw_OnReflect(HSD_GObj* gobj)
{
    Fighter* fp = GET_FIGHTER(gobj);
    ftFy_SpecialLw_Change(gobj, fp->ground_or_air == GA_Ground
                                   ? ftFy_MS_SpecialLwHit : ftFy_MS_SpecialAirLwHit);
    ftAnim_8006EBA4(gobj);
    fp->accessory4_cb = ftFy_SpecialLw_HitGFX;
    ftCommon_8007DB24(gobj);
}

/* +4104: the air-to-ground cases deliberately set f3 to 1 (not zero) and preserve frame. */
void ftFy_SpecialAirLw_Coll(HSD_GObj* gobj)
{
    static const int states[] = { ftFy_MS_SpecialLwStart, ftFy_MS_SpecialLwTurn,
                                 ftFy_MS_SpecialLwHit, ftFy_MS_SpecialLwEnd,
                                 ftFy_MS_SpecialLwLoop };
    Fighter* fp = GET_FIGHTER(gobj);
    if (ft_80081D0C(gobj)) {
        unsigned idx;
        ftCommon_8007D7FC(fp);
        idx = (unsigned) (fp->motion_id - ftFy_MS_SpecialAirLwStart);
        if (idx < 5) {
            Fighter_ChangeMotionState(gobj, states[idx], 0, fp->cur_anim_frame,
                                      1.0f, 1.0f, NULL);
        }
        ftAnim_8006EBA4(gobj);
    }
}
