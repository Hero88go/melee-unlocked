/* What Tails needs from the Akaneia integration layer (mu_ak_fighters.c and the m-ex data layer).
 *
 * On console these were m-ex runtime calls at 0x803D7058 / 0x803D7088 / 0x803D7094 (MEX_GetData
 * and the fighter-article helpers) or m-ex hooks inside the game (effect/sound id routing, the CPU
 * table slot). The Source Port has no m-ex runtime, so each one is a small native service the
 * integration layer provides once for every Akaneia fighter. Nothing here is Tails specific except
 * the names Tails passes. See NOTES.md for the exact console behaviour each must reproduce. */
#ifndef MU_AK_TAILS_HOOKS_H
#define MU_AK_TAILS_HOOKS_H

#include <Runtime/platform.h>

#include <melee/ft/forward.h>
#include <melee/it/forward.h>
#include <sysdolphin/baselib/forward.h>

/* MEX_GetData(MXDT_FTCOSTUMEARCHIVE)[fp->kind].runtimes[fp->costume_id].archive, then
 * HSD_ArchiveGetPublicAddress(archive, symbol). NULL when the costume has no archive or symbol.
 * Tails asks for "PlyTailsColor" (the per-costume trail/effect colour table). */
void* mu_ak_costume_symbol(Fighter* fp, const char* symbol);

/* MEX_IndexFighterItem (0x803D7058): registers article `index` of the fighter's own item list
 * (ft_data->x48_items[index]) as that fighter's m-ex item kind, so it can be spawned. Called from
 * OnLoad, like the vanilla fighters' it_8026B3F8 calls. */
void mu_ak_register_article(FighterKind kind, void* article, int index);

/* The item kind m-ex assigned to article `index` of this fighter (0x803D7088). */
ItemKind mu_ak_article_kind(Fighter_GObj* gobj, int index);

/* MEX_GetData(MXDT_FTNAME)[ckind]: the display name of an external character id ("Sonic"). */
const char* mu_ak_fighter_name(int ckind);

/* The console CPU pipeline Tails replaces (m-ex MexCPU_Process, a copy of ftCo_800B3900):
 *   ftCo_800B33B0(fp); ftCo_800B2AFC(fp);
 *   if (custom != NULL) custom(fp);          <- Tails's decision code
 *   ftCo_800B2790(fp); ftCo_800B3E04(fp);
 *   fp->cpu.x7C++;
 * (m-ex's copy leaves out ftCo_800B0AF4.) The three ftCo_800B2xxx/33B0 helpers are static in
 * ftCo_0A01.c, so this has to live next to them or they must be exported. */
void mu_ak_cpu_process(Fighter_GObj* gobj, void (*custom)(Fighter* fp));

#endif
