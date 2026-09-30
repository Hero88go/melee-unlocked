/* Akaneia's Tails, native: neutral special (energy shot).
 *
 * One projectile at a time: the move does nothing while the last shot is alive. The shot itself
 * leaves on the frame the animation script sets cmd_vars[0] (the accessory callback watches for
 * it); cmd_vars[1], also set by the script, turns an air landing into special landing lag.
 * The projectile (article 0) lives in itTs_shot.c. */
#include "ftTs.h"

#include <melee/ft/fighter.h>
#include <melee/ft/ft_081B.h>
#include <melee/ft/ft_084E.h>
#include <melee/ft/ft_0892.h>
#include <melee/ft/ftanim.h>
#include <melee/ft/ftcommon.h>
#include <melee/ft/inlines.h>
#include <melee/ft/kinds/ftCommon/ftCo_Fall.h>
#include <melee/ft/kinds/ftCommon/ftCo_Landing.h>
#include <melee/ft/types.h>

/* code+0x5F04 SpecialN_Accessory: fires when the script raises cmd_vars[0]. */
static void ftTs_SpecialN_Accessory(Fighter_GObj* gobj)
{
    Fighter* fp = GET_FIGHTER(gobj);

    if (fp->cmd_vars[0] != 1) {
        return;
    }
    fp->cmd_vars[0] = 0;
    ftTs_SpecialN_SpawnProjectile(gobj);
    if (fp->ground_or_air == GA_Air) {
        fp->self_vel.y = ftTs_Attrs(fp)->airn_shot_vel_y;
    }
}

/* code+0x5478 SpecialN_OnEnter */
static void ftTs_SpecialN_OnEnter(Fighter_GObj* gobj)
{
    Fighter* fp = GET_FIGHTER(gobj);

    fp->cmd_vars[0] = 0;
    fp->cmd_vars[1] = 0;
    fp->self_vel.y *= ftTs_Attrs(fp)->n_enter_vel_y_mul;
    fp->accessory4_cb = ftTs_SpecialN_Accessory;
}

/* code+0x2F1C SpecialN_Enter */
void ftTs_SpecialN_Enter(Fighter_GObj* gobj)
{
    if (ftTs_Vars(GET_FIGHTER(gobj))->shot_gobj != NULL) {
        return;
    }
    Fighter_ChangeMotionState(gobj, ftTs_MS_SpecialN, Ft_MF_None, 0.0f, 1.0f, 0.0f, NULL);
    ftAnim_8006EBA4(gobj);
    ftTs_SpecialN_OnEnter(gobj);
}

/* code+0x2F88 SpecialAirN_Enter */
void ftTs_SpecialAirN_Enter(Fighter_GObj* gobj)
{
    if (ftTs_Vars(GET_FIGHTER(gobj))->shot_gobj != NULL) {
        return;
    }
    Fighter_ChangeMotionState(gobj, ftTs_MS_SpecialAirN, Ft_MF_None, 0.0f, 1.0f, 0.0f, NULL);
    ftAnim_8006EBA4(gobj);
    ftTs_SpecialN_OnEnter(gobj);
}

/* code+0x40B4 SpecialAirN_Trans: ground to air, keeping the frame. */
static void ftTs_SpecialAirN_Trans(Fighter_GObj* gobj)
{
    Fighter* fp = GET_FIGHTER(gobj);

    ftCommon_8007D5D4(fp);
    Fighter_ChangeMotionState(gobj, ftTs_MS_SpecialAirN, ftTs_MF_TransN, fp->cur_anim_frame,
                              1.0f, 0.0f, NULL);
    fp->accessory4_cb = ftTs_SpecialN_Accessory;
}

/* code+0x4128 SpecialN_Trans: air to ground, keeping the frame. */
static void ftTs_SpecialN_Trans(Fighter_GObj* gobj)
{
    Fighter* fp = GET_FIGHTER(gobj);

    ftCommon_8007D7FC(fp);
    Fighter_ChangeMotionState(gobj, ftTs_MS_SpecialN, ftTs_MF_TransN, fp->cur_anim_frame, 1.0f,
                              0.0f, NULL);
    fp->accessory4_cb = ftTs_SpecialN_Accessory;
}

/* code+0x12CC */
void ftTs_SpecialN_Anim(Fighter_GObj* gobj)
{
    if (!ftAnim_IsFramesRemaining(gobj)) {
        ft_8008A2BC(gobj);
    }
}

/* code+0x130C */
void ftTs_SpecialN_IASA(Fighter_GObj* gobj) {}

/* code+0x1310 */
void ftTs_SpecialN_Phys(Fighter_GObj* gobj)
{
    ft_80084F3C(gobj);
}

/* code+0x1330 */
void ftTs_SpecialN_Coll(Fighter_GObj* gobj)
{
    if (!ft_80082708(gobj)) {
        ftTs_SpecialAirN_Trans(gobj);
    }
}

/* code+0x1370 */
void ftTs_SpecialAirN_Anim(Fighter_GObj* gobj)
{
    if (!ftAnim_IsFramesRemaining(gobj)) {
        ftCo_Fall_Enter(gobj);
    }
}

/* code+0x13B0 */
void ftTs_SpecialAirN_IASA(Fighter_GObj* gobj) {}

/* code+0x13B4 */
void ftTs_SpecialAirN_Phys(Fighter_GObj* gobj)
{
    Fighter* fp = GET_FIGHTER(gobj);
    ftTails_DatAttrs* da = ftTs_Attrs(fp);

    ftCommon_CalcSelfAccel_Deaccel(fp, da->airn_x_decel);
    ftCommon_Fall(fp, da->airn_gravity, da->airn_terminal_vel);
}

/* code+0x1404 */
void ftTs_SpecialAirN_Coll(Fighter_GObj* gobj)
{
    Fighter* fp = GET_FIGHTER(gobj);
    ftCollisionBox ecb = ftCollisionBox_FromDisc(&ftTs_Attrs(fp)->airn_ecb);

    if (!ft_800824A0(gobj, &ecb)) {
        return;
    }
    if (fp->cmd_vars[1] != 0) {
        ftCo_LandingFallSpecial_Enter(gobj, false, 10.0f);
    } else {
        ftTs_SpecialN_Trans(gobj);
    }
}
