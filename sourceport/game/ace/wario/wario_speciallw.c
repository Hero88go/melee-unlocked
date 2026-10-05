/* ACE's Wario: the down special (states 350 to 353), his own code.
 *
 *   350, 351  the start, on the ground and in the air; he cannot sink (a negative y speed is
 *             cleared every frame)
 *   352       the dive: y speed set to attribute +48 (-5.2) every frame, particle generator 15 of
 *             his effect file spawned every frame, until the floor
 *   353       the landing
 */
#include "wario.h"

#include <melee/ef/efasync.h>
#include <melee/ef/eflib.h>
#include <melee/ft/fighter.h>
#include <melee/ft/ft_081B.h>
#include <melee/ft/ft_084E.h>
#include <melee/ft/ft_0892.h>
#include <melee/ft/ftanim.h>
#include <melee/ft/ftcliffcommon.h>
#include <melee/ft/ftcommon.h>
#include <melee/ft/inlines.h>
#include <melee/ft/kinds/ftCommon/ftCo_Fall.h>
#include <melee/ft/types.h>

/* +04E4 (slot 10, speciallw) and +0548 (slot 11, specialairlw), with the state. */
static void ftWr_SpecialLw_EnterState(HSD_GObj* gobj, FtMotionId msid)
{
    Fighter* fp = GET_FIGHTER(gobj);

    fp->cmd_vars[0] = 0;
    fp->self_vel.x = 0.0f;
    fp->self_vel.y = 0.0f;
    Fighter_ChangeMotionState(gobj, msid, Ft_MF_None, 0.0f, 1.0f, 0.0f, NULL);
    ftAnim_8006EBA4(gobj);
}

/* +04E4 */
void ftWr_SpecialLw_Enter(HSD_GObj* gobj)
{
    ftWr_SpecialLw_EnterState(gobj, ftWr_MS_SpecialLw);
}

/* +0548 */
void ftWr_SpecialAirLw_Enter(HSD_GObj* gobj)
{
    ftWr_SpecialLw_EnterState(gobj, ftWr_MS_SpecialAirLw);
}

/* +1BE4: state 350, animation. The dive follows, blended over one frame (+1F7C in f3). */
void ftWr_SpecialLw_Anim(HSD_GObj* gobj)
{
    if (!ftAnim_IsFramesRemaining(gobj)) {
        Fighter_ChangeMotionState(gobj, ftWr_MS_SpecialLwFall, Ft_MF_None, 0.0f, 1.0f, 1.0f,
                                  NULL);
    }
}

/* +1C50: state 350, physics. */
void ftWr_SpecialLw_Phys(HSD_GObj* gobj)
{
    Fighter* fp = GET_FIGHTER(gobj);

    ft_80085134(gobj);
    if (fp->self_vel.y < 0.0f) {
        fp->self_vel.y = 0.0f;
    }
}

/* +1C94: state 350, collision. Once the script raises cmd_vars[0] he is made airborne every
 * frame; a true result of the ground and ledge test goes to the ledge catch routine (as the
 * file has it). */
void ftWr_SpecialLw_Coll(HSD_GObj* gobj)
{
    Fighter* fp = GET_FIGHTER(gobj);

    if (fp->cmd_vars[0] != 0) {
        ftCommon_8007D5D4(fp);
    }
    if (ft_CheckGroundAndLedge(gobj, (int) fp->facing_dir)) {
        ftCliffCommon_80081370(gobj);
    }
}

/* +1D40: state 351, animation: +1BE4 again. */
void ftWr_SpecialAirLw_Anim(HSD_GObj* gobj)
{
    ftWr_SpecialLw_Anim(gobj);
}

/* +1DAC: state 351, physics: only the clamp, no physics routine. */
void ftWr_SpecialAirLw_Phys(HSD_GObj* gobj)
{
    Fighter* fp = GET_FIGHTER(gobj);

    if (fp->self_vel.y < 0.0f) {
        fp->self_vel.y = 0.0f;
    }
}

/* +1DCC: state 351, collision. */
void ftWr_SpecialAirLw_Coll(HSD_GObj* gobj)
{
    Fighter* fp = GET_FIGHTER(gobj);

    ft_CheckGroundAndLedge(gobj, (int) fp->facing_dir);
    if (fp->cmd_vars[0] != 0 && ftCliffCommon_80081298(gobj)) {
        ftCliffCommon_80081370(gobj);
    }
}

/* +1E50: state 352, animation: every frame the two removal callbacks and the particles
 * (spawn kind 1, id 0x177F, the joint of fp->parts[1]: the word at +0x10 of the part table). */
void ftWr_SpecialLwFall_Anim(HSD_GObj* gobj)
{
    Fighter* fp = GET_FIGHTER(gobj);

    fp->death2_cb = efLib_DestroyAll;
    fp->take_dmg_cb = efLib_DestroyAll;
    efAsync_Spawn(gobj, &fp->x60C, 1, FTWR_EFFECT_DIVE, fp->parts[1].joint);
}

/* +1E80: state 352, physics. */
void ftWr_SpecialLwFall_Phys(HSD_GObj* gobj)
{
    Fighter* fp = GET_FIGHTER(gobj);

    fp->self_vel.y = ftWr_Attrs(fp)->speciallw_fall_vel_y;
}

/* +213C: the dive reaches the floor: all speed removed, grounded, state 353 blended over one
 * frame. */
static void ftWr_SpecialLwFall_Landed(HSD_GObj* gobj)
{
    Fighter* fp = GET_FIGHTER(gobj);

    ftCommon_8007E2FC(gobj);
    ftCommon_8007D7FC(fp);
    Fighter_ChangeMotionState(gobj, ftWr_MS_SpecialLwLanding, Ft_MF_None, 0.0f, 1.0f, 1.0f,
                              NULL);
}

/* +1E94: state 352, collision. */
void ftWr_SpecialLwFall_Coll(HSD_GObj* gobj)
{
    ft_800831CC(gobj, NULL, ftWr_SpecialLwFall_Landed);
}

/* +1EA4: state 353, animation. */
void ftWr_SpecialLwLanding_Anim(HSD_GObj* gobj)
{
    if (!ftAnim_IsFramesRemaining(gobj)) {
        ft_8008A2BC(gobj);
    }
}

/* +1EF0: state 353, the physics slot of the table: the floor test, and the fall when its result
 * is not positive. */
void ftWr_SpecialLwLanding_Phys(HSD_GObj* gobj)
{
    if ((int) ft_80082708(gobj) <= 0) {
        ftCo_Fall_Enter(gobj);
    }
}
