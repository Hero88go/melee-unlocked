/* ACE's Sonic BM: the down special, a spin charge and roll (states 349, 350, 354 to 359, 368),
 * the file's own code.
 *
 * A start (349 ground, 350 air) leads to the charge (354 ground, 356 air) with his afterimage
 * chain (article 1). While the stick is held down, each B press in the script's window
 * (cmd_vars[1]) raises the level kept in fp+22F8 (up to attrs +04, 5) and restarts the charge;
 * letting the stick go starts the roll (358) at (1.9 + 0.3 * level) a frame with 60 frames to
 * go. The ground roll hits for 8 + 2 * level, turns with the stick against him (and then eases
 * back to speed by 0.15 a frame instead of 0.25), stops on a shield button (L, R, Z held), a
 * B press before a turn, or when the 60 frames are out (355, then the common run with his
 * speed), and jumps into the air roll (359). The air roll hits for 6, 8, 9 or 11 at levels 1 to
 * 4 (no hit at 0 or 5), and ends (368) on a shield button or B, or after 60 frames. The level
 * is kept until the next down special entry clears it. */
#include "sonicbm.h"

#include <dolphin/os.h>

#include <melee/ft/fighter.h>
#include <melee/ft/ft_081B.h>
#include <melee/ft/ft_084E.h>
#include <melee/ft/ft_0881.h>
#include <melee/ft/ft_0892.h>
#include <melee/ft/ftanim.h>
#include <melee/ft/ftcoll.h>
#include <melee/ft/ftcommon.h>
#include <melee/ft/inlines.h>
#include <melee/ft/kinds/ftCommon/ftCo_Fall.h>
#include <melee/ft/kinds/ftCommon/ftCo_Jump.h>
#include <melee/ft/kinds/ftCommon/ftCo_JumpAerial.h>
#include <melee/ft/types.h>
#include <sysdolphin/baselib/controller.h>
#include <sysdolphin/baselib/gobj.h>

/* The shield and Z buttons (andi. 0x70 at +1A94, +1B40, +1DF0). */
#define ftSc_PAD_LRZ (HSD_PAD_Z | HSD_PAD_R | HSD_PAD_L)

/* The roll's target speed: (level * per level + base) fused in single, times his facing. */
static inline float ftSc_SpecialLw_RollSpeed(Fighter* fp)
{
    ftSc_DatAttrs* attrs = ftSc_Attrs(fp);
    float speed = MU_FMADDS((float) ftSc_FV(fp)->lw_level, attrs->speciallw_speed_add,
                            attrs->speciallw_speed);
    speed = speed * fp->facing_dir;
    return speed;
}

/* +095C (slot 10, speciallw): the level cleared, the first throw flag cleared, state 349. */
void ftSc_SpecialLw_Enter(HSD_GObj* gobj)
{
    Fighter* fp = GET_FIGHTER(gobj);

    ftSc_FV(fp)->lw_level = 0;
    fp->throw_flags_b0 = false;
    Fighter_ChangeMotionState(gobj, ftSc_MS_SpecialLw, Ft_MF_None, 0.0f, 1.0f, 0.0f, NULL);
    ftAnim_8006EBA4(gobj);
}

/* +09C4 (slot 11, specialairlw): the same into state 350. */
void ftSc_SpecialAirLw_Enter(HSD_GObj* gobj)
{
    Fighter* fp = GET_FIGHTER(gobj);

    ftSc_FV(fp)->lw_level = 0;
    fp->throw_flags_b0 = false;
    Fighter_ChangeMotionState(gobj, ftSc_MS_SpecialAirLw, Ft_MF_None, 0.0f, 1.0f, 0.0f, NULL);
    ftAnim_8006EBA4(gobj);
}

/* +1404: state 349, animation: at the end, the ground charge (354) and the charge sound. */
void ftSc_SpecialLw_Anim(HSD_GObj* gobj)
{
    Fighter* fp = GET_FIGHTER(gobj);

    if (ftAnim_IsFramesRemaining(gobj)) {
        return;
    }
    Fighter_ChangeMotionState(gobj, ftSc_MS_SpecialLwCharge, Ft_MF_None, 0.0f, 1.0f, 0.0f,
                              NULL);
    ft_800881D8(fp, FTSC_SFX_CHARGE, 0x7F, 0x43);
}

/* +381C: off the floor in 349, 354, 355 or 358. Into the air; a ground roll still at 0.5 or more
 * goes on as the air roll (359, flags 8) keeping its slot and frames, with hit_done as the word
 * 1 and the turn flag cleared; anything else falls. */
