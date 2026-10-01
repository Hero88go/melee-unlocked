/* Charizard Down B, Rock Smash (states 351, 352).
 *
 * Entering the move puts a rock item in Charizard's hands (unless one is already out); the rock's
 * look follows the floor material under Charizard. When the script sets cmd_vars[0] the rock breaks
 * into a burst item at the TransN2 bone. cmd_vars[1] allows one turn-around. Getting hit or dying
 * during the move destroys the rock, and the rock destroys itself when Charizard leaves the move. */
#include "ftlizardon.h"

#include <melee/ft/fighter.h>
#include <melee/ft/ft_081B.h>
#include <melee/ft/ft_084E.h>
#include <melee/ft/ft_0892.h>
#include <melee/ft/ftanim.h>
#include <melee/ft/ftcommon.h>
#include <melee/ft/ftparts.h>
#include <melee/ft/inlines.h>
#include <melee/ft/kinds/ftCommon/ftCo_Fall.h>
#include <melee/ft/kinds/ftCommon/inlines.h>
#include <melee/ft/types.h>
#include <melee/it/item.h>
#include <melee/lb/lb_00B0.h>

static void ftLz_SpecialLw_SetRockCallbacks(Fighter* fp)
{
    fp->death2_cb = ftLz_SpecialLw_DestroyRock;
    fp->take_dmg_cb = ftLz_SpecialLw_DestroyRock;
}

/* PlLz SpecialLw_Enter, shared by the ground and air entries. */
static void ftLz_SpecialLw_EnterState(HSD_GObj* gobj, FtMotionId msid)
{
    Fighter* fp = GET_FIGHTER(gobj);
    ftLz_FighterVars* fv = ftLz_Vars(fp);

    Fighter_ChangeMotionState(gobj, msid, Ft_MF_None, 0.0F, 1.0F, 0.0F, NULL);
    fp->cmd_vars[0] = 0;
    fp->cmd_vars[1] = 1;
    if (fv->rock == NULL) {
        float facing_dir = fp->facing_dir;
        int part = ftParts_GetBoneIndex(fp, FtPart_TransN2);
        int kind = mu_ak_article_kind(gobj, ftLz_Item_Rock);
        /* The console passes an uninitialised stack Vec3 as the spawn position; the rock is put in
         * Charizard's hand right after, so any position works. The fighter's own is used here. */
        Vec3 pos = fp->cur_pos;
        /* the low byte of the floor flags, the floor's material */
        int material = (u8) fp->coll_data.floor.flags;
        itLzRock_Spawn(gobj, &pos, part, kind, facing_dir, material);
    }
    ftLz_SpecialLw_SetRockCallbacks(fp);
}

/* m-ex "speciallw". */
void ftLz_SpecialLw_Enter(HSD_GObj* gobj)
{
    ftLz_SpecialLw_EnterState(gobj, ftLz_MS_SpecialLw);
}

/* m-ex "specialairlw". */
void ftLz_SpecialAirLw_Enter(HSD_GObj* gobj)
{
    ftLz_SpecialLw_EnterState(gobj, ftLz_MS_SpecialAirLw);
}

/* PlLz SpecialLw_DestroyRock: the take-damage and death callback during the move. */
void ftLz_SpecialLw_DestroyRock(HSD_GObj* gobj)
{
    HSD_GObj* rock = ftLz_Vars(GET_FIGHTER(gobj))->rock;
    if (rock != NULL) {
        Item_8026A8EC(rock);
    }
}

void ftLz_SpecialLw_Anim(HSD_GObj* gobj)
{
    if (!ftAnim_IsFramesRemaining(gobj)) {
        ft_8008A2BC(gobj);
    }
}

/* PlLz SpecialLw_IASACB (both states): break the rock on cmd_vars[0], turn once on cmd_vars[1]. */
void ftLz_SpecialLw_IASA(HSD_GObj* gobj)
{
    Fighter* fp = GET_FIGHTER(gobj);

    if (fp->cmd_vars[0] != 0) {
        Vec3 pos;
        float facing_dir;
        int part;
        int kind;

        lb_8000B1CC(fp->parts[ftParts_GetBoneIndex(fp, FtPart_TransN2)].joint, NULL, &pos);
        facing_dir = fp->facing_dir;
        part = ftParts_GetBoneIndex(fp, FtPart_TransN2);
        kind = mu_ak_article_kind(gobj, ftLz_Item_RockBurst);
        itLzRockBurst_Spawn(gobj, &pos, part, kind, facing_dir);
        fp->cmd_vars[0] = 0;
    }
    if (fp->cmd_vars[1] != 0) {
        float stick_x = fp->input.lstick[0].x;
        if ((stick_x < 0.0F ? -stick_x : stick_x) > 0.0F) {
            ftCommon_UpdateFacing(fp);
            ftPartSetRotY(fp, 0, (float) (fp->facing_dir * M_PI_2));
            fp->cmd_vars[1] = 0;
        }
    }
}

void ftLz_SpecialLw_Phys(HSD_GObj* gobj)
{
    ft_80084F3C(gobj);
}

void ftLz_SpecialLw_Coll(HSD_GObj* gobj)
{
    Fighter* fp = GET_FIGHTER(gobj);
    if (ft_80082708(gobj) == GA_Ground) {
        ftCommon_GroundToAirStateChange(gobj, fp, ftLz_MS_SpecialAirLw,
                                        ftCommon_GroundAirColl_MF);
        ftLz_SpecialLw_SetRockCallbacks(fp);
    }
}

void ftLz_SpecialAirLw_Anim(HSD_GObj* gobj)
{
    if (!ftAnim_IsFramesRemaining(gobj)) {
        ftCo_Fall_Enter(gobj);
    }
}

void ftLz_SpecialAirLw_IASA(HSD_GObj* gobj)
{
    ftLz_SpecialLw_IASA(gobj);
}

void ftLz_SpecialAirLw_Phys(HSD_GObj* gobj)
{
    ft_80084EEC(gobj);
}

void ftLz_SpecialAirLw_Coll(HSD_GObj* gobj)
{
    Fighter* fp = GET_FIGHTER(gobj);
    if (ft_80081D0C(gobj) != GA_Ground) {
        ftCommon_AirToGroundStateChange(gobj, fp, ftLz_MS_SpecialLw, ftCommon_GroundAirColl_MF);
        ftLz_SpecialLw_SetRockCallbacks(fp);
    }
}
