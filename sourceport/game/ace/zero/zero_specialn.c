/* ACE's Zero: the neutral special, the buster (states 344 and 347; +0408 to +0B4C, +1EF8 and
 * +1FC4 of the "ftFunction" block of PlZx.dat).
 *
 * The state is entered with no callback set. Its interrupt callback watches frames 12 to 39:
 *   B not held, up to frame 37     the state starts again at frame 39 with the plain shot's
 *                                  accessory callback (article 0)
 *   B held at frames 37 and 38     the animation rate is set to 0 and the state starts again at
 *                                  frame 37 with the charged shot's callback (article 1), every
 *                                  frame while B stays held: the pose is held
 * The shot itself leaves when the animation script raises the throw flag. */
#include "zero.h"

#include <dolphin/os.h>

#include <melee/ft/fighter.h>
#include <melee/ft/ft_081B.h>
#include <melee/ft/ft_084E.h>
#include <melee/ft/ft_0892.h>
#include <melee/ft/ftanim.h>
#include <melee/ft/inlines.h>
#include <melee/ft/kinds/ftCommon/ftCo_Fall.h>
#include <melee/ft/types.h>
#include <melee/lb/lb_00B0.h>
#include <sysdolphin/baselib/controller.h>

/* +0408 "SpecialN" (slot 4): the state change and nothing else (no ftAnim_8006EBA4, no
 * callback, no flag cleared). */
void ftZx_SpecialN_Enter(HSD_GObj* gobj)
{
    Fighter_ChangeMotionState(gobj, ftZx_MS_SpecialN, Ft_MF_None, 0.0f, 1.0f, 0.0f, NULL);
}

/* +0448 "SpecialAirN" (slot 5) */
void ftZx_SpecialAirN_Enter(HSD_GObj* gobj)
{
    Fighter_ChangeMotionState(gobj, ftZx_MS_SpecialAirN, Ft_MF_None, 0.0f, 1.0f, 0.0f, NULL);
}

/* +1FC4 "SpecialN_Accessory4_Callback" and +1EF8 "SpecialN2_Accessory4_Callback": the same
 * routine but for the article. On the throw flag the shot leaves from bone 64, offset and speed
 * from his attributes, straight ahead. */
static void ftZx_SpecialN_ItemSpawn(HSD_GObj* gobj, bool charged)
{
    Fighter* fp = GET_FIGHTER(gobj);
    ftZx_DatAttrs* sa;
    Vec3 pos;
    Vec3 vel;
    float facing;

    if (!fp->throw_flags_b0) {
        return;
    }
    sa = ftZx_Attrs(fp);
    fp->throw_flags_b0 = false;

    lb_8000B1CC(fp->parts[FTZX_SPECIALN_BONE].joint, NULL, &pos);
    facing = fp->facing_dir;
    pos.x += facing * sa->specialn_item_offset_x;
    pos.y += sa->specialn_item_offset_y;
    pos.z = 0.0f;
    vel.x = sa->specialn_item_vel_x * facing;
    vel.y = 0.0f;
    vel.z = 0.0f;
    if (charged) {
        itZx_ChargedShot_Spawn(gobj, &pos, &vel, facing);
    } else {
        itZx_Shot_Spawn(gobj, &pos, &vel, facing);
    }
}

/* +1FC4 "SpecialN_Accessory4_Callback" */
static void ftZx_SpecialN_ShotSpawn(HSD_GObj* gobj)
{
    ftZx_SpecialN_ItemSpawn(gobj, false);
}

/* +1EF8 "SpecialN2_Accessory4_Callback" */
static void ftZx_SpecialN_ChargedShotSpawn(HSD_GObj* gobj)
{
    ftZx_SpecialN_ItemSpawn(gobj, true);
}

