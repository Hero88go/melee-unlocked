/* The m-ex runtime services the added fighters' code calls, native.
 *
 * On the console these are small routines in the m-ex codeset (MEX_IndexFighterItem at 803D7058,
 * MEX_GetFtItemID at 803D7088, MEX_GetData) that fighter code reaches through fixed addresses. Here
 * each one is a plain C function over the registry (mu_ak_fighters.c) and the m-ex data layer
 * (shim/mu_mex.c). Three more services need a static of the decomp and live next to it, behind
 * MU_AKANEIA_FIGHTERS: mu_ak_item_create (it/item.c), mu_ak_mp_joint_list (mp/mplib.c) and
 * mu_ak_cpu_process (ft/kinds/ftCommon/ftCo_0A01.c). */
#include <melee/ft/fighter.h>
#include <melee/ft/ftdata.h>
#include <melee/ft/types.h>
#include <melee/it/forward.h>
#include <sysdolphin/baselib/archive.h>
#include <sysdolphin/baselib/gobj.h>

#include <stddef.h>

#include "../mu_ak_fighter.h"
#include "../sonic/sonic.h"

/* MEX_IndexFighterItem: hands article `index` of the fighter's own item list to the item code,
 * under the item kind MxDt gives that article. Called from each fighter's onload. */
void mu_ak_register_article(FighterKind kind, void* article, int index)
{
    const int item_kind = mu_ak_item_kind(kind, index);
    if (item_kind < 0) {
        OSReport("[ak] fighter kind %d has no item kind for article %d; not registered\n", (int) kind,
                 index);
        return;
    }
    mu_ak_article_set(item_kind, article);
}

/* MEX_GetFtItemID (803D7088): the item kind of the fighter's article `index`, -1 when it has
 * none. */
ItemKind mu_ak_article_kind(HSD_GObj* fighter_gobj, int index)
{
    Fighter* fp = GET_FIGHTER(fighter_gobj);
    int kind = mu_ak_item_kind(fp->kind, index);
    /* An index the fighter's own list does not have, asked by a Kirby who holds an ability, is
     * looked up in the list of the fighter he copied (+03C to +07C of the routine). Kirby is a
     * retail kind, so his own lookup is always -1 here. */
    if (kind < 0 && fp->kind == Ft_Kind_Kirby && fp->u.kb.hat.kind != Ft_Kind_Kirby) {
        kind = mu_ak_item_kind(fp->u.kb.hat.kind, index);
    }
    return (ItemKind) kind;
}

/* MEX_GetData(MXDT_FTCOSTUMEARCHIVE)[kind].runtimes[costume].archive: the loaded costume file. */
HSD_Archive* mu_ak_costume_archive(FighterKind kind, int costume)
{
    const struct UnkCostumeList* list;
    if ((int) kind < 0 || (int) kind >= FT_KIND_TABLE_MAX) {
        return NULL;
    }
    list = &CostumeListsForeachCharacter[kind];
    if (list->costume_list == NULL || costume < 0 || costume >= list->numCostumes) {
        return NULL;
    }
    return list->costume_list[costume].x14_archive;
}

/* A public symbol of the fighter's costume file, NULL when the costume has none. */
void* mu_ak_costume_symbol(Fighter* fp, const char* symbol)
{
    HSD_Archive* archive = mu_ak_costume_archive(fp->kind, fp->costume_id);
    return archive != NULL ? HSD_ArchiveGetPublicAddress(archive, symbol) : NULL;
}

/* MEX_GetData(MXDT_FTNAME)[external id]. Native player state holds native character kinds, so the
 * kind goes back to the disc's external id first. */
const char* mu_ak_fighter_name(int ckind)
{
    return mu_mex_fighter_name(mu_ak_mex_external(ckind));
}

static int sonic_item_kind(HSD_GObj* fighter_gobj, int index)
{
    return (int) mu_ak_article_kind(fighter_gobj, index);
}

/* Sonic's code takes its services through a table (sonic.h); filled at each view change. */
void mu_ak_services_apply(int mod_view)
{
    if (!mod_view) {
        __builtin_memset(&mu_ak_sonic_hooks, 0, sizeof mu_ak_sonic_hooks);
        return;
    }
    mu_ak_sonic_hooks.costume_archive = mu_ak_costume_archive;
    mu_ak_sonic_hooks.index_fighter_item = mu_ak_register_article;
    mu_ak_sonic_hooks.fighter_item_kind = sonic_item_kind;
    mu_ak_sonic_hooks.fighter_name = mu_ak_fighter_name;
}
