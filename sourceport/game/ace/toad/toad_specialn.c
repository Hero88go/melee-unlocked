/* ACE's Toad: the neutral special (states 343 and 344), the file's own code.
 *
 * Mario's neutral special (ft/kinds/ftMario/ftmariospecialn.c) in shape: the animation script
 * raises the throw flag, the accessory callback creates his article 0 at the hand bone and
 * spawns effect 5000 of his own effect file. Differences from Mario's: the entries clear only
 * the first throw flag and no command variable, and the interrupt callbacks are empty. */
#include "toad.h"

#include <dolphin/os.h>

#include <melee/ef/efsync.h>
#include <melee/ft/fighter.h>
#include <melee/ft/ft_081B.h>
#include <melee/ft/ft_084E.h>
#include <melee/ft/ft_0892.h>
#include <melee/ft/ftanim.h>
#include <melee/ft/ftcommon.h>
#include <melee/ft/ftparts.h>
#include <melee/ft/inlines.h>
#include <melee/ft/kinds/ftCommon/ftCo_Fall.h>
#include <melee/ft/types.h>
#include <melee/lb/lb_00B0.h>

/* The state change flags of the ground and air swap (0x5000 at +09A0 and +0A84). */
#define ftTd_MF_SpecialN_Coll (Ft_MF_SkipColAnim | Ft_MF_UpdateCmd)

static void ftTd_SpecialN_ShotSpawn(HSD_GObj* gobj);

/* +0284 (slot 4, specialn) */
void ftTd_SpecialN_Enter(HSD_GObj* gobj)
{
    Fighter* fp = GET_FIGHTER(gobj);

    fp->throw_flags_b0 = false;
    Fighter_ChangeMotionState(gobj, ftMr_MS_SpecialN, Ft_MF_None, 0.0f, 1.0f, 0.0f, NULL);
    ftAnim_8006EBA4(gobj);
    fp->accessory4_cb = ftTd_SpecialN_ShotSpawn;
}

/* +02FC (slot 5, specialairn) */
void ftTd_SpecialAirN_Enter(HSD_GObj* gobj)
{
    Fighter* fp = GET_FIGHTER(gobj);

    fp->throw_flags_b0 = false;
    Fighter_ChangeMotionState(gobj, ftMr_MS_SpecialAirN, Ft_MF_None, 0.0f, 1.0f, 0.0f, NULL);
    ftAnim_8006EBA4(gobj);
    fp->accessory4_cb = ftTd_SpecialN_ShotSpawn;
}

/* +1610: the accessory callback of both states. When the script raises the throw flag (bit 0x80
 * of fp+2210): article 0 at the bone of part 0x17 (the left hand, as Mario's), then effect 5000
 * on that bone with his facing. The item kind is the one m-ex gives his article 0
 * (MEX_GetFtItemID at 0x803D7088). */
static void ftTd_SpecialN_ShotSpawn(HSD_GObj* gobj)
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
    kind = mu_ak_article_kind(gobj, ftTd_Article_Shot);
    if ((int) kind < 0) {
        /* Not on the console: there the id always exists. */
        OSReport("[ak] Toad: no item kind for the shot article (fighter kind %d)\n",
                 (int) fp->kind);
    } else {
        itTd_Shot_Spawn(gobj, &pos, kind, fp->facing_dir);
    }
    efSync_Spawn(FTTD_EFFECT_SPECIALN, gobj, joint, &fp->facing_dir);
}

/* +08EC: state 343, animation. */
void ftTd_SpecialN_Anim(HSD_GObj* gobj)
{
    if (!ftAnim_IsFramesRemaining(gobj)) {
        ft_8008A2BC(gobj);
    }
}

/* +0938 (and +0A1C, +0B18, +0E94, +14F0, +1590): an interrupt callback that is a lone return. */
void ftTd_SpecialN_IASA(HSD_GObj* gobj)
{
    (void) gobj;
}

/* +093C: state 343, physics: a branch to the retail routine. */
void ftTd_SpecialN_Phys(HSD_GObj* gobj)
{
    ft_80084F3C(gobj);
}

/* +0940: state 343, collision. Off the floor the air state goes on at the same frame and keeps
 * the accessory callback. */
void ftTd_SpecialN_Coll(HSD_GObj* gobj)
{
    Fighter* fp;

    if (!ft_80082708(gobj)) {
        fp = GET_FIGHTER(gobj);
        ftCommon_8007D5D4(fp);
        Fighter_ChangeMotionState(gobj, ftMr_MS_SpecialAirN, ftTd_MF_SpecialN_Coll,
                                  fp->cur_anim_frame, 1.0f, 0.0f, NULL);
        fp->accessory4_cb = ftTd_SpecialN_ShotSpawn;
    }
}

/* +09D0: state 344, animation. */
void ftTd_SpecialAirN_Anim(HSD_GObj* gobj)
{
    if (!ftAnim_IsFramesRemaining(gobj)) {
        ftCo_Fall_Enter(gobj);
    }
}

/* +0A20: state 344, physics: a branch to the retail routine. */
void ftTd_SpecialAirN_Phys(HSD_GObj* gobj)
{
    ft_80084DB0(gobj);
}

/* +0A24: state 344, collision. */
void ftTd_SpecialAirN_Coll(HSD_GObj* gobj)
{
    Fighter* fp;

    if (ft_80081D0C(gobj)) {
        fp = GET_FIGHTER(gobj);
        ftCommon_8007D7FC(fp);
        Fighter_ChangeMotionState(gobj, ftMr_MS_SpecialN, ftTd_MF_SpecialN_Coll,
                                  fp->cur_anim_frame, 1.0f, 0.0f, NULL);
        fp->accessory4_cb = ftTd_SpecialN_ShotSpawn;
    }
}
