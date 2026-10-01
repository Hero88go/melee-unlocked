/* Charizard: load, death, item and eye-texture events, the per-frame refuel and the tail flame.
 * m-ex ftFunction slots: onload, ondeath, onunknown, onitempickup, onmakeiteminvisible,
 * onmakeitemvisible, onitemdrop, onitemcatch, onunknownitemrelated, onhit,
 * onunknowneyetexturerelated, onframe, onrespawn, enterdoublejump. */
#include "ftlizardon.h"

#include <melee/ef/efsync.h>
#include <melee/ft/fighter.h>
#include <melee/ft/ftanim.h>
#include <melee/ft/ftparts.h>
#include <melee/ft/inlines.h>
#include <melee/ft/kinds/ftCommon/ftCo_JumpAerial.h>
#include <melee/ft/types.h>
#include <melee/it/it_26B1.h>
#include <sysdolphin/baselib/gobjproc.h>
#include <sysdolphin/baselib/jobj.h>

/* Priority the tail flame proc is added with (console HSD_GObj_SetupProc(gobj, cb, 16)). */
#define FTLZ_TAIL_FIRE_PROC_PRIORITY 16
#define FTLZ_TAIL_FIRE_TIMER_RESET 5

/* ftData->x48_items: an array of article pointers (disc data). */
typedef DISC_PTR(void) ftLz_ArticleSlot;

/* m-ex "onload" (PlLz OnLoad). */
void ftLz_Init_OnLoad(HSD_GObj* gobj)
{
    Fighter* fp = GET_FIGHTER(gobj);
    ftLz_ArticleSlot* items = (ftLz_ArticleSlot*) DISC_GET(void, fp->ft_data->x48_items);
    ftLz_DatAttrs* da;

    PUSH_ATTRS(fp, ftLz_DatAttrs);

    /* Bowser does it_8026B3F8(items[0], It_Kind_Koopa_Flame); an m-ex fighter registers its
     * articles by index and the kinds are handed out by m-ex. */
    mu_ak_register_article(fp->kind, DP(items[ftLz_Item_Fire]), ftLz_Item_Fire);
    mu_ak_register_article(fp->kind, DP(items[ftLz_Item_Rock]), ftLz_Item_Rock);
    mu_ak_register_article(fp->kind, DP(items[ftLz_Item_RockBurst]), ftLz_Item_RockBurst);

    /* Two midair jumps with Kirby's and Jigglypuff's multi-jump code; its parameters sit at the
     * end of the special attributes. */
    da = ftLz_Attrs(fp);
    fp->can_multijump = true;
    fp->x2D0 = &da->multijump;
    fp->x2226_b1 = true;

    HSD_GObj_SetupProc(gobj, ftLz_Init_TailFireProc, FTLZ_TAIL_FIRE_PROC_PRIORITY);
}

/* m-ex "ondeath" (PlLz OnRespawn): full flame reserves, no rock. */
void ftLz_Init_OnDeath(HSD_GObj* gobj)
{
    Fighter* fp = GET_FIGHTER(gobj);
    ftLz_DatAttrs* da = ftLz_Attrs(fp);
    ftLz_FighterVars* fv = ftLz_Vars(fp);

    ftParts_80074A4C(gobj, 0, 0);
    fv->fire_speed = da->fire_speed_max;
    fv->fire_size = da->fire_size_max;
    fv->rock = NULL;
    fv->tail_fire_timer = FTLZ_TAIL_FIRE_TIMER_RESET;
}

/* m-ex "onunknown" (PlLz OnDestroy): empty on the console too. */
void ftLz_Init_OnUserDataRemove(HSD_GObj* gobj) {}

/* m-ex "onitempickup": the common hold-kind hand poses. */
void ftLz_Init_OnItemPickup(HSD_GObj* gobj, bool catch_item)
{
    Fighter_OnItemPickup(gobj, catch_item, true, true);
}

/* m-ex "onmakeiteminvisible". */
void ftLz_Init_OnItemInvisible(HSD_GObj* gobj)
{
    Fighter_OnItemInvisible(gobj, true);
}

/* m-ex "onmakeitemvisible". */
void ftLz_Init_OnItemVisible(HSD_GObj* gobj)
{
    Fighter_OnItemVisible(gobj, true);
}

/* m-ex "onitemdrop" (PlLz OnItemRelease). */
void ftLz_Init_OnItemDrop(HSD_GObj* gobj, bool drop_item)
{
    Fighter_OnItemDrop(gobj, drop_item, true, true);
}

/* m-ex "onitemcatch": the pickup handler with the caller's flag. */
void ftLz_Init_OnItemCatch(HSD_GObj* gobj, bool catch_item)
{
    ftLz_Init_OnItemPickup(gobj, catch_item);
}

/* m-ex "onunknownitemrelated": the drop handler with the caller's flag. */
void ftLz_Init_OnItemUnknown(HSD_GObj* gobj, bool drop_item)
{
    ftLz_Init_OnItemDrop(gobj, drop_item);
}

/* m-ex "onhit" (PlLz EyeTextureDamaged). */
void ftLz_Init_OnKnockbackEnter(HSD_GObj* gobj)
{
    Fighter_OnKnockbackEnter(gobj, true);
}

/* m-ex "onunknowneyetexturerelated" (PlLz EyeTextureNormal). */
void ftLz_Init_OnKnockbackExit(HSD_GObj* gobj)
{
    Fighter_OnKnockbackExit(gobj, true);
}

/* m-ex "onframe": the flame reserves refill outside Flamethrower. */
void ftLz_Init_OnFrame(HSD_GObj* gobj)
{
    ftLz_SpecialN_RefuelFire(gobj);
}

/* m-ex "onrespawn" (PlLz ResetAttributes): reload the special attributes from the file. */
void ftLz_Init_LoadSpecialAttrs(HSD_GObj* gobj)
{
    COPY_ATTRS(gobj, ftLz_DatAttrs);
}

/* m-ex "enterdoublejump". */
void ftLz_Init_EnterDoubleJump(HSD_GObj* gobj)
{
    ftCo_JumpAerial_Enter_Basic(gobj);
}

/* The GObj proc OnLoad adds (PlLz SpawnTailFire): every frame the fighter is visible, the tail
 * flame effect is spawned on its bone. The countdown has no other reader. */
void ftLz_Init_TailFireProc(HSD_GObj* gobj)
{
    Fighter* fp = GET_FIGHTER(gobj);
    ftLz_DatAttrs* da;
    ftLz_FighterVars* fv;
    HSD_JObj* jobj;

    if (fp->invisible || fp->x2226_b4) {
        return;
    }
    da = ftLz_Attrs(fp);
    fv = ftLz_Vars(fp);
    jobj = fp->parts[da->tail_fire_part].joint;
    HSD_JObjSetMtxDirtySub(jobj);
    efSync_Spawn(da->tail_fire_gfx, gobj, jobj, &fp->facing_dir);
    if (fv->tail_fire_timer > 0) {
        fv->tail_fire_timer -= 1;
    } else {
        fv->tail_fire_timer = FTLZ_TAIL_FIRE_TIMER_RESET;
    }
}