static void ftSc_SpecialLw_ToAir(HSD_GObj* gobj)
{
    Fighter* fp = GET_FIGHTER(gobj);
    ftSc_MotionVars* mv = ftSc_MV(fp);
    float speed;
    s32 slot;
    s32 count;

    ftCommon_8007D5D4(fp);
    speed = fp->self_vel.x;
    if (!(speed >= 0.0f)) {
        speed = -speed;
    }
    if (speed < 0.5f || fp->motion_id != ftSc_MS_SpecialLwRoll) {
        ftCo_Fall_Enter(gobj);
        return;
    }
    slot = mv->slot;
    count = mv->count;
    Fighter_ChangeMotionState(gobj, ftSc_MS_SpecialAirLwRoll, Ft_MF_SkipHit, 0.0f, 1.0f, 0.0f,
                              NULL);
    mv->slot = slot;
    mv->count = count;
    ftSc_MV_SetWord2370(fp, 1);
    mv->flag = 0;
}

/* +1498: states 349, 354, 355 and 358, collision. With the turn flag set: the retail ground
 * collision only (it may not leave the floor). Else off the floor: +381C. */
void ftSc_SpecialLw_Coll(HSD_GObj* gobj)
{
    if (ftSc_MV(GET_FIGHTER(gobj))->flag == 1) {
        ft_800827A0(gobj);
        return;
    }
    if (!ft_80082708(gobj)) {
        ftSc_SpecialLw_ToAir(gobj);
    }
}

/* +14F8: state 350, animation: at the end, the air charge (356) and the charge sound. */
void ftSc_SpecialAirLw_Anim(HSD_GObj* gobj)
{
    Fighter* fp = GET_FIGHTER(gobj);

    if (ftAnim_IsFramesRemaining(gobj)) {
        return;
    }
    Fighter_ChangeMotionState(gobj, ftSc_MS_SpecialAirLwCharge, Ft_MF_None, 0.0f, 1.0f, 0.0f,
                              NULL);
    ft_800881D8(fp, FTSC_SFX_CHARGE, 0x7F, 0x43);
}

/* +158C: states 350 and 356, physics: his gravity and his air friction. */
void ftSc_SpecialAirLw_Phys(HSD_GObj* gobj)
{
    Fighter* fp = GET_FIGHTER(gobj);

    ftCommon_Fall(fp, fp->co_attrs.gravity, fp->co_attrs.terminal_velocity);
    ftCommon_CalcSelfAccel_Deaccel(fp, fp->co_attrs.aerial_friction);
}

/* +38F8: landing in 350, 356 or 359. Grounded; the air roll goes on as the ground roll (358,
 * flags 8) keeping its slot and frames, hit_done the word 1, turn flag cleared. Else the stick
 * past 0.2 either way turns him, and he goes on in 349 (from 350) or 354 at the same frame. */
static void ftSc_SpecialAirLw_Landed(HSD_GObj* gobj)
{
    Fighter* fp = GET_FIGHTER(gobj);
    ftSc_MotionVars* mv = ftSc_MV(fp);
    float x;

    ftCommon_8007D6A4(fp);
    if (fp->motion_id == ftSc_MS_SpecialAirLwRoll) {
        s32 slot = mv->slot;
        s32 count = mv->count;
        Fighter_ChangeMotionState(gobj, ftSc_MS_SpecialLwRoll, Ft_MF_SkipHit, 0.0f, 1.0f, 0.0f,
                                  NULL);
        mv->slot = slot;
        mv->count = count;
        ftSc_MV_SetWord2370(fp, 1);
        mv->flag = 0;
        return;
    }
    x = fp->input.lstick[0].x;
    if (!(x >= 0.0f)) {
        x = -x;
    }
    if (x > 0.2) {
        ftCommon_UpdateFacing(fp);
    }
    Fighter_ChangeMotionState(gobj,
                              fp->motion_id == ftSc_MS_SpecialAirLw ? ftSc_MS_SpecialLw
                                                                    : ftSc_MS_SpecialLwCharge,
                              Ft_MF_None, fp->cur_anim_frame, 1.0f, 0.0f, NULL);
}

/* +15C4: states 350, 356 and 359, collision. */
void ftSc_SpecialAirLw_Coll(HSD_GObj* gobj)
{
    if (ft_80081D0C(gobj)) {
        ftSc_SpecialAirLw_Landed(gobj);
    }
}

/* +16A0: state 354, the ground charge, animation. See the file comment. Letting the stick go
 * starts the roll (358) even when the charge animation is still running; if that state's
 * animation then has no frames left, 357. */
