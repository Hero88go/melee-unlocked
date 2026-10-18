/* ACE's Meta Knight: the common states his load rewrites in his copy of the common state table
 * (AttackS3S 53, JumpAerialF 27, JumpAerialB 28, AttackS4S 60), and his own rows that continue
 * them: 354 and 369 (the second and third forward tilt), 365 and 366 (the later air jumps), 367
 * (forward smash held on A).
 *
 * The forward tilt chain: while the script sets cmd_vars[2] to 1, an A press goes on to the
 * next hit (53 -> 354 -> 369).
 * The forward smash: before frame 22, with cmd_vars[1] set by the script and fp+236C still 1
 * (onsmashf), A held enters 367, which counts the frames held in fp+2368 and returns to the
 * smash at frame 22 when A is let go or the hold animation ends; back in the smash every active
 * hitbox has its base damage scaled once by 1 + 0.0061183 * frames held. */
#include "metaknight.h"

#include <melee/ft/fighter.h>
#include <melee/ft/ft_081B.h>
#include <melee/ft/ft_0892.h>
#include <melee/ft/ftanim.h>
#include <melee/ft/ftcoll.h>
#include <melee/ft/ftcommon.h>
#include <melee/ft/inlines.h>
#include <melee/ft/kinds/ftCommon/ftCo_Fall.h>
#include <melee/ft/types.h>
#include <sysdolphin/baselib/controller.h>
#include <sysdolphin/baselib/jobj.h>

/* +0AA0: AttackS4S animation (his copy of the common table). */
void ftMk_AttackS4S_Anim(HSD_GObj* gobj)
{
    Fighter* fp = GET_FIGHTER(gobj);
    ftMk_MotionVars* mv = ftMk_MVars(fp);
    int i;

    if (!(fp->cur_anim_frame >= 22.0f) && (s32) fp->cmd_vars[1] == 1) {
        /* +0BF8 */
        if (mv->x236C == 1 && (fp->input.held_buttons[0] & HSD_PAD_A)) {
            const s32 msid = fp->motion_id;
            fp->cmd_vars[1] = 0;
            mv->x236C = 0;
            mv->x2368.i = 0;
            FTMK_CHANGE(gobj, ftMk_MS_AttackS4Hold);
            mv->x2364.i = msid;
        }
    }

    /* +0AE8: hitboxes fp+914 to fp+DF4 (four), one mark byte each at fp+2370 */
    for (i = 0; i < 4; i++) {
        HitCapsule* hit = &fp->x914[i];
        if ((s32) hit->state != 0 && mv->x2370[i] != 1) {
            float scale;
            float base;
            mv->x2370[i] = 1;
            /* fmadd (double) of the frames held, then frsp; the base damage word as an
             * integer; fmuls; fctiwz */
            scale = (float) __builtin_fma((double) mv->x2368.i, 0.006118333333333333, 1.0);
            base = (float) (double) (s32) hit->unk_count;
            ftColl_8007ABD0(hit, (u32) (s32) (scale * base), gobj);
        }
    }

    if (!ftAnim_IsFramesRemaining(gobj)) {
        ft_8008A2BC(gobj);
    }
}

/* +0C50: animation of AttackS3S (his copy), 354 and 369. */
void ftMk_AttackS3S_Anim(HSD_GObj* gobj)
{
    if (!ftAnim_IsFramesRemaining(gobj)) {
        ft_8008A2BC(gobj);
    }
}

/* +0C9C: interrupt of AttackS3S (his copy): with cmd_vars[2] == 1 an A press goes to 354. */
void ftMk_AttackS3S_IASA(HSD_GObj* gobj)
{
    Fighter* fp = GET_FIGHTER(gobj);

    if ((s32) fp->cmd_vars[2] != 1) {
        return;
    }
    if (!(fp->input.pressed_buttons & HSD_PAD_A)) {
        return;
    }
    fp->cmd_vars[2] = 0;
    FTMK_CHANGE(gobj, ftMk_MS_AttackS3S2);
}

