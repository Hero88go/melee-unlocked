/* ACE's Wario: the neutral special (states 341 and 342), his own code.
 *
 * A punch. During its first frames (attribute +00, 7) a stick pushed against his facing turns him
 * around and restarts the same state at the same frame. The animation callbacks do that turn
 * (ground threshold +50, air threshold +04); the physics callbacks hold a second turn that also
 * reverses his x speed, behind attribute +0C, which is 0 on the disc.
 *
 * The frame limit is an integer on the disc, converted unsigned (the 0x59800000 constant at
 * +1F4C is 2^52, the unsigned conversion bias). */
#include "wario.h"

#include <melee/ft/fighter.h>
#include <melee/ft/ft_081B.h>
#include <melee/ft/ft_084E.h>
#include <melee/ft/ft_0892.h>
#include <melee/ft/ftanim.h>
#include <melee/ft/ftcommon.h>
#include <melee/ft/inlines.h>
#include <melee/ft/kinds/ftCommon/ftCo_Fall.h>
#include <melee/ft/types.h>

/* +0254 (slot 4, specialn) */
void ftWr_SpecialN_Enter(HSD_GObj* gobj)
{
    Fighter* fp = GET_FIGHTER(gobj);

    fp->cmd_vars[0] = 0;
    Fighter_ChangeMotionState(gobj, ftWr_MS_SpecialN, Ft_MF_None, 0.0f, 1.0f, 0.0f, NULL);
    ftAnim_8006EBA4(gobj);
}

/* +02B0 (slot 5, specialairn) */
void ftWr_SpecialAirN_Enter(HSD_GObj* gobj)
{
    Fighter* fp = GET_FIGHTER(gobj);

    fp->cmd_vars[0] = 0;
    Fighter_ChangeMotionState(gobj, ftWr_MS_SpecialAirN, Ft_MF_None, 0.0f, 1.0f, 0.0f, NULL);
    ftAnim_8006EBA4(gobj);
}

/* The test the four callbacks share (+08E8, +0A04, +0B9C, +0CCC): the stick is past the
 * threshold on the side away from his facing. A facing of exactly 0 never turns. */
static bool ftWr_SpecialN_StickAgainstFacing(Fighter* fp, float threshold)
{
    if (fp->facing_dir > 0.0f) {
        return fp->input.lstick[0].x < -threshold;
    }
    if (fp->facing_dir < 0.0f) {
        return fp->input.lstick[0].x > threshold;
    }
    return false;
}

/* +0890: state 341, animation. */
void ftWr_SpecialN_Anim(HSD_GObj* gobj)
{
    Fighter* fp = GET_FIGHTER(gobj);
    ftWr_DatAttrs* sa = ftWr_Attrs(fp);

    if (fp->cur_anim_frame < (float) sa->specialn_turn_frames &&
        ftWr_SpecialN_StickAgainstFacing(fp, sa->specialn_ground_turn_stick))
    {
        ftCommon_UpdateFacing(fp);
        Fighter_ChangeMotionState(gobj, fp->motion_id, Ft_MF_None, fp->cur_anim_frame, 1.0f,
                                  0.0f, NULL);
    }
    if (!ftAnim_IsFramesRemaining(gobj)) {
        ft_8008A2BC(gobj);
    }
}

/* +09A8: state 341, physics. The turn is behind attribute +0C (0 on the disc). */
void ftWr_SpecialN_Phys(HSD_GObj* gobj)
{
    Fighter* fp = GET_FIGHTER(gobj);
    ftWr_DatAttrs* sa = ftWr_Attrs(fp);

    if (sa->specialn_phys_turn != 0 &&
        fp->cur_anim_frame < (float) sa->specialn_turn_frames &&
        ftWr_SpecialN_StickAgainstFacing(fp, sa->specialn_turn_stick))
    {
        ftCommon_UpdateFacing(fp);
        fp->self_vel.x = -fp->self_vel.x;
        Fighter_ChangeMotionState(gobj, fp->motion_id, Ft_MF_None, fp->cur_anim_frame, 1.0f,
                                  0.0f, NULL);
    }
    ft_80084F3C(gobj);
}

/* +0AB4: state 341, collision. Off the floor the air state goes on at the same frame. */
void ftWr_SpecialN_Coll(HSD_GObj* gobj)
{
    Fighter* fp = GET_FIGHTER(gobj);

    if (!ft_80082708(gobj)) {
        ftCommon_8007D5D4(fp);
        Fighter_ChangeMotionState(gobj, ftWr_MS_SpecialAirN, ftWr_MF_SpecialN_Coll,
                                  fp->cur_anim_frame, 1.0f, 0.0f, NULL);
        ftCommon_ClampAirDrift(fp);
    }
}

/* +0B44: state 342, animation. As +0890 with the air threshold (+04), ending in the fall. */
void ftWr_SpecialAirN_Anim(HSD_GObj* gobj)
{
    Fighter* fp = GET_FIGHTER(gobj);
    ftWr_DatAttrs* sa = ftWr_Attrs(fp);

    if (fp->cur_anim_frame < (float) sa->specialn_turn_frames &&
        ftWr_SpecialN_StickAgainstFacing(fp, sa->specialn_turn_stick))
    {
        ftCommon_UpdateFacing(fp);
        Fighter_ChangeMotionState(gobj, fp->motion_id, Ft_MF_None, fp->cur_anim_frame, 1.0f,
                                  0.0f, NULL);
    }
    if (!ftAnim_IsFramesRemaining(gobj)) {
        ftCo_Fall_Enter(gobj);
    }
}

/* +0C5C: state 342, physics. The common fall first, then the turn behind attribute +0C. */
void ftWr_SpecialAirN_Phys(HSD_GObj* gobj)
{
    Fighter* fp = GET_FIGHTER(gobj);
    ftWr_DatAttrs* sa = ftWr_Attrs(fp);

    ftCommon_FallBasic(fp);
    if (sa->specialn_phys_turn != 0 &&
        fp->cur_anim_frame < (float) sa->specialn_turn_frames &&
        ftWr_SpecialN_StickAgainstFacing(fp, sa->specialn_turn_stick))
    {
        ftCommon_UpdateFacing(fp);
        fp->self_vel.x = -fp->self_vel.x;
        Fighter_ChangeMotionState(gobj, fp->motion_id, Ft_MF_None, fp->cur_anim_frame, 1.0f,
                                  0.0f, NULL);
    }
}

/* +0D78: state 342, collision. Landing: with command variable 0 raised by the script he goes to
 * the wait state, otherwise the ground state goes on at the same frame. */
void ftWr_SpecialAirN_Coll(HSD_GObj* gobj)
{
    Fighter* fp = GET_FIGHTER(gobj);

    if (ft_80081D0C(gobj)) {
        if (fp->cmd_vars[0] != 0) {
            ft_8008A2BC(gobj);
        } else {
            ftCommon_8007D7FC(fp);
            Fighter_ChangeMotionState(gobj, ftWr_MS_SpecialN, ftWr_MF_SpecialN_Coll,
                                      fp->cur_anim_frame, 1.0f, 0.0f, NULL);
        }
    }
}