void ftSc_SpecialLwCharge_Anim(HSD_GObj* gobj)
{
    Fighter* fp = GET_FIGHTER(gobj);
    ftSc_DatAttrs* attrs = ftSc_Attrs(fp);
    ftSc_FighterVars* fv = ftSc_FV(fp);

    if (!fp->throw_flags_b0) {
        fp->throw_flags_b0 = true;
        itSc_Trail_Spawn(gobj, gobj, mu_ak_article_kind(gobj, ftSc_Article_Trail), 0, 0,
                         fp->cur_pos.z);
    }

    if (fp->cmd_vars[1] != 0 && (fp->input.pressed_buttons & HSD_PAD_B)) {
        /* +1754 */
        s32 max = attrs->speciallw_level_max;
        fp->cmd_vars[1] = 0;
        if (fv->lw_level < max) {
            fv->lw_level += 1;
        }
        Fighter_ChangeMotionState(gobj, ftSc_MS_SpecialLwCharge, Ft_MF_None, 0.0f, 1.0f, 0.0f,
                                  NULL);
        ft_800881D8(fp, FTSC_SFX_CHARGE, 0x7F, 0x43);
    }
    if ((fp->input.held_buttons[0] & FTSC_PAD_STICK_DOWN) == 0) {
        /* +17BC */
        Fighter_ChangeMotionState(gobj, ftSc_MS_SpecialLwRoll, Ft_MF_None, 0.0f, 1.0f, 0.0f,
                                  NULL);
        ft_800881D8(fp, FTSC_SFX_ROLL, 0x7F, 0x43);
        ftSc_MV(fp)->flag = 0;
        ftSc_MV(fp)->count = 60;
        fp->self_vel.x = ftSc_SpecialLw_RollSpeed(fp);
    }
    if (ftAnim_IsFramesRemaining(gobj)) {
        return;
    }
    Fighter_ChangeMotionState(gobj, ftSc_MS_SpecialLwCancel, Ft_MF_None, 0.0f, 1.0f, 0.0f,
                              NULL);
}

/* +189C: state 355, animation: at the end, his speed vector written as ground speed and the
 * two ground knockback speeds (the three words of fp+80 into fp+EC), then the common run. */
void ftSc_SpecialLwStop_Anim(HSD_GObj* gobj)
{
    Fighter* fp = GET_FIGHTER(gobj);

    if (ftAnim_IsFramesRemaining(gobj)) {
        return;
    }
    fp->gr_vel = fp->self_vel.x;
    fp->xF0_ground_kb_vel = fp->self_vel.y;
    fp->xF4_ground_attacker_shield_kb_vel = fp->self_vel.z;
    Fighter_ChangeMotionState(gobj, ftCo_MS_Run, Ft_MF_None, 0.0f, 1.0f, 0.0f, NULL);
}

/* +1930: state 356, the air charge, animation: the afterimage chain on the first frame; at the
 * end, the retail fall (no level is taken in the air). */
void ftSc_SpecialAirLwCharge_Anim(HSD_GObj* gobj)
{
    Fighter* fp = GET_FIGHTER(gobj);

    if (!fp->throw_flags_b0) {
        fp->throw_flags_b0 = true;
        itSc_Trail_Spawn(gobj, gobj, mu_ak_article_kind(gobj, ftSc_Article_Trail), 0, 0,
                         fp->cur_pos.z);
    }
    if (!ftAnim_IsFramesRemaining(gobj)) {
        ftCo_Fall_Enter(gobj);
    }
}

/* +19CC: state 357, animation: at the end, the retail wait. */
void ftSc_SpecialLwCancel_Anim(HSD_GObj* gobj)
{
    if (!ftAnim_IsFramesRemaining(gobj)) {
        ft_8008A2BC(gobj);
    }
}

