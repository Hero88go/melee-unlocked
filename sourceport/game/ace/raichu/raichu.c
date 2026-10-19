/* ACE 2.0.0 Raichu, native kind 45, external 37, internal 38. */
#include "raichu.h"
#include "raichu_trail.h"
#include "../../akaneia/common/mu_ak_kirby.h"
#include <melee/ft/fighter.h>
#include <melee/ft/ftcamera.h>
#include <melee/ft/inlines.h>
#include <melee/ft/kinds/ftCommon/ftCo_0A01.h>
#include <melee/ft/kinds/ftPikachu/ftpikachuspecialn.h>
#include <melee/ft/kinds/ftPikachu/ftpikachuspecials.h>
#include <melee/ft/kinds/ftPikachu/ftpikachuspecialhi.h>
#include <melee/ft/kinds/ftPikachu/ftpikachuspeciallw.h>
#include <melee/it/kinds/types.h>
#include <melee/it/it_26B1.h>
#include <sysdolphin/baselib/gobj.h>
#include <sysdolphin/baselib/gobjproc.h>

/* +1EDC, +2B1C. +06CC is never written, so the custom CPU-table branch is
 * unreachable. +0C9C is set to Pikachu's kind (12) on every load. */
static void ftRc_CpuProc(HSD_GObj* gobj)
{
    Fighter* fp = GET_FIGHTER(gobj);
    if (fp->is_sleeping || !ftCo_IsCpuControlled(fp)) return;
    FighterKind kind = fp->kind;
    fp->kind = Ft_Kind_Pikachu;
    mu_ak_cpu_process(gobj, NULL);
    fp->kind = kind;
}

/* +0C00: retained on the object after replacing its retail CPU proc. */
static void ftRc_CpuInit(HSD_GObj* gobj)
{
    for (HSD_GObjProc* proc = gobj->proc; proc != NULL; proc = proc->child) {
        if (proc->on_invoke == Fighter_procCpu) {
            HSD_GObjProc_RemoveProc(proc);
            HSD_GObj_SetupProc(gobj, ftRc_CpuProc, 2);
            return;
        }
    }
}

/* +0000. Exactly 0x100 bytes, including the two words after Pikachu's attrs.
 * Only articles 0 and 1 are registered by the disc's onload. */
static void ftRc_OnLoad(HSD_GObj* gobj)
{
    Fighter* fp = GET_FIGHTER(gobj);
    DISC_PTR(void)* articles = DP(fp->ft_data->x48_items);
    __builtin_memcpy(fp->dat_attrs_backup, DP(fp->ft_data->ext_attr), 0x100);
    fp->dat_attrs = fp->dat_attrs_backup;
    mu_ak_register_article(fp->kind, DP(articles[0]), 0);
    mu_ak_register_article(fp->kind, DP(articles[1]), 1);
    /* Borrowed Pikachu down special creates the retail kind in xDC. The
     * reference replay creates kind 81 at frame 683 without a Pikachu present.
     * Register its data here, as the native Metal Mario cape adaptation does. */
    it_8026B3F8(DP(articles[0]), ftRc_Attrs(fp)->xDC);
    HSD_GObj_SetupProc(gobj, ftRc_CpuInit, 2);
    ftRc_Trail_OnLoad(gobj);
}

static const MotionState ftRc_States[28] = {
#include "raichu_states.inc"
};
extern const ItemLogicTable itRc_Articles[2];
extern const MuAkKirbyCopy ftKbRc_Copy;

const MuAkFighter mu_ak_raichu = {
    .name = "Raichu", .file = "PlRc.dat",
    .onload = ftRc_OnLoad,
    .specialn = ftRc_SpecialN_Enter, .specialairn = ftRc_SpecialAirN_Enter,
    .specials = ftRc_SpecialS_Enter, .specialairs = ftRc_SpecialAirS_Enter,
    .specialhi = ftRc_SpecialHi_Enter, .specialairhi = ftRc_SpecialAirHi_Enter,
    .move_logic = ftRc_States, .move_logic_count = 28,
    .articles = itRc_Articles, .article_count = 2,
    .kirby = &ftKbRc_Copy,
    .flags = MU_AK_READY,
};
