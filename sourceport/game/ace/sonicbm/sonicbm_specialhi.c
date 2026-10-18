/* ACE's Sonic BM: the up special, a spring (states 347, 348; the spring is article 0 in
 * sonicbm_items.c), the file's own code.
 *
 * The entries mark the up and side specials used, take his jumps and clear the script's words;
 * the ground one also clears fp+22D4, which kills a spring still out (its animation callback
 * tests the word). In the first six frames the stick held back turns him. When the script raises
 * the first throw flag a spring is put at his feet (unless he came here by bouncing on his own
 * spring) and fp+22D4 is set. The script's cmd_vars[1] = 1 launches him (3.75 up); from then on
 * he falls with his own gravity. Once the script sets cmd_vars[2] = 1, A, B or the C-stick
 * leave into the retail fall interrupt. At the end: a fall with no jumps left. */
#include "sonicbm.h"

#include <dolphin/os.h>

#include <melee/ft/fighter.h>
#include <melee/ft/ft_081B.h>
#include <melee/ft/ftanim.h>
#include <melee/ft/ftcommon.h>
#include <melee/ft/ftparts.h>
#include <melee/ft/inlines.h>
#include <melee/ft/kinds/ftCommon/ftCo_Fall.h>
#include <melee/ft/types.h>
#include <sysdolphin/baselib/controller.h>
#include <sysdolphin/baselib/gobj.h>

/* +07F0 (slot 8, specialhi): once per air time. */
void ftSc_SpecialHi_Enter(HSD_GObj* gobj)
{
    Fighter* fp = GET_FIGHTER(gobj);
    ftSc_FighterVars* fv = ftSc_FV(fp);
    int max_jumps;

    if (fv->up_used == 1) {
        return;
    }
    max_jumps = fp->co_attrs.max_jumps;
    fv->spring_live = 0;
    fp->cmd_vars[1] = 0;
    fp->cmd_vars[2] = 0;
    fp->throw_flags_b0 = false;
    fp->x1968_jumpsUsed = (u8) max_jumps;
    fv->up_used = 1;
    fv->side_used = 1;
    Fighter_ChangeMotionState(gobj, ftSc_MS_SpecialHi, Ft_MF_None, 0.0f, 1.0f, 0.0f, NULL);
    ftSc_MV(fp)->hi_air = 0;
    ftSc_MV(fp)->hi_bounced = 0;
    ftAnim_8006EBA4(gobj);
}

/* +08A4 (slot 9, specialairhi): the same without clearing fp+22D4; the spring's state is 1. */
void ftSc_SpecialAirHi_Enter(HSD_GObj* gobj)
{
    Fighter* fp = GET_FIGHTER(gobj);
    ftSc_FighterVars* fv = ftSc_FV(fp);
    int max_jumps;

    if (fv->up_used == 1) {
        return;
    }
    max_jumps = fp->co_attrs.max_jumps;
    fv->up_used = 1;
    fv->side_used = 1;
    fp->cmd_vars[1] = 0;
    fp->cmd_vars[2] = 0;
    fp->throw_flags_b0 = false;
    fp->x1968_jumpsUsed = (u8) max_jumps;
    Fighter_ChangeMotionState(gobj, ftSc_MS_SpecialAirHi, Ft_MF_None, 0.0f, 1.0f, 0.0f, NULL);
    ftSc_MV(fp)->hi_air = 1;
    ftSc_MV(fp)->hi_bounced = 0;
    ftAnim_8006EBA4(gobj);
}

/* +10F4: states 347 and 348, animation. */
void ftSc_SpecialHi_Anim(HSD_GObj* gobj)
{
    Fighter* fp = GET_FIGHTER(gobj);

    /* cror eq, gt, un: the turn window is frame 6.0 or less */
    if (fp->cur_anim_frame <= 6.0f) {
        if (fp->facing_dir <= 0.0f) {
            /* +1184 */
            if (fp->facing_dir >= 0.0f) {
                goto check_flag;
            }
            if (fp->input.lstick[0].x <= 0.275) {
                goto check_flag;
            }
        } else if (!(fp->input.lstick[0].x < -0.275)) {
            goto check_flag;
        }
        /* +119C */
        ftCommon_UpdateFacing(fp);
        ftPartSetRotY(fp, 0, fp->facing_dir * 0x1.921fb6p+0f);
        if (!fp->throw_flags_b0) {
            goto check_end;
        }
        goto spawn;
    }
check_flag:
    if (fp->throw_flags_b0) {
        goto spawn;
    }
check_end:
    if (ftAnim_IsFramesRemaining(gobj)) {
        return;
    }
    goto fall;

spawn:
    /* +11CC */
    if (ftSc_MV(fp)->hi_bounced != 0) {
        goto check_end;
    }
    fp->throw_flags_b0 = false;
    itSc_Spring_Spawn(gobj, &fp->cur_pos, mu_ak_article_kind(gobj, ftSc_Article_Spring),
                      ftSc_MV(fp)->hi_air);
    ftSc_FV(fp)->spring_live = 1;
    if (ftAnim_IsFramesRemaining(gobj)) {
        return;
    }

fall:
    /* +121C */
    ftCo_Fall_Enter(gobj);
    fp->x1968_jumpsUsed = (u8) fp->co_attrs.max_jumps;
}

/* +1244: states 347 and 348, interrupt. Once the script sets cmd_vars[2] = 1: A or B pressed,
 * or the C-stick past 0.1 on either axis, runs the retail fall interrupt (its result is not
 * used) and leaves no jumps. */
void ftSc_SpecialHi_IASA(HSD_GObj* gobj)
{
    Fighter* fp = GET_FIGHTER(gobj);
    float c;

    if (fp->cmd_vars[2] != 1) {
        return;
    }
    if ((fp->input.pressed_buttons & (HSD_PAD_A | HSD_PAD_B)) == 0) {
        c = fp->input.cstick[0].x;
        if (!(c >= 0.0f)) {
            c = -c;
        }
        if (!(c > 0.1)) {
            c = fp->input.cstick[0].y;
            if (c < 0.0f) {
                c = -c;
            }
            if (c <= 0.1) {
                return;
            }
        }
    }
    ftCo_Fall_IASA_Inner(gobj);
    fp->x1968_jumpsUsed = (u8) fp->co_attrs.max_jumps;
}

/* +12F4: states 347 and 348, physics. cmd_vars[1] = 1 from the script: into the air, 3.75 up,
 * no x speed, and cmd_vars[1] = 2; while it is 2, his gravity; always the retail simple drift
 * with his aerial drift attributes. */
void ftSc_SpecialHi_Phys(HSD_GObj* gobj)
{
    Fighter* fp = GET_FIGHTER(gobj);

    if (fp->cmd_vars[1] == 1) {
        fp->cmd_vars[1] = 2;
        ftCommon_8007D5D4(fp);
        fp->self_vel.y = 3.75f;
        fp->self_vel.x = 0.0f;
    } else if (fp->cmd_vars[1] == 2) {
        ftCommon_Fall(fp, fp->co_attrs.gravity, fp->co_attrs.terminal_velocity);
    }
    ftCommon_CalcSelfAccel_DriftSimple(fp, 0.0f, fp->co_attrs.aerial_drift_base,
                                       fp->co_attrs.air_drift_max);
}

/* +13B8: states 347 and 348, collision: the air collision for both (also the ground state);
 * on landing, the retail landing. */
void ftSc_SpecialHi_Coll(HSD_GObj* gobj)
{
    if (ft_80081D0C(gobj)) {
        ft_80082B1C(gobj);
    }
}
