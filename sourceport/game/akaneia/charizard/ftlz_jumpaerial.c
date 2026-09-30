/* Charizard's two midair jumps (states 341, 342), entered by the common multi-jump code
 * (ftCo_800D74A4) through fp->x2D0. The four callbacks are Charizard's own copies; they behave like
 * ftCo_JumpAerialF1_*, which Jigglypuff's jumps use. */
#include "ftlizardon.h"

#include <melee/ft/fighter.h>
#include <melee/ft/ft_081B.h>
#include <melee/ft/ftanim.h>
#include <melee/ft/ftcliffcommon.h>
#include <melee/ft/ftcommon.h>
#include <melee/ft/inlines.h>
#include <melee/ft/kinds/ftCommon/ftCo_Fall.h>
#include <melee/ft/kinds/ftCommon/ftCo_FallAerial.h>
#include <melee/ft/kinds/ftCommon/ftCo_JumpAerial.h>
#include <melee/ft/ftwalljump.h>
#include <melee/ft/types.h>

/* PlLz JumpAerial_AnimCB: the turn-around spin, then fall once the animation ends. */
void ftLz_JumpAerial_Anim(HSD_GObj* gobj)
{
    Fighter* fp = GET_FIGHTER(gobj);

    ft_800CB6EC(fp, fp->x2D0->x0);
    if (!ftAnim_IsFramesRemaining(gobj)) {
        if (fp->x1968_jumpsUsed < fp->co_attrs.max_jumps) {
            ftCo_Fall_Enter(gobj);
        } else {
            ftCo_FallAerial_Enter(gobj);
        }
    }
}

/* PlLz JumpAerial_IASACB. */
void ftLz_JumpAerial_IASA(HSD_GObj* gobj)
{
    ftCo_Fall_IASA_Inner(gobj);
}

/* PlLz JumpAerial_PhysCB: ft_80084E1C with the multi-jump drift multipliers, except that the stick
 * has to be strictly past the threshold to drift. */
void ftLz_JumpAerial_Phys(HSD_GObj* gobj)
{
    Fighter* fp = GET_FIGHTER(gobj);
    ftCo_DatAttrs* co = &fp->co_attrs;
    float threshold = p_ftCommonData->x258;
    float drift_mul = co->air_drift_stick_mul;
    float drift_max = co->air_drift_max;
    float jump_drift_mul = fp->x2D0->xC;
    float jump_max_mul = fp->x2D0->x10;
    float stick_x;
    float accel;
    float target;

    ftCommon_CheckFallFast(fp);
    if (fp->fall_fast) {
        fp->self_vel.y = -co->fast_fall_velocity;
    } else {
        ftCommon_Fall(fp, co->gravity, co->terminal_velocity);
    }

    stick_x = fp->input.lstick[0].x;
    if (threshold < (stick_x < 0.0F ? -stick_x : stick_x)) {
        accel = stick_x * (drift_mul * jump_drift_mul);
        target = stick_x * (drift_max * jump_max_mul);
    } else {
        target = 0.0F;
        accel = 0.0F;
    }
    ftCommon_CalcSelfAccel_AccelToVelClampedFrom(fp, fp->self_vel.x, accel, target,
                                                 co->aerial_friction);
}

/* PlLz JumpAerial_CollCB: ft_80082F28's landing / walljump / ledge check, landing through
 * ft_80082B1C. */
void ftLz_JumpAerial_Coll(HSD_GObj* gobj)
{
    Fighter* fp = GET_FIGHTER(gobj);

    if (ft_CheckGroundAndLedge(gobj, fp->facing_dir >= 0.0F ? 1 : -1)) {
        ft_80082B1C(gobj);
    } else if (!ftWallJump_8008169C(gobj)) {
        ftCliffCommon_80081298(gobj);
    }
}