/* +1A18: state 358, the ground roll, animation. */
void ftSc_SpecialLwRoll_Anim(HSD_GObj* gobj)
{
    Fighter* fp = GET_FIGHTER(gobj);
    ftSc_DatAttrs* attrs = ftSc_Attrs(fp);
    ftSc_MotionVars* mv = ftSc_MV(fp);

    ftColl_8007ABD0(&fp->x914[0],
                    (u32) (ftSc_FV(fp)->lw_level * attrs->speciallw_damage_add +
                           attrs->speciallw_damage),
                    gobj);

    if (mv->flag == 0) {
        bool turn;
        if (fp->facing_dir < 0.0f) {
            /* +1AE0 */
            turn = !(fp->input.lstick[0].x <= 0.2);
        } else if (fp->facing_dir <= 0.0f) {
            turn = false;
        } else {
            turn = fp->input.lstick[0].x < -0.2;
        }
        if (turn) {
            /* +1AF4 */
            s32 count;
            ftCommon_UpdateFacing(fp);
            count = mv->count;
            Fighter_ChangeMotionState(gobj, ftSc_MS_SpecialLwRoll, Ft_MF_SkipHit, 0.0f, 1.0f,
                                      0.0f, NULL);
            mv->flag = 1;
            mv->count = count;
            if (fp->input.held_buttons[0] & ftSc_PAD_LRZ) {
                goto stop;
            }
            goto press;
        }
    }
    /* +1A90 */
    if (fp->input.held_buttons[0] & ftSc_PAD_LRZ) {
        goto stop;
    }
    goto press;

stop:
    /* +1B4C */
    Fighter_ChangeMotionState(gobj, ftSc_MS_SpecialLwStop, Ft_MF_None, 0.0f, 1.0f, 0.0f, NULL);
press:
    /* +1A9C */
    if ((fp->input.pressed_buttons & HSD_PAD_B) && mv->flag == 0) {
        /* +1BB4 */
        Fighter_ChangeMotionState(gobj, ftSc_MS_SpecialLwStop, Ft_MF_None, 0.0f, 1.0f, 0.0f,
                                  NULL);
    }
    /* +1AB4 */
    mv->count -= 1;
    if (mv->count <= 0) {
        Fighter_ChangeMotionState(gobj, ftSc_MS_SpecialLwStop, Ft_MF_None, 0.0f, 1.0f, 0.0f,
                                  NULL);
    }
}

/* +1BE0: state 358, interrupt: a jump input goes on as the air roll (359, flags 8) keeping its
 * slot and frames, hit_done the word 1, turn flag cleared, 2.0 up. */
void ftSc_SpecialLwRoll_IASA(HSD_GObj* gobj)
{
    Fighter* fp = GET_FIGHTER(gobj);
    ftSc_MotionVars* mv = ftSc_MV(fp);
    s32 slot;
    s32 count;

    if (ftCo_Jump_GetInput(gobj) == JumpInput_None) {
        return;
    }
    ftCommon_8007D5D4(fp);
    slot = mv->slot;
    count = mv->count;
    Fighter_ChangeMotionState(gobj, ftSc_MS_SpecialAirLwRoll, Ft_MF_SkipHit, 0.0f, 1.0f, 0.0f,
                              NULL);
    mv->slot = slot;
    mv->count = count;
    ftSc_MV_SetWord2370(fp, 1);
    mv->flag = 0;
    fp->self_vel.y = 2.0f;
}

/* +1C9C: state 358, physics: x speed eased toward the roll speed by 0.25 a frame (0.15 after a
 * turn, and on reaching it after a turn the turn flag is cleared with the roll sound). Every
 * compare as the file makes it. */
void ftSc_SpecialLwRoll_Phys(HSD_GObj* gobj)
{
    Fighter* fp = GET_FIGHTER(gobj);
    ftSc_MotionVars* mv = ftSc_MV(fp);
    float target = ftSc_SpecialLw_RollSpeed(fp);
    s32 flag = mv->flag;
    float facing = fp->facing_dir;
    float v = 0.0f;
    float step = 0.0f;

    if (flag == 0) {
        if (facing < 0.0f) {
            /* +1D88 */
            v = fp->self_vel.x;
            if (v <= target) {
                goto set_target;
            }
            step = 0.25f;
            goto ease_down;
        } else if (facing <= 0.0f) {
            goto set_target;
        }
        v = fp->self_vel.x;
        if (target <= v) {
            goto set_target;
        }
        v = v + 0.25f;
        if (target > v) {
            goto set_v;
        }
        goto set_target;
    }

    /* +1D30 */
    if (facing < 0.0f) {
        /* +1D70 */
        v = fp->self_vel.x;
        if (v <= target) {
            goto reached;
        }
        step = 0.15f;
        goto ease_down;
    } else if (facing <= 0.0f) {
        goto reached;
    }
    v = fp->self_vel.x;
    if (v >= target) {
        goto reached;
    }
    v = v + 0.15f;
    if (target <= v) {
        goto set_target;
    }
    goto set_v;

ease_down:
    /* +1D9C */
    v = v - step;
    if (target >= v) {
        goto set_target;
    }
set_v:
    fp->self_vel.x = v;
    return;

reached:
    /* +1DB0 */
    if (flag != 1) {
        goto set_target;
    }
    fp->self_vel.x = target;
    mv->flag = 0;
    ft_800881D8(fp, FTSC_SFX_ROLL, 0x7F, 0x43);
    return;

set_target:
    fp->self_vel.x = target;
}

