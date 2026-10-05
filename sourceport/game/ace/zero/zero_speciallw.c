/* ACE's Zero: the down special, the dive (states 358, 359, 360 and 362; +17D0 to +1DAC of the
 * "ftFunction" block of PlZx.dat).
 *
 * Both entries are Link's (ftLk_SpecialLw_Enter and ftLk_SpecialAirLw_Enter, the m-ex defaults
 * of slots 10 and 11), which also install Link's bomb callback (zero.c). The physics callbacks
 * write a speed and then call Link's retail callback of the same state by absolute address
 * (0x800EB91C, 0x800EB93C), as does the air collision callback (0x800EB9D4).
 *
 *   358  ground: forward at 3 from frame 10, 0.7 from frame 25, stopped from frame 44
 *   359  air: 2 forward and 3 down from frame 10, 0.75 forward and 1.5 down from frame 25
 *   360  the ground dive after it left the floor (Link's hookshot state, replaced)
 *   362  the landing of the air dive */
#include "zero.h"

#include <melee/ft/fighter.h>
#include <melee/ft/ft_081B.h>
#include <melee/ft/ft_084E.h>
#include <melee/ft/ft_0892.h>
#include <melee/ft/ftanim.h>
#include <melee/ft/ftcommon.h>
#include <melee/ft/inlines.h>
#include <melee/ft/kinds/ftLink/ftlinkspeciallw.h>
#include <melee/ft/types.h>

/* +17D0 "SpecialLw_Phys" (state 358): the common ground physics, the ground speed by the frame,
 * then Link's ftLk_SpecialLw_Phys (the common ground physics once more). */
void ftZx_SpecialLw_Phys(HSD_GObj* gobj)
{
    Fighter* fp = GET_FIGHTER(gobj);
    float frame;

    ft_80084F3C(gobj);
    frame = fp->cur_anim_frame;
    if (!(frame < 10.0f)) {
        if (!(frame >= 25.0f)) {
            fp->gr_vel = fp->facing_dir * 3.0f;
        } else if (!(frame >= 44.0f)) {
            fp->gr_vel = (float) ((double) fp->facing_dir * 0.7);
        } else {
            fp->gr_vel = 0.0f;
        }
    }
    ftLk_SpecialLw_Phys(gobj);
}

/* +189C "SpecialLw_Coll" (state 358): off the floor, state 360 at the same frame. */
void ftZx_SpecialLw_Coll(HSD_GObj* gobj)
{
    Fighter* fp = GET_FIGHTER(gobj);

    if (!ft_80082708(gobj)) {
        ftCommon_8007D5D4(fp);
        Fighter_ChangeMotionState(gobj, ftZx_MS_SpecialAirLwFall, Ft_MF_None,
                                  fp->cur_anim_frame, 1.0f, 0.0f, NULL);
    }
}

/* +1914 "SpecialAirLw_Phys" (state 359): the dive velocity by the frame, then Link's
 * ftLk_SpecialAirLw_Phys (the common air physics). */
void ftZx_SpecialAirLw_Phys(HSD_GObj* gobj)
{
    Fighter* fp = GET_FIGHTER(gobj);
    float frame = fp->cur_anim_frame;

    if (!(frame < 10.0f)) {
        float facing = fp->facing_dir;

        fp->self_vel.x = facing + facing;
        fp->self_vel.y = -3.0f;
        if (!(frame < 25.0f)) {
            fp->self_vel.x = facing * 0.75f;
            fp->self_vel.y = -1.5f;
        }
    }
    ftLk_SpecialAirLw_Phys(gobj);
}

/* The landing of the dive, shared by +19A4 and +1BBC: state 362 by the frame he lands on.
 *   up to 12     at frame 3
 *   13 to 25     at frame 0, ground speed 1 times his facing
 *   26 and on    at frame 3, ground speed 1 times his facing
 * The file tests the frame again after each state change (the new state's frame, 3 or 0, fails
 * the later tests), so one landing makes one change. No ftAnim_8006EBA4 and no air to ground
 * bookkeeping: the state change only. */
