/* ACE's Daisy: the entry of the side special on the ground, and the down special (states 352
 * and 353; m-ex "ftFunction" of PlDa.dat).
 *
 * The down special is Peach's turnip pull (ft/kinds/ftPeach/ftpeachspeciallw.c) with these
 * differences: what is pulled is always her own article 1 (item kind from MxDt by index; no
 * roll for Peach's rare items), the held item is thrown when it is that article, the effect of
 * the pull is model effect 0 of her effect file (id 5000) where Peach spawns 1234, and the
 * effect comes before the pickup. The animation and physics callbacks of both states are
 * Peach's functions (same bodies in the file). */
#include "daisy.h"

#include <dolphin/os.h>

#include <melee/ef/efsync.h>
#include <melee/ft/fighter.h>
#include <melee/ft/ft_081B.h>
#include <melee/ft/ftanim.h>
#include <melee/ft/ftcommon.h>
#include <melee/ft/inlines.h>
#include <melee/ft/kinds/ftCommon/ftCo_ItemThrow.h>
#include <melee/ft/kinds/ftCommon/ftpickupitem.h>
#include <melee/ft/kinds/ftCommon/inlines.h>
#include <melee/ft/types.h>
#include <melee/it/inlines.h>
#include <melee/lb/lb_00B0.h>
#include <sysdolphin/baselib/gobj.h>

/* The effect of the pull (code+0x140C: li r3, 0x1388): an m-ex id, model effect 0 of the
 * effect file MxDt gives her (EfPeData.dat, which ACE ships with entries of hers). */
#define FTDA_GFX_SPECIALLW 5000

_Static_assert(ftCo_MS_LightThrowF4 == 0x6C && ftCo_MS_LightThrowAirF4 == 0x70,
               "the two throw states PlDa.dat passes at code+0x8AC and +0x928");

/* ---- the side special ---------------------------------------------------------------------------
 * The callback both entries leave at fp+21EC is Peach's static reset (0x8011C2F4, in
 * ftpeachspecials.c); her ground entry stores that address. The same body, for the ground
 * entry below (her air entry is Peach's function and installs Peach's own). */
static void ftDa_SpecialS_Reset(HSD_GObj* gobj)
{
    Fighter* fp = GET_FIGHTER(gobj);
    ftPe_DatAttrs* da = fp->dat_attrs;

    Fighter_ClearCmdVars(fp);
    fp->self_vel.x = 0;
    fp->x6A4_transNOffset.x = 0;
    if (fp->active_sticky.lstick.x < da->x30) {
        fp->x2070.count_thrown_items = true;
        fp->mv.pe.specials.x0 = true;
    } else {
        fp->mv.pe.specials.x0 = false;
    }
}

/* code+0x700, [specials]. Peach's ftPe_SpecialS_Enter with one number changed: the upward
 * speed is set to 1.0 where Peach clears it. */
void ftDa_SpecialS_Enter(HSD_GObj* gobj)
{
    Fighter* fp = GET_FIGHTER(gobj);
    ftPe_DatAttrs* da = fp->dat_attrs;

    fp->x21EC = ftDa_SpecialS_Reset;
    fp->self_vel.y = 1.0f;
    fp->gr_vel = da->x34 * fp->facing_dir;
    Fighter_ChangeMotionState(gobj, ftPe_MS_SpecialSStart, Ft_MF_None, 0.0f, 1.0f, 0.0f,
                              NULL);
    ftAnim_8006EBA4(gobj);
}

/* ---- the down special ---------------------------------------------------------------------------
 * code+0x1378, the accessory callback of states 352 and 353: pull the article once the script
 * sets the throw flag. The joint position the file reads here (part 109) is not used: the
 * spawn reads it again. */
