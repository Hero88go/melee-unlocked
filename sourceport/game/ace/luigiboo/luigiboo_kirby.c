/* PlKbCpLg.dat has only the retail hat root, no kbFunction/ftcmd/itFunction.
 * MxDt Kirby function slots 0..3 for internal 44 equal retail internal 17:
 * 800F0710, 800F07C0, 800F9614, 800F9684; slots 4..8 are zero. */
#include "luigiboo_kirby.h"

#ifdef MU_AK_KIRBY_RETAIL_STATES
#include <melee/ft/inlines.h>
#include <melee/ft/kinds/ftKirby/ftkirby.h>
#include <melee/ft/types.h>
#include <melee/it/it_26B1.h>
#include <melee/it/types.h>

static void ftLb_KirbyGain(HSD_GObj* gobj)
{
    Fighter* fp = GET_FIGHTER(gobj);
    if (fp->u.kb.hat.jobj == NULL) {
        mu_ak_kirby_attach_hat(gobj);
        fp->x2225_b2 = true;
    }
}

/* Slot 5 on the console is zero, and its load hook (the m-ex code at ftKb_SpecialN_800F16D0+1C)
 * then registers nothing for an added kind: it calls table 5 when the row is set and falls back
 * to the retail switch only below kind 27. So the console never stores the article of Kirby's
 * Luigi fireball (It_Kind_Kirby_LuigiFire) for this copy (that switch is the kind's only store),
 * and the retail special_n above creates that item from the empty table entry: a mod bug. Read
 * as written it stopped the native game (a NULL article, address 0x10 read); what the console
 * does after that read is not known. Here the copy stores the article its own hat file has for
 * it, as the retail switch does for Luigi: PlKbCpLg.dat is retail Luigi's hat file, and
 * hat_dynamics[0] is that article (g->hats[16]->hat_dynamics[0] in the retail case). */
static void ftLb_KirbyInitItems(FighterKind kind, KirbyHatStruct* hat)
{
    (void) kind;
    it_8026B3F8(DISC_GET(Article, hat->hat_dynamics[0]), It_Kind_Kirby_LuigiFire);
}

const MuAkKirbyCopy ftLb_KirbyCopy = {
    .on_swallow = ftLb_KirbyGain,
    .on_lose = ftKb_SpecialN_800F07C0,
    .special_n = ftKb_SpecialNLg_800F9614,
    .special_air_n = ftKb_SpecialNLg_800F9684,
    .init_items = ftLb_KirbyInitItems,
    .flags = MU_AK_KIRBY_RETAIL_STATES,
};
#endif