static void ftZx_SpecialAirLw_Land(HSD_GObj* gobj, Fighter* fp)
{
    if (!(fp->cur_anim_frame > 12.0f)) {
        /* +1A88 */
        Fighter_ChangeMotionState(gobj, ftZx_MS_SpecialLwLand, Ft_MF_None, 3.0f, 1.0f, 0.0f,
                                  NULL);
    }
    /* +19E4 */
    if (fp->cur_anim_frame < 13.0f) {
        return;
    }
    if (!(fp->cur_anim_frame > 25.0f)) {
        /* +1AB8 */
        Fighter_ChangeMotionState(gobj, ftZx_MS_SpecialLwLand, Ft_MF_None, 0.0f, 1.0f, 0.0f,
                                  NULL);
        fp->gr_vel = fp->facing_dir;
    }
    /* +1A10 */
    if (fp->cur_anim_frame < 26.0f) {
        return;
    }
    Fighter_ChangeMotionState(gobj, ftZx_MS_SpecialLwLand, Ft_MF_None, 3.0f, 1.0f, 0.0f, NULL);
    fp->gr_vel = fp->facing_dir;
}

/* +19A4 "SpecialAirLw_Coll" (state 359): the landing above, then Link's ftLk_SpecialAirLw_Coll
 * either way (which tests the floor again and would take Link's ground state 358). */
void ftZx_SpecialAirLw_Coll(HSD_GObj* gobj)
{
    Fighter* fp = GET_FIGHTER(gobj);

    if (ft_80081D0C(gobj)) {
        ftZx_SpecialAirLw_Land(gobj, fp);
    }
    ftLk_SpecialAirLw_Coll(gobj);
}

/* +1AEC "SpecialAirLw2_AnimCB" (state 360): empty. */
void ftZx_SpecialAirLwFall_Anim(HSD_GObj* gobj)
{
    (void) gobj;
}

/* +1AF0 "SpecialAirLw2_IASACB" (state 360): empty. */
void ftZx_SpecialAirLwFall_IASA(HSD_GObj* gobj)
{
    (void) gobj;
}

/* +1AF4 "SpecialAirLw2_Phys" (state 360): the air bookkeeping every frame (ftCommon_8007D5D4),
 * a forward speed by the frame and no vertical speed written and no common physics; the fall
 * state when the animation ends. */
void ftZx_SpecialAirLwFall_Phys(HSD_GObj* gobj)
{
    Fighter* fp = GET_FIGHTER(gobj);
    float frame;

    ftCommon_8007D5D4(fp);
    frame = fp->cur_anim_frame;
    if (!(frame < 10.0f)) {
        if (frame >= 25.0f) {
            fp->self_vel.x = fp->facing_dir * 0.75f;
        } else {
            fp->self_vel.x = fp->facing_dir + fp->facing_dir;
        }
    }
    if (!ftAnim_IsFramesRemaining(gobj)) {
        Fighter_ChangeMotionState(gobj, ftCo_MS_Fall, Ft_MF_None, 0.0f, 1.0f, 0.0f, NULL);
    }
}

/* +1BBC "SpecialAirLw2_Coll" (state 360): the landing above and nothing after it. */
void ftZx_SpecialAirLwFall_Coll(HSD_GObj* gobj)
{
    Fighter* fp = GET_FIGHTER(gobj);

    if (ft_80081D0C(gobj)) {
        ftZx_SpecialAirLw_Land(gobj, fp);
    }
}

/* +1CF0 "SpecialLwLand_Coll" (state 362, its only callback): the air dash is given back, the
 * common ground physics run from here (the row has no physics callback), off the floor he
 * teeters (state 245) at 0.1 times his facing, and from frame 19 he stands. */
void ftZx_SpecialLwLand_Coll(HSD_GObj* gobj)
{
    Fighter* fp = GET_FIGHTER(gobj);
    ftZx_FighterVars* fv = ftZx_Vars(fp);

    fv->specialairs_used = 0;
    fv->specialairs_count = 0;
    ft_80084F3C(gobj);
    if (!ft_80082708(gobj)) {
        fp->gr_vel = (float) ((double) fp->facing_dir * 0.1);
        Fighter_ChangeMotionState(gobj, ftCo_MS_Ottotto, Ft_MF_None, 0.0f, 1.0f, 0.0f, NULL);
    }
    if (!(fp->cur_anim_frame < 19.0f)) {
        ft_8008A2BC(gobj);
    }
}