/* +0670 "SpecialN_IASACB" and +088C "SpecialAirN_IASACB": the same routine with the state. */
static void ftZx_SpecialN_Charge(HSD_GObj* gobj, FtMotionId msid)
{
    Fighter* fp = GET_FIGHTER(gobj);
    float frame = fp->cur_anim_frame;

    if (frame < 12.0f || frame > 39.0f) {
        return;
    }
    if (fp->input.held_buttons[0] & HSD_PAD_B) {
        if (frame < 37.0f || frame > 38.0f) {
            return;
        }
        ftAnim_SetAnimRate(gobj, 0.0f);
        fp->throw_flags = 0;
        fp->cmd_vars[0] = 0;
        ftAnim_8006EBA4(gobj);
        Fighter_ChangeMotionState(gobj, msid, Ft_MF_None, 37.0f, 1.0f, 0.0f, NULL);
        fp->accessory4_cb = ftZx_SpecialN_ChargedShotSpawn;
    } else if (frame <= 37.0f) {
        /* +0760 */
        fp->throw_flags = 0;
        fp->cmd_vars[0] = 0;
        Fighter_ChangeMotionState(gobj, msid, Ft_MF_None, 39.0f, 1.0f, 0.0f, NULL);
        ftAnim_8006EBA4(gobj);
        fp->accessory4_cb = ftZx_SpecialN_ShotSpawn;
    }
}

/* +0630 "SpecialN_AnimCB" */
void ftZx_SpecialN_Anim(HSD_GObj* gobj)
{
    if (!ftAnim_IsFramesRemaining(gobj)) {
        ft_8008A2BC(gobj);
    }
}

/* +0670 "SpecialN_IASACB" */
void ftZx_SpecialN_IASA(HSD_GObj* gobj)
{
    ftZx_SpecialN_Charge(gobj, ftZx_MS_SpecialN);
}

/* +07EC "SpecialN_PhysCB" */
void ftZx_SpecialN_Phys(HSD_GObj* gobj)
{
    ft_80084F3C(gobj);
}

/* +080C "SpecialN_CollCB": off the floor he falls; the move is not carried into the air. */
void ftZx_SpecialN_Coll(HSD_GObj* gobj)
{
    if (!ft_800827A0(gobj)) {
        ftCo_Fall_Enter(gobj);
    }
}

/* +084C "SpecialAirN_AnimCB" */
void ftZx_SpecialAirN_Anim(HSD_GObj* gobj)
{
    if (!ftAnim_IsFramesRemaining(gobj)) {
        ftCo_Fall_Enter(gobj);
    }
}

/* +088C "SpecialAirN_IASACB" */
void ftZx_SpecialAirN_IASA(HSD_GObj* gobj)
{
    ftZx_SpecialN_Charge(gobj, ftZx_MS_SpecialAirN);
}

/* +0A08 "SpecialAirN_PhysCB" */
void ftZx_SpecialAirN_Phys(HSD_GObj* gobj)
{
    ft_80084EEC(gobj);
}

/* +0A28 "SpecialAirN_CollCB": landing by the frame.
 *   up to 37     the ground state at the same frame, plain shot callback
 *   38 and 39    the ground state at the same frame, charged shot callback
 *   41 and on    the callback dropped and the common landing (ft_80082B1C)
 * A frame between 37 and 38, or between 39 and 41, does nothing. The two state changes do not
 * call ftAnim_8006EBA4. */
void ftZx_SpecialAirN_Coll(HSD_GObj* gobj)
{
    Fighter* fp = GET_FIGHTER(gobj);
    float frame;

    if (!ft_80081D0C(gobj)) {
        return;
    }
    frame = fp->cur_anim_frame;
    if (!(frame > 37.0f)) {
        /* +0ACC */
        fp->throw_flags = 0;
        fp->cmd_vars[0] = 0;
        Fighter_ChangeMotionState(gobj, ftZx_MS_SpecialN, Ft_MF_None, frame, 1.0f, 0.0f,
                                  NULL);
        fp->accessory4_cb = ftZx_SpecialN_ShotSpawn;
        return;
    }
    if (frame < 38.0f) {
        return;
    }
    if (!(frame > 39.0f)) {
        /* +0B0C */
        fp->throw_flags = 0;
        fp->cmd_vars[0] = 0;
        Fighter_ChangeMotionState(gobj, ftZx_MS_SpecialN, Ft_MF_None, frame, 1.0f, 0.0f,
                                  NULL);
        fp->accessory4_cb = ftZx_SpecialN_ChargedShotSpawn;
        return;
    }
    if (frame < 41.0f) {
        return;
    }
    fp->accessory4_cb = NULL;
    ft_80082B1C(gobj);
}