/* +1DD8: state 359, the air roll, animation. A shield button or B pressed ends it (368; with
 * both, or with a shield button and then B, the state is set twice; a shield button alone goes
 * on to the hit and the countdown in 368). The hit by level; at the end of the 60 frames, 368
 * with an animation blend of 1.0 (the only state change of the file with a blend). */
void ftSc_SpecialAirLwRoll_Anim(HSD_GObj* gobj)
{
    Fighter* fp = GET_FIGHTER(gobj);
    ftSc_MotionVars* mv = ftSc_MV(fp);

    if (fp->input.pressed_buttons & ftSc_PAD_LRZ) {
        Fighter_ChangeMotionState(gobj, ftSc_MS_SpecialAirLwEnd, Ft_MF_None, 0.0f, 1.0f, 0.0f,
                                  NULL);
    }
    if (fp->input.pressed_buttons & HSD_PAD_B) {
        Fighter_ChangeMotionState(gobj, ftSc_MS_SpecialAirLwEnd, Ft_MF_None, 0.0f, 1.0f, 0.0f,
                                  NULL);
        return;
    }
    /* +1E00 */
    switch (ftSc_FV(fp)->lw_level) {
    case 1:
        ftColl_8007ABD0(&fp->x914[0], 6, gobj);
        break;
    case 2:
        ftColl_8007ABD0(&fp->x914[0], 8, gobj);
        break;
    case 3:
        ftColl_8007ABD0(&fp->x914[0], 9, gobj);
        break;
    case 4:
        ftColl_8007ABD0(&fp->x914[0], 11, gobj);
        break;
    default:
        break;
    }
    mv->count -= 1;
    if (mv->count <= 0) {
        Fighter_ChangeMotionState(gobj, ftSc_MS_SpecialAirLwEnd, Ft_MF_None, 0.0f, 1.0f, 1.0f,
                                  NULL);
    }
}

/* +1F90: state 359, interrupt. X or Y: the retail double jump check (its result is not used).
 * Then the retail fall interrupt once for A pressed and once more for each C-stick direction
 * past 0.31 (so up to five calls in one frame; kept). */
void ftSc_SpecialAirLwRoll_IASA(HSD_GObj* gobj)
{
    Fighter* fp = GET_FIGHTER(gobj);

    if (fp->input.pressed_buttons & HSD_PAD_XY) {
        ftCo_800CB870(gobj);
    }
    if (fp->input.pressed_buttons & HSD_PAD_A) {
        ftCo_Fall_IASA_Inner(gobj);
    }
    if (fp->input.cstick[0].x >= 0.31) {
        ftCo_Fall_IASA_Inner(gobj);
    }
    if (fp->input.cstick[0].x <= -0.31) {
        ftCo_Fall_IASA_Inner(gobj);
    }
    if (fp->input.cstick[0].y >= 0.31) {
        ftCo_Fall_IASA_Inner(gobj);
    }
    if (fp->input.cstick[0].y <= -0.31) {
        ftCo_Fall_IASA_Inner(gobj);
    }
}

/* +20DC: state 359, physics: his gravity and the retail drift. */
void ftSc_SpecialAirLwRoll_Phys(HSD_GObj* gobj)
{
    Fighter* fp = GET_FIGHTER(gobj);

    ftCommon_Fall(fp, fp->co_attrs.gravity, fp->co_attrs.terminal_velocity);
    ftCommon_CalcSelfAccel_Drift(fp);
}

/* +27C4: state 368, animation: at the end, the retail fall. */
void ftSc_SpecialAirLwEnd_Anim(HSD_GObj* gobj)
{
    if (!ftAnim_IsFramesRemaining(gobj)) {
        ftCo_Fall_Enter(gobj);
    }
}

/* +2810: state 368, physics: the retail air physics, then an x speed over his jump's largest x
 * speed is cut to 0.9 of itself (in double). */
void ftSc_SpecialAirLwEnd_Phys(HSD_GObj* gobj)
{
    Fighter* fp = GET_FIGHTER(gobj);

    ft_80084EEC(gobj);
    if (fp->self_vel.x > fp->co_attrs.jump_h_max_velocity) {
        fp->self_vel.x = (float) (fp->self_vel.x * 0.9);
    }
}

/* +2860: state 368, collision: on landing, the retail landing. */
void ftSc_SpecialAirLwEnd_Coll(HSD_GObj* gobj)
{
    if (ft_80081D0C(gobj)) {
        ft_80082B1C(gobj);
    }
}