/* +1F6C: interrupt of 354: the same, to 369. */
void ftMk_AttackS3S2_IASA(HSD_GObj* gobj)
{
    Fighter* fp = GET_FIGHTER(gobj);

    if ((s32) fp->cmd_vars[2] != 1) {
        return;
    }
    if (!(fp->input.pressed_buttons & HSD_PAD_A)) {
        return;
    }
    fp->cmd_vars[2] = 0;
    FTMK_CHANGE(gobj, ftMk_MS_AttackS3S3);
}

/* +0CE4: animation of JumpAerialF and JumpAerialB (his copy), 365 and 366.
 * The turn the double jump chose (fp+2368): 2 turns the top joint's y rotation up to pi/2, 1 up
 * to 3pi/2, pi / fp+236C a frame. While fp+2368 is set, the first frame past 6 turns his facing
 * (once per jump: the word fp+2228). X, Y or bit 16 held jumps again at once; else at the end
 * of the animation 366 goes to FallAerial (blend 100), anything else to Fall (blend 1). */
void ftMk_JumpAerial_Anim(HSD_GObj* gobj)
{
    Fighter* fp = GET_FIGHTER(gobj);
    ftMk_MotionVars* mv = ftMk_MVars(fp);
    const s32 turn = mv->x2368.i;

    if (turn > 0) {
        HSD_JObj* jobj = fp->parts[0].joint;
        if (turn == 2) {
            /* +0DC4 */
            const float rot = jobj->rotate.y;
            if (!(rot >= 1.5707963267948966)) {
                jobj->rotate.y =
                    (float) (3.141592653589793 / (double) mv->x236C + (double) rot);
            }
        } else if (turn == 1) {
            /* +0E44 */
            const float rot = jobj->rotate.y;
            if (!(rot >= 4.71238898038469)) {
                jobj->rotate.y =
                    (float) (3.141592653589793 / (double) mv->x236C + (double) rot);
            }
        }
        /* +0D1C */
        if (ftMk_Word2228_IsZero(fp) && fp->cur_anim_frame > 6.0f) {
            ftMk_Word2228_Set(fp, 1);
            fp->facing_dir = -fp->facing_dir;
        }
    }

    /* +0D3C */
    if (fp->input.held_buttons[0] & FTMK_PAD_JUMP) {
        ftMk_Init_EnterDoubleJump(gobj);
        return;
    }
    if (ftAnim_IsFramesRemaining(gobj)) {
        return;
    }
    if (fp->motion_id == ftMk_MS_JumpAerialLast) {
        Fighter_ChangeMotionState(gobj, ftCo_MS_FallAerial, Ft_MF_None, 0.0f, 1.0f, 100.0f, NULL);
    } else {
        Fighter_ChangeMotionState(gobj, ftCo_MS_Fall, Ft_MF_None, 0.0f, 1.0f, 1.0f, NULL);
    }
}

/* +2B10: interrupt of 365 and 366: a branch to the common fall's interrupt. */
void ftMk_JumpAerial_IASA(HSD_GObj* gobj)
{
    ftCo_Fall_IASA_Inner(gobj);
}

/* +2B14: physics of 365 and 366. */
void ftMk_JumpAerial_Phys(HSD_GObj* gobj)
{
    Fighter* fp = GET_FIGHTER(gobj);

    ft_80084DB0(gobj);
    ftCommon_CalcSelfAccel_Drift(fp);
}

/* +2B48: animation of 367: one more frame held; with A let go, or at the end of the hold
 * animation, back to the saved state (fp+2364) at frame 22, and the damage multiplier fp+2120
 * set to 1.3671. */
void ftMk_AttackS4Hold_Anim(HSD_GObj* gobj)
{
    Fighter* fp = GET_FIGHTER(gobj);
    ftMk_MotionVars* mv = ftMk_MVars(fp);
    const HSD_Pad held = fp->input.held_buttons[0] & HSD_PAD_A;

    mv->x2368.i = mv->x2368.i + 1;
    if (held) {
        if (ftAnim_IsFramesRemaining(gobj)) {
            return;
        }
    }
    Fighter_ChangeMotionState(gobj, mv->x2364.i, Ft_MF_None, 22.0f, 1.0f, 0.0f, NULL);
    fp->smash_attrs.x2120_damageMul = 1.3671f;
}