static void ftDa_SpecialLw_SpawnVeg(HSD_GObj* gobj)
{
    Fighter* fp = GET_FIGHTER(gobj);
    ItemKind kind;
    Item_GObj* veg_gobj;

    if (!ftCheckThrowB0(fp)) {
        return;
    }
    kind = mu_ak_article_kind(gobj, ftDa_Article_Veg);
    veg_gobj = itDa_Veg_Spawn(gobj, kind, fp->facing_dir);
    fp->item_gobj = veg_gobj;
    fp->u.pe.veg_gobj = veg_gobj;
    if (veg_gobj != NULL) {
        Vec3 pos = fp->cur_pos;

        efSync_Spawn(FTDA_GFX_SPECIALLW, gobj, &pos);
        ftpickupitem_80094818(gobj, false);
        fp->death2_cb = ftDa_Init_OnDeath2;
        fp->take_dmg_cb = ftDa_Init_OnDeath2;
    }

    {
        /* Bring-up evidence (the first ones only). */
        static int logged;
        if (logged < 4) {
            logged++;
            OSReport("[ak] Daisy pull: item kind %d, %s\n", (int) kind,
                     veg_gobj != NULL ? "created" : "not created");
        }
    }
}

/* code+0x7E4, [speciallw]: with nothing held, the pull; holding her own article, the throw;
 * holding anything else, nothing. The file clears the first byte of the throw flags only. */
void ftDa_SpecialLw_Enter(HSD_GObj* gobj)
{
    Fighter* fp = GET_FIGHTER(gobj);
    HSD_GObj* held_gobj = fp->item_gobj;
    ItemKind kind = mu_ak_article_kind(gobj, ftDa_Article_Veg);

    if (held_gobj != NULL) {
        if (itGetKind(held_gobj) == kind) {
            ftCo_800957F4(gobj, ftCo_MS_LightThrowF4);
        }
        return;
    }
    fp->throw_flags &= 0x00FFFFFF;
    Fighter_ChangeMotionState(gobj, ftPe_MS_SpecialLw, Ft_MF_None, 0.0f, 1.0f, 0.0f, NULL);
    ftAnim_8006EBA4(gobj);
    fp->accessory4_cb = ftDa_SpecialLw_SpawnVeg;
}

/* code+0x8C4, [specialairlw]: only the throw of her own article. */
void ftDa_SpecialAirLw_Enter(HSD_GObj* gobj)
{
    Fighter* fp = GET_FIGHTER(gobj);
    HSD_GObj* held_gobj = fp->item_gobj;

    if (held_gobj == NULL) {
        return;
    }
    if (itGetKind(held_gobj) == mu_ak_article_kind(gobj, ftDa_Article_Veg)) {
        ftCo_800957F4(gobj, ftCo_MS_LightThrowAirF4);
    }
}

/* code+0x14B0: off the ground during 352. */
static void ftDa_SpecialLw_GroundToAir(HSD_GObj* gobj)
{
    Fighter* fp = GET_FIGHTER(gobj);

    ftCommon_GroundToAirStateChange(gobj, fp, ftPe_MS_SpecialAirLw, FTDA_SPECIALLW_COLL_MF);
    fp->accessory4_cb = ftDa_SpecialLw_SpawnVeg;
}

/* code+0x1528: landed during 353. */
static void ftDa_SpecialLw_AirToGround(HSD_GObj* gobj)
{
    Fighter* fp = GET_FIGHTER(gobj);

    ftCommon_AirToGroundStateChange(gobj, fp, ftPe_MS_SpecialLw, FTDA_SPECIALLW_COLL_MF);
    fp->accessory4_cb = ftDa_SpecialLw_SpawnVeg;
}

/* code+0xAD8 (the common routine is reached through a pointer kept at code+0x14AC) */
void ftDa_SpecialLw_Coll(HSD_GObj* gobj)
{
    ft_8008403C(gobj, ftDa_SpecialLw_GroundToAir);
}

/* code+0xB70 */
void ftDa_SpecialAirLw_Coll(HSD_GObj* gobj)
{
    ft_80082C74(gobj, ftDa_SpecialLw_AirToGround);
}
