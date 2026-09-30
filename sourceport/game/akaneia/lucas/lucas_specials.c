/* Akaneia's Lucas: side special, PK Fire. One state per ground/air; the command script raises
 * throw_flags_b0 on the frame the bolt (article 1, lucas_it_pkfire.c) leaves his hand, and the
 * accessory callback spawns it and nudges him back. */
#include "lucas.h"

#include <melee/ft/fighter.h>
#include <melee/ft/ft_081B.h>
#include <melee/ft/ft_084E.h>
#include <melee/ft/ft_0892.h>
#include <melee/ft/ftanim.h>
#include <melee/ft/kinds/ftCommon/ftCo_Fall.h>
#include <melee/ft/kinds/ftCommon/ftCo_Landing.h>
#include <melee/lb/lb_00B0.h>

/* code+0x5598 "SpecialS_Accessory4_Callback" */
static void ftLc_SpecialS_Accessory(HSD_GObj* gobj)
{
    Fighter* fp = GET_FIGHTER(gobj);
    ftLucasAttributes* attrs;
    Vec3 pos;
    Vec3 vel;

    if (!fp->throw_flags_b0) {
        return;
    }
    attrs = ftLc_Attrs(fp);
    fp->throw_flags_b0 = false;

    /* Recoil. */
    fp->self_vel.x = -(fp->facing_dir * 1.5f);
    fp->gr_vel = fp->self_vel.x;

    lb_8000B1CC(fp->parts[ftLc_Part_Hand].joint, NULL, &pos);
    pos.x += fp->facing_dir * attrs->x48_PKFIRE_SPAWN_X;
    pos.y += attrs->x4C_PKFIRE_SPAWN_Y;
    pos.z = 0.0f;
    vel.x = attrs->x44_PKFIRE_VEL_X * fp->facing_dir;
    vel.y = 0.0f;
    vel.z = 0.0f;
    ftLc_PKFire_Spawn(gobj, &pos, &vel, fp->facing_dir);
}

/* code+0x39E4 "SpecialS_Enter" */
static void ftLc_SpecialS_EnterShared(HSD_GObj* gobj, FtMotionId msid)
{
    Fighter* fp = GET_FIGHTER(gobj);
    fp->throw_flags = 0;
    fp->cmd_vars[0] = 0;
    Fighter_ChangeMotionState(gobj, msid, 0, 0.0f, 1.0f, 0.0f, NULL);
    ftAnim_8006EBA4(gobj);
    fp->accessory4_cb = ftLc_SpecialS_Accessory;
}

/* m-ex specials, code+0x674 */
void ftLc_SpecialS_Enter(HSD_GObj* gobj)
{
    ftLc_SpecialS_EnterShared(gobj, ftLc_MS_SpecialS);
}

/* m-ex specialairs, code+0x698 */
void ftLc_SpecialAirS_Enter(HSD_GObj* gobj)
{
    ftLc_SpecialS_EnterShared(gobj, ftLc_MS_SpecialAirS);
}

/* code+0x17F8 */
void ftLc_SpecialS_Anim(HSD_GObj* gobj)
{
    if (!ftAnim_IsFramesRemaining(gobj)) {
        ft_8008A2BC(gobj);
    }
}

/* code+0x1838 */
void ftLc_SpecialS_Phys(HSD_GObj* gobj)
{
    ft_80084F3C(gobj);
}

/* code+0x1858: walking off an edge ends the move. */
void ftLc_SpecialS_Coll(HSD_GObj* gobj)
{
    if (!ft_800827A0(gobj)) {
        ftCo_Fall_Enter(gobj);
    }
}

/* code+0x1898 */
void ftLc_SpecialAirS_Anim(HSD_GObj* gobj)
{
    if (!ftAnim_IsFramesRemaining(gobj)) {
        ftCo_Fall_Enter(gobj);
    }
}

/* code+0x18D8 */
void ftLc_SpecialAirS_Phys(HSD_GObj* gobj)
{
    ft_80084EEC(gobj);
}

/* code+0x18F8: landing ends the move with special landing lag. */
void ftLc_SpecialAirS_Coll(HSD_GObj* gobj)
{
    Fighter* fp = GET_FIGHTER(gobj);
    if (ft_80081D0C(gobj)) {
        ftCo_LandingFallSpecial_Enter(gobj, false, ftLc_Attrs(fp)->x50_PKFIRE_LANDING_LAG);
    }
}
