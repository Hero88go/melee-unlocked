/* ACE's Lucina: the aerial Dancing Blade (side special) callbacks that are her own. Every other
 * callback of her state table is Marth's retail function (lucina.c). */
#include "lucina.h"

#include <melee/ft/fighter.h>
#include <melee/ft/ft_081B.h>
#include <melee/ft/ftcommon.h>
#include <melee/ft/inlines.h>
#include <melee/ft/kinds/ftMars/ftmarsspecials.h>

/* +0954 "SpecialAirS_PhysCB": physics of the aerial first swing (state 358), in place of
 * ftMs_SpecialAirS1_Phys. Marth's falls with his attribute gravity and slows down sideways every
 * frame. Hers does nothing until the script sets command variable 0; then the first swing of an
 * airtime lifts her once (at most 1.0 upward), and the later frames fall: softly (0.3) after
 * frame 10 of a first use, at 1.9 times her common gravity and terminal velocity from the second
 * use on. No sideways slowdown. */
void ftLu_SpecialAirS1_Phys(HSD_GObj* gobj)
{
    Fighter* fp = GET_FIGHTER(gobj);
    ftLu_FighterVars* fv = ftLu_Vars(fp);
    float frame = fp->cur_anim_frame;

    if (frame == 1.0f) {
        fv->specialairs_count++;
    }
    if (fp->cmd_vars[0] != 1) {
        return;
    }
    if (fv->specialairs_lifted == 0) {
        fp->self_vel.y += 1.0f;
        fv->specialairs_lifted = 1;
        if (fp->self_vel.y > 1.0f) {
            fp->self_vel.y = 1.0f;
        }
        return;
    }
    if (fv->specialairs_count > 1) {
        /* +09E4: the products are taken in double and rounded to single (fmul, frsp) */
        ftCommon_Fall(fp, (float) ((double) fp->co_attrs.gravity * 1.9),
                      (float) ((double) fp->co_attrs.terminal_velocity * 1.9));
    } else if (frame > 10.0f) {
        ftCommon_Fall(fp, 0.3f, 0.3f);
    }
}

/* +0BA8 "SpecialSLand": an aerial swing reached the floor. The grounded state of the same swing
 * is nine ids lower (358 to 366 against 349 to 357); it goes on at the current frame with no
 * flags kept, where Marth's ftMs_SpecialS_80137748 and its siblings keep the hitboxes, the script
 * and the sword trail (ftCommon_AirToGroundStateChange with transition flags). */
void ftLu_SpecialS_Land(HSD_GObj* gobj)
{
    Fighter* fp = GET_FIGHTER(gobj);
    ftLu_FighterVars* fv = ftLu_Vars(fp);

    Fighter_ChangeMotionState(gobj, (FtMotionId) (fp->motion_id - 9), Ft_MF_None,
                              fp->cur_anim_frame, 1.0f, 0.0f, NULL);
    fv->specialairs_lifted = 0;
    fv->specialairs_count = 0;
    ftCommon_8007D6A4(fp);
}

/* +0A48 "SpecialAirS_CollCB": collision of all nine aerial swings (states 358 to 366). Floor
 * check only (ft_80081D0C inside ft_80082C74); Marth's callbacks also handle the grounded half of
 * the same function and reset his own word (fp+222C), which hers never does. */
void ftLu_SpecialAirS_Coll(HSD_GObj* gobj)
{
    ft_80082C74(gobj, ftLu_SpecialS_Land);
}

/* +0A70 "SpecialAirS2_Phys": physics of the aerial second swing (states 359 and 360). No
 * vertical speed up to frame 1, Marth's ftMs_SpecialS2_Phys after it. */
void ftLu_SpecialAirS2_Phys(HSD_GObj* gobj)
{
    Fighter* fp = GET_FIGHTER(gobj);

    if (fp->cur_anim_frame <= 1.0f) {
        fp->self_vel.y = 0.0f;
    } else {
        ftMs_SpecialS2_Phys(gobj);
    }
}
