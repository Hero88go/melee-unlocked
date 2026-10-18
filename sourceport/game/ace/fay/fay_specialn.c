/* ACE's Fay: the neutral special (341 to 344, the swaps 362 and 363).
 *
 * Two modes, kept in fp+22F8 and shown by the HUD object. Each entry goes to the mode's state;
 * in it the script's command variable 3 asks once for the held article (mode 0: article 1,
 * mode 1: article 2, in state 0 on the ground and in the air but for mode 0 in the air, state 1)
 * and the throw flag fires a shot (mode 0: article 0, 4.5 a frame for 20 frames; mode 1:
 * article 3, 9.0 a frame for 16 frames). A pressed while the script allows it (command variable
 * 1) swaps the mode and goes to the swap state (362 on the ground, 363 in the air).
 *
 * The two mode tests are not the same: mode 0 looks at A while command variable 1 is NOT 1,
 * mode 1 while it IS 1 (and clears it either way first). Kept. */
#include "fay.h"

#include <melee/ft/fighter.h>
#include <melee/ft/ft_081B.h>
#include <melee/ft/ft_0892.h>
#include <melee/ft/ftanim.h>
#include <melee/ft/ftcommon.h>
#include <melee/ft/inlines.h>
#include <melee/ft/kinds/ftCommon/ftCo_Fall.h>
#include <melee/ft/types.h>
#include <sysdolphin/baselib/controller.h>

/* +04D8 and +05E4 (slots 4 and 5): command variables 1, 2, 3 and the throw flag cleared, then
 * the mode's state (none when fp+22F8 is neither 0 nor 1), the take damage callback +32DC, and
 * ftAnim_8006EBA4. Command variable 0 is left as it is. */
static void ftFy_SpecialN_EnterMode(HSD_GObj* gobj, FtMotionId mode0, FtMotionId mode1)
{
    Fighter* fp = GET_FIGHTER(gobj);
    s32 mode = ftFy_FV(fp)->n_mode;

    fp->cmd_vars[1] = 0;
    fp->cmd_vars[2] = 0;
    fp->throw_flags_b0 = false;
    fp->cmd_vars[3] = 0;
    if (mode == 0) {
        Fighter_ChangeMotionState(gobj, mode0, Ft_MF_None, 0.0f, 1.0f, 0.0f, NULL);
    } else if (mode == 1) {
        Fighter_ChangeMotionState(gobj, mode1, Ft_MF_None, 0.0f, 1.0f, 0.0f, NULL);
    }
    fp->take_dmg_cb = ftFy_TakeDamage_ClearShotFlag;
    ftAnim_8006EBA4(gobj);
}

/* +04D8 (slot 4) */
void ftFy_SpecialN_Enter(HSD_GObj* gobj)
{
    ftFy_SpecialN_EnterMode(gobj, ftFy_MS_SpecialN, ftFy_MS_SpecialN2);
}

/* +05E4 (slot 5) */
void ftFy_SpecialAirN_Enter(HSD_GObj* gobj)
{
    ftFy_SpecialN_EnterMode(gobj, ftFy_MS_SpecialAirN, ftFy_MS_SpecialAirN2);
}

/* The held article, once per state: command variable 3 set to 1, then +39B8 at her position. */
static void ftFy_SpecialN_HeldOnce(HSD_GObj* gobj, Fighter* fp, Vec3* pos, int article,
                                   s32 msid)
{
    if (fp->cmd_vars[3] != 1) {
        fp->cmd_vars[3] = 1;
        itFy_Held_Spawn(gobj, pos, mu_ak_article_kind(gobj, article), msid, fp->facing_dir);
    }
}

/* +0D8C: 341 (mode 0, ground).
 *   held article 1, state 0;
 *   while command variable 1 is not 1: it is cleared, and A held swaps to mode 1 (fp+22F8 = 1)
 *     in 362 (the change has an animation blend of 1.0);
 *   the throw flag: a shot of article 0 at x + 11 * facing (fused), y + 6.2 (in double), the
 *     z of her position, speed 4.5 * facing, 20 frames;
 *   the animation's end: the common wait (ft_8008A2BC). */
