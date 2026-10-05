/* ACE's Metal Mario: the neutral special (m-ex "ftFunction" of PlMM.dat, slots 4 and 5, and the
 * state callbacks of his own).
 *
 * Every routine here is Mario's (ft/kinds/ftMario/ftmariospecialn.c) with one change: the
 * accessory callback is his own, which throws his article 0 in place of Mario's fireball and
 * spawns his own effect. The animation, IASA and physics callbacks of the two states are
 * Mario's own functions, named in the move table (metalmario.c): the file's copies at
 * code+0x4B0, +0x4FC, +0x510, +0x5A4, +0x5F0 and +0x604 make the same calls. */
#include "metalmario.h"

#include <dolphin/os.h>

#include <melee/ef/efsync.h>
#include <melee/ft/fighter.h>
#include <melee/ft/ft_081B.h>
#include <melee/ft/ftanim.h>
#include <melee/ft/ftcommon.h>
#include <melee/ft/ftparts.h>
#include <melee/ft/inlines.h>
#include <melee/ft/kinds/ftCommon/inlines.h>
#include <melee/ft/types.h>
#include <melee/lb/lb_00B0.h>

/* The motion flags of the ground and air swap (0x5000 at code+0x574 and +0x668): the ones
 * Mario's file keeps to itself as ftMr_MF_SpecialN_Coll. */
static MotionFlags const ftMM_MF_SpecialN_Coll =
    Ft_MF_SkipColAnim | Ft_MF_UpdateCmd;

/* The effect of the throw: m-ex id 5000, model effect 0 of the effect file MxDt gives him
 * (EfMrData.dat, Mario's file as changed on ACE), where Mario spawns his 1146. */
#define ftMM_Gfx_SpecialN 5000

/* code+0x224, [specialn]. Same as retail ftMr_SpecialN_Enter, except that only the first throw
 * flag is cleared (Mario clears the whole word) and the accessory callback is his own. */
void ftMM_SpecialN_Enter(HSD_GObj* gobj)
{
    Fighter* fp = GET_FIGHTER(gobj);

    fp->cmd_vars[0] = 0;
    fp->throw_flags_b0 = false;
    Fighter_ChangeMotionState(gobj, ftMr_MS_SpecialN, 0, 0.0f, 1.0f, 0.0f,
                              NULL);
    ftAnim_8006EBA4(gobj);
    fp->accessory4_cb = ftMM_SpecialN_FireSpawn;
}

/* code+0x2A4, [specialairn]. Same as retail ftMr_SpecialAirN_Enter, with the same two
 * differences. */
void ftMM_SpecialAirN_Enter(HSD_GObj* gobj)
{
    Fighter* fp = GET_FIGHTER(gobj);

    fp->cmd_vars[0] = 0;
    fp->throw_flags_b0 = false;
    Fighter_ChangeMotionState(gobj, ftMr_MS_SpecialAirN, 0, 0.0f, 1.0f, 0.0f,
                              NULL);
    ftAnim_8006EBA4(gobj);
    fp->accessory4_cb = ftMM_SpecialN_FireSpawn;
}

/* code+0x514 (and its tail at +0x548), the collision callback of state 343. Same as retail
 * ftMr_SpecialN_Coll: off the ground, go on in the air state at the same frame and keep his
 * accessory callback. */
void ftMM_SpecialN_Coll(HSD_GObj* gobj)
{
    Fighter* fp;

    if (ft_80082708(gobj) == false) {
        fp = GET_FIGHTER(gobj);
        ftCommon_GroundToAirStateChange(gobj, fp, ftMr_MS_SpecialAirN,
                                        ftMM_MF_SpecialN_Coll);
        fp->accessory4_cb = ftMM_SpecialN_FireSpawn;
    }
}

/* code+0x608 (and its tail at +0x63C), the collision callback of state 344. Same as retail
 * ftMr_SpecialAirN_Coll. */
void ftMM_SpecialAirN_Coll(HSD_GObj* gobj)
{
    Fighter* fp;

    if (ft_80081D0C(gobj) != false) {
        fp = GET_FIGHTER(gobj);
        ftCommon_AirToGroundStateChange(gobj, fp, ftMr_MS_SpecialN,
                                        ftMM_MF_SpecialN_Coll);
        fp->accessory4_cb = ftMM_SpecialN_FireSpawn;
    }
}

/* code+0xB08 (and its tail at +0xB2C), the accessory callback of both states. Retail
 * ftMr_SpecialN_ItemFireSpawn without the test for Mario's kind: when the animation script
 * raises the throw flag, his article 0 is created at the left hand bone (FtPart_L1stNb, 23) and
 * his effect is spawned on that bone. The item kind is the one m-ex gives his article 0
 * (MEX_GetFtItemID at 0x803D7088). */
void ftMM_SpecialN_FireSpawn(HSD_GObj* gobj)
{
    Vec3 pos;
    Fighter* fp = GET_FIGHTER(gobj);
    HSD_JObj* joint;
    ItemKind kind;

    if (!fp->throw_flags_b0) {
        return;
    }
    fp->throw_flags_b0 = false;

    joint = fp->parts[ftParts_GetBoneIndex(fp, FtPart_L1stNb)].joint;
    lb_8000B1CC(joint, NULL, &pos);
    kind = mu_ak_article_kind(gobj, ftMM_Article_Fire);
    if ((int) kind < 0) {
        /* Not on the console: there the id always exists. Without it nothing can be created. */
        OSReport("[ak] Metal Mario: no item kind for the fire article (fighter kind %d)\n",
                 (int) fp->kind);
    } else {
        itMM_Fire_Spawn(gobj, &pos, kind, fp->facing_dir);
    }
    efSync_Spawn(ftMM_Gfx_SpecialN, gobj, joint, &fp->facing_dir);
}
