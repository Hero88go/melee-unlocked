/* ACE's Ninten: forward smash (state 341). Lucas's stick swing with a bat (article 2): the
 * same routines but for the word that keeps the item (fp+2244, Lucas fp+2248), the offset of
 * the reflect bubble in the attributes (+E4, Lucas +C8), the reflect sound, and one more
 * store at the entry. The physics and collision callbacks (code+0xD4C, +0xD80) are Lucas's
 * byte for byte: ftLc_AttackS4_Phys and ftLc_AttackS4_Coll. */
#include "ninten.h"

#include <melee/ft/fighter.h>
#include <melee/ft/ft_0881.h>
#include <melee/ft/ft_0892.h>
#include <melee/ft/ftanim.h>
#include <melee/ft/ftcoll.h>
#include <melee/ft/inlines.h>
#include <melee/ft/kinds/ftCommon/ftCo_Wait.h>
#include <melee/it/item.h>
#include <sysdolphin/baselib/gobj.h>

/* The sound of a reflected projectile (code+0x35F0: li r4, 0xE0), a retail id of the common
 * bank; Lucas plays 0x13F7, a sound of his own bank. */
#define FTNT_SFX_BAT_REFLECT 0xE0

/* code+0x35DC "PlayReflectSound" */
static void ftNt_AttackS4_OnReflect(HSD_GObj* gobj)
{
    ft_80088478(GET_FIGHTER(gobj), FTNT_SFX_BAT_REFLECT, 0x7F, 0x40);
}

/* code+0x4360 "Ninten_RemoveSmashItemGOBJ": the bat held at fp+2244. */
void ftNt_RemoveBat(HSD_GObj* gobj)
{
    Fighter* fp = GET_FIGHTER(gobj);
    ftNinten_FighterVars* vars = ftNt_Vars(fp);

    if (vars->bat_gobj != NULL) {
        Item_8026A8EC(vars->bat_gobj);
        vars->bat_gobj = NULL;
    }
}

/* code+0xB50 "OnSmashF" (slot 35). Lucas's entry, and the accessory callback is cleared
 * first (Lucas's does not touch it). */
void ftNt_AttackS4_Enter(HSD_GObj* gobj)
{
    Fighter* fp = GET_FIGHTER(gobj);
    ftNinten_FighterVars* vars = ftNt_Vars(fp);
    Item_GObj* bat_gobj;

    fp->accessory4_cb = NULL;
    fp->allow_interrupt = false;
    fp->cmd_vars[0] = 0;
    fp->cmd_vars[1] = 0;
    Fighter_ChangeMotionState(gobj, ftLc_MS_AttackS4, 0, 0.0f, 1.0f, 0.0f, NULL);
    ftAnim_8006EBA4(gobj);

    bat_gobj = ftNt_SpawnBat(gobj, &fp->cur_pos, ftNt_Part_Hand, fp->facing_dir);
    vars->bat_gobj = bat_gobj;
    if (bat_gobj != NULL) {
        fp->death2_cb = ftNt_RemoveAllArticles;
        fp->take_dmg_cb = ftNt_RemoveAllArticles;
    }
}

/* code+0xC40 "AttackS4_Anim": the bat reflects while the script holds its variable. */
void ftNt_AttackS4_Anim(HSD_GObj* gobj)
{
    Fighter* fp = GET_FIGHTER(gobj);
    ftNinten_FighterVars* vars = ftNt_Vars(fp);
    ftNintenAttributes* attrs = ftNt_Attrs(fp);

    if (fp->cmd_vars[0] != 0) {
        if (!fp->reflecting) {
            ftColl_CreateReflectHit(gobj, DISC_REFLECT(&attrs->xE4_BAT_REFLECT),
                                    ftNt_AttackS4_OnReflect);
        }
    } else {
        fp->reflecting = false;
    }
    if (!ftAnim_IsFramesRemaining(gobj)) {
        if (vars->bat_gobj != NULL) {
            ftLc_RemoveAndDestroyItem(vars->bat_gobj);
            vars->bat_gobj = NULL;
        }
        ft_8008A2BC(gobj);
    }
}

/* code+0xCE8 "AttackS4_IASA" */
void ftNt_AttackS4_IASA(HSD_GObj* gobj)
{
    Fighter* fp = GET_FIGHTER(gobj);
    ftNinten_FighterVars* vars = ftNt_Vars(fp);

    if (fp->allow_interrupt) {
        if (vars->bat_gobj != NULL) {
            ftLc_RemoveAndDestroyItem(vars->bat_gobj);
            vars->bat_gobj = NULL;
        }
        ftCo_Wait_IASA(gobj);
    }
}