void ftFy_SpecialN_Anim(HSD_GObj* gobj)
{
    Fighter* fp = GET_FIGHTER(gobj);
    Vec3 pos = fp->cur_pos;
    float x = pos.x;
    float y = pos.y;

    ftFy_SpecialN_HeldOnce(gobj, fp, &pos, ftFy_Article_Gun, 0);
    if (fp->cmd_vars[1] != 1) {
        HSD_Pad held = fp->input.held_buttons[0];
        fp->cmd_vars[1] = 0;
        if (held & HSD_PAD_A) {
            /* +0F0C */
            ftFy_FV(fp)->n_mode = 1;
            Fighter_ChangeMotionState(gobj, ftFy_MS_SpecialNSwap, Ft_MF_None, 0.0f, 1.0f, 1.0f,
                                      NULL);
            return;
        }
    }
    if (fp->throw_flags_b0) {
        /* +0E7C */
        Vec3 vel;
        float facing = fp->facing_dir;
        fp->throw_flags_b0 = false;
        vel.x = facing * 4.5f;
        vel.y = 0.0f;
        vel.z = 0.0f;
        pos.x = MU_FMADDS(facing, 11.0f, x);
        pos.y = (float) ((double) y + 6.2);
        itFy_Shot_Spawn(gobj, &pos, &vel, mu_ak_article_kind(gobj, ftFy_Article_Shot), 20,
                        fp->facing_dir);
    }
    if (!ftAnim_IsFramesRemaining(gobj)) {
        ft_8008A2BC(gobj);
    }
}

/* +0F50: 342 (mode 1, ground).
 *   held article 2, state 0;
 *   while command variable 1 IS 1: it is cleared, and A held swaps to mode 0 (fp+22F8 = 0) in
 *     362 (blend 1.0);
 *   the throw flag: a shot of article 3 at x + 17.5 * facing (fused, in double), y + 7.0, speed
 *     9.0 * facing, 16 frames;
 *   the animation's end: the common wait. */
void ftFy_SpecialN2_Anim(HSD_GObj* gobj)
{
    Fighter* fp = GET_FIGHTER(gobj);
    Vec3 pos = fp->cur_pos;
    float x = pos.x;
    float y = pos.y;

    ftFy_SpecialN_HeldOnce(gobj, fp, &pos, ftFy_Article_Gun2, 0);
    if (fp->cmd_vars[1] == 1) {
        HSD_Pad held = fp->input.held_buttons[0];
        fp->cmd_vars[1] = 0;
        if (held & HSD_PAD_A) {
            /* +1024 */
            ftFy_FV(fp)->n_mode = 0;
            Fighter_ChangeMotionState(gobj, ftFy_MS_SpecialNSwap, Ft_MF_None, 0.0f, 1.0f, 1.0f,
                                      NULL);
            return;
        }
    }
    if (fp->throw_flags_b0) {
        /* +1080 */
        Vec3 vel;
        float facing = fp->facing_dir;
        fp->throw_flags_b0 = false;
        vel.x = facing * 9.0f;
        vel.y = 0.0f;
        vel.z = 0.0f;
        pos.x = MU_FMADDS(facing, 17.5f, x);
        pos.y = y + 7.0f;
        itFy_Shot_Spawn(gobj, &pos, &vel, mu_ak_article_kind(gobj, ftFy_Article_Shot2), 16,
                        fp->facing_dir);
    }
    if (!ftAnim_IsFramesRemaining(gobj)) {
        ft_8008A2BC(gobj);
    }
}

/* +1114: 343 (mode 0, air). As 341 with the held article in its state 1, the shot at
 * x + 6 * facing (fused), y + 10.0, the swap into 363, and the common fall at the end. */
void ftFy_SpecialAirN_Anim(HSD_GObj* gobj)
{
    Fighter* fp = GET_FIGHTER(gobj);
    Vec3 pos = fp->cur_pos;
    float x = pos.x;
    float y = pos.y;

    ftFy_SpecialN_HeldOnce(gobj, fp, &pos, ftFy_Article_Gun, 1);
    if (fp->cmd_vars[1] != 1) {
        HSD_Pad held = fp->input.held_buttons[0];
        fp->cmd_vars[1] = 0;
        if (held & HSD_PAD_A) {
            /* +1290 */
            ftFy_FV(fp)->n_mode = 1;
            Fighter_ChangeMotionState(gobj, ftFy_MS_SpecialAirNSwap, Ft_MF_None, 0.0f, 1.0f,
                                      1.0f, NULL);
            return;
        }
    }
    if (fp->throw_flags_b0) {
        /* +1204 */
        Vec3 vel;
        float facing = fp->facing_dir;
        fp->throw_flags_b0 = false;
        vel.x = facing * 4.5f;
        vel.y = 0.0f;
        vel.z = 0.0f;
        pos.x = MU_FMADDS(facing, 6.0f, x);
        pos.y = y + 10.0f;
        itFy_Shot_Spawn(gobj, &pos, &vel, mu_ak_article_kind(gobj, ftFy_Article_Shot), 20,
                        fp->facing_dir);
    }
    if (!ftAnim_IsFramesRemaining(gobj)) {
        ftCo_Fall_Enter(gobj);
    }
}

