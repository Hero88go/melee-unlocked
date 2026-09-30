/* Akaneia's Lucas: forward smash. He swings a stick (article 8) that reflects projectiles while
 * the command script holds cmd_vars[0], like Ness's bat. */
#include "lucas.h"

#include <melee/ft/fighter.h>
#include <melee/ft/ft_0881.h>
#include <melee/ft/ft_084E.h>
#include <melee/ft/ft_081B.h>
#include <melee/ft/ft_0892.h>
#include <melee/ft/ftanim.h>
#include <melee/ft/ftcoll.h>
#include <melee/ft/kinds/ftCommon/ftCo_Wait.h>

/* code+0x4330: reflect sound. */
static void ftLc_AttackS4_OnReflect(HSD_GObj* gobj)
{
    ft_80088478(GET_FIGHTER(gobj), 0x13F7, 0x7F, 0x40);
}

/* m-ex onsmashf, code+0xE34. */
void ftLc_AttackS4_Enter(HSD_GObj* gobj)
{
    Fighter* fp = GET_FIGHTER(gobj);
    ftLucas_FighterVars* vars = ftLc_Vars(fp);
    Item_GObj* stick;

    fp->allow_interrupt = false;
    fp->cmd_vars[0] = 0;
    fp->cmd_vars[1] = 0;
    Fighter_ChangeMotionState(gobj, ftLc_MS_AttackS4, 0, 0.0f, 1.0f, 0.0f, NULL);
    ftAnim_8006EBA4(gobj);

    stick = ftLc_SpawnStick(gobj, &fp->cur_pos, ftLc_Part_Hand, fp->facing_dir);
    vars->held_item_gobj = stick;
    if (stick != NULL) {
        fp->death2_cb = ftLc_RemoveAllArticles;
        fp->take_dmg_cb = ftLc_RemoveAllArticles;
    }
}

/* code+0x10C8 */
void ftLc_AttackS4_Anim(HSD_GObj* gobj)
{
    Fighter* fp = GET_FIGHTER(gobj);
    ftLucas_FighterVars* vars = ftLc_Vars(fp);
    ftLucasAttributes* attrs = ftLc_Attrs(fp);

    if (fp->cmd_vars[0] != 0) {
        if (!fp->reflecting) {
            ftColl_CreateReflectHit(gobj, DISC_REFLECT(&attrs->xC8_STICK_REFLECT),
                                    ftLc_AttackS4_OnReflect);
        }
    } else {
        fp->reflecting = false;
    }

    if (!ftAnim_IsFramesRemaining(gobj)) {
        if (vars->held_item_gobj != NULL) {
            ftLc_RemoveAndDestroyItem(vars->held_item_gobj);
            vars->held_item_gobj = NULL;
        }
        ft_8008A2BC(gobj);
    }
}

/* code+0x1170 */
void ftLc_AttackS4_IASA(HSD_GObj* gobj)
{
    Fighter* fp = GET_FIGHTER(gobj);
    ftLucas_FighterVars* vars = ftLc_Vars(fp);
    if (fp->allow_interrupt) {
        if (vars->held_item_gobj != NULL) {
            ftLc_RemoveAndDestroyItem(vars->held_item_gobj);
            vars->held_item_gobj = NULL;
        }
        ftCo_Wait_IASA(gobj);
    }
}

/* code+0x11D4 */
void ftLc_AttackS4_Phys(HSD_GObj* gobj)
{
    ft_80084FA8(gobj);
    ftColl_8007AEF8(gobj);
}

/* code+0x1208 */
void ftLc_AttackS4_Coll(HSD_GObj* gobj)
{
    ft_80084104(gobj);
}
