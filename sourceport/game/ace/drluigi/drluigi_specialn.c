/* ACE's Dr. Luigi: the neutral special (m-ex "ftFunction" of PlDl.dat, slots 4 and 5, and the
 * two state callbacks of his own).
 *
 * Every routine here is Luigi's (ft/kinds/ftLuigi/ftluigispecialn.c) with one change: the
 * accessory callback is his own, which throws the pill article in place of Luigi's fireball. The
 * animation, IASA and physics callbacks of the two states are Luigi's own functions, named in
 * the move table (drluigi.c). */
#include "drluigi.h"

#include <dolphin/os.h>

#include <melee/ft/fighter.h>
#include <melee/ft/ft_081B.h>
#include <melee/ft/ftanim.h>
#include <melee/ft/ftcommon.h>
#include <melee/ft/ftparts.h>
#include <melee/ft/inlines.h>
#include <melee/ft/kinds/ftCommon/inlines.h>
#include <melee/ft/types.h>
#include <melee/lb/lb_00B0.h>

/* code+0x334, [specialn]. Same as retail ftLg_SpecialN_Enter, except that only the first throw
 * flag is cleared (Luigi clears the whole word) and the accessory callback is the pill's. */
void ftDl_SpecialN_Enter(HSD_GObj* gobj)
{
    Fighter* fp = GET_FIGHTER(gobj);

    fp->cmd_vars[0] = 0;
    fp->throw_flags_b0 = false;
    Fighter_ChangeMotionState(gobj, ftLg_MS_SpecialN, 0, 0.0f, 1.0f, 0.0f,
                              NULL);
    ftAnim_8006EBA4(gobj);
    fp->accessory4_cb = ftDl_SpecialN_PillSpawn;
}

/* code+0x3B4, [specialairn]. Same as retail ftLg_SpecialAirN_Enter, with the same two
 * differences. */
void ftDl_SpecialAirN_Enter(HSD_GObj* gobj)
{
    Fighter* fp = GET_FIGHTER(gobj);

    fp->cmd_vars[0] = 0;
    fp->throw_flags_b0 = false;
    Fighter_ChangeMotionState(gobj, ftLg_MS_SpecialAirN, 0, 0.0f, 1.0f, 0.0f,
                              NULL);
    ftAnim_8006EBA4(gobj);
    fp->accessory4_cb = ftDl_SpecialN_PillSpawn;
}

/* code+0x4D8 (and its tail at +0x50C), the collision callback of state 341. Same as retail
 * ftLg_SpecialN_Coll: off the ground, go on in the air state at the same frame (motion flags
 * 0x5000, ftLg_MF_SpecialN_Coll) and keep the pill callback. */
void ftDl_SpecialN_Coll(HSD_GObj* gobj)
{
    Fighter* fp;

    if (ft_80082708(gobj) == false) {
        fp = GET_FIGHTER(gobj);
        ftCommon_GroundToAirStateChange(gobj, fp, ftLg_MS_SpecialAirN,
                                        ftLg_MF_SpecialN_Coll);
        fp->accessory4_cb = ftDl_SpecialN_PillSpawn;
    }
}

/* code+0x568 (and its tail at +0x59C), the collision callback of state 342. Same as retail
 * ftLg_SpecialAirN_Coll. */
void ftDl_SpecialAirN_Coll(HSD_GObj* gobj)
{
    Fighter* fp;

    if (ft_80081D0C(gobj) != false) {
        fp = GET_FIGHTER(gobj);
        ftCommon_AirToGroundStateChange(gobj, fp, ftLg_MS_SpecialN,
                                        ftLg_MF_SpecialN_Coll);
        fp->accessory4_cb = ftDl_SpecialN_PillSpawn;
    }
}

/* code+0x604, the accessory callback of both states. Retail ftLg_SpecialN_FireSpawn with the
 * pill: when the animation script raises the throw flag, the pill is created at the left hand
 * bone (FtPart_L1stNb, 23, the bone Luigi uses). Unlike Luigi's it spawns no effect, and the item
 * kind is the one m-ex gives his article 0 (MEX_GetFtItemID at 0x803D7088). */
void ftDl_SpecialN_PillSpawn(HSD_GObj* gobj)
{
    Vec3 pos;
    Fighter* fp = GET_FIGHTER(gobj);
    ItemKind kind;

    if (!fp->throw_flags_b0) {
        return;
    }
    fp->throw_flags_b0 = false;

    lb_8000B1CC(fp->parts[ftParts_GetBoneIndex(fp, FtPart_L1stNb)].joint, NULL,
                &pos);
    kind = mu_ak_article_kind(gobj, ftDl_Article_Pill);
    if ((int) kind < 0) {
        /* Not on the console: there the id always exists. Without it nothing can be created. */
        OSReport("[ak] Dr. Luigi: no item kind for the pill article (fighter kind %d)\n",
                 (int) fp->kind);
        return;
    }
    itDl_Pill_Spawn(gobj, &pos, kind, fp->facing_dir);
}