/* +12D4: 344 (mode 1, air). As 342 with the shot at y + 10.0, the swap into 363 and the common
 * fall at the end. */
void ftFy_SpecialAirN2_Anim(HSD_GObj* gobj)
{
    Fighter* fp = GET_FIGHTER(gobj);
    Vec3 pos = fp->cur_pos;
    float x = pos.x;
    float y = pos.y;

    ftFy_SpecialN_HeldOnce(gobj, fp, &pos, ftFy_Article_Gun2, 0);
    if (fp->cmd_vars[1] == 1) {
        HSD_Pad held = fp->input.held_buttons[0];
        fp->cmd_vars[1] = 0;
        if (held & HSD_PAD_A) {
            /* +13A8 */
            ftFy_FV(fp)->n_mode = 0;
            Fighter_ChangeMotionState(gobj, ftFy_MS_SpecialAirNSwap, Ft_MF_None, 0.0f, 1.0f,
                                      1.0f, NULL);
            return;
        }
    }
    if (fp->throw_flags_b0) {
        /* +1404 */
        Vec3 vel;
        float facing = fp->facing_dir;
        fp->throw_flags_b0 = false;
        vel.x = facing * 9.0f;
        vel.y = 0.0f;
        vel.z = 0.0f;
        pos.x = MU_FMADDS(facing, 17.5f, x);
        pos.y = y + 10.0f;
        itFy_Shot_Spawn(gobj, &pos, &vel, mu_ak_article_kind(gobj, ftFy_Article_Shot2), 16,
                        fp->facing_dir);
    }
    if (!ftAnim_IsFramesRemaining(gobj)) {
        ftCo_Fall_Enter(gobj);
    }
}

/* +1498: 344's physics. The common aerial physics (ft_80084DB0), then by command variable 2:
 * 1 (set by the script) pushes her back by one facing unit and goes to 2; otherwise after frame
 * 10 and while it is not 2, the horizontal speed is damped by 0.8 (in double). */
void ftFy_SpecialAirN2_Phys(HSD_GObj* gobj)
{
    Fighter* fp = GET_FIGHTER(gobj);

    ft_80084DB0(gobj);
    if (fp->cmd_vars[2] == 1) {
        /* +1504 */
        fp->self_vel.x = fp->self_vel.x - fp->facing_dir;
        fp->cmd_vars[2] = 2;
        return;
    }
    if (fp->cur_anim_frame > 10.0f && fp->cmd_vars[2] != 2) {
        fp->self_vel.x = (float) ((double) fp->self_vel.x * 0.8);
    }
}

/* +3C44: the landing of 344 and 363: back into 342 or 362 at the same frame (flags 0x5000). */
static void ftFy_SpecialAirN_Land(HSD_GObj* gobj)
{
    Fighter* fp = GET_FIGHTER(gobj);
    FtMotionId msid = fp->motion_id;

    ftCommon_8007D7FC(fp);
    if (msid == ftFy_MS_SpecialAirN2) {
        Fighter_ChangeMotionState(gobj, ftFy_MS_SpecialN2, FTFY_MF_SWAP, fp->cur_anim_frame,
                                  1.0f, 0.0f, NULL);
    } else if (msid == ftFy_MS_SpecialAirNSwap) {
        Fighter_ChangeMotionState(gobj, ftFy_MS_SpecialNSwap, FTFY_MF_SWAP, fp->cur_anim_frame,
                                  1.0f, 0.0f, NULL);
    }
}

/* +1530: 344's and 363's collision: the common air collision (ft_80081D0C), and on landing
 * +3C44. (343's collision is Fox's, the retail ftFx_SpecialAirNStart_Coll.) */
void ftFy_SpecialAirN2_Coll(HSD_GObj* gobj)
{
    if (ft_80081D0C(gobj) != false) {
        ftFy_SpecialAirN_Land(gobj);
    }
}

/* +2E64: 362 (the swap, ground): the common wait at the animation's end. */
void ftFy_SpecialNSwap_Anim(HSD_GObj* gobj)
{
    if (!ftAnim_IsFramesRemaining(gobj)) {
        ft_8008A2BC(gobj);
    }
}

/* +2EB0: 363 (the swap, air): the common fall at the animation's end. */
void ftFy_SpecialAirNSwap_Anim(HSD_GObj* gobj)
{
    if (!ftAnim_IsFramesRemaining(gobj)) {
        ftCo_Fall_Enter(gobj);
    }
}
