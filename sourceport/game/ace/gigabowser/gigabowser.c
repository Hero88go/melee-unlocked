/* ACE's playable Giga Bowser: load and the descriptor (m-ex "ftFunction" of PlGkp.dat). */
#include "gigabowser.h"

#include <melee/ft/kinds/ftGigaKoopa/ftgkoopa.h>

/* code+0x0 [OnLoad], the one slot the file exports. It calls the retail Giga Bowser's OnLoad by
 * its console address (0x8014F6B8, ftGk_Init_OnLoad): the attribute block of the fighter file
 * (ftKoopaAttributes, the same values as the retail PlGk.dat) is installed over fp->dat_attrs,
 * article 0 of the file becomes the data of the retail flame item (It_Kind_Koopa_Flame), and the
 * two fighter flags Giga Bowser has are set. Then it installs the CPU helper. */
static void ftGkp_Init_OnLoad(HSD_GObj* gobj)
{
    ftGk_Init_OnLoad(gobj);
    ftGkp_Cpu_InitSpoofData(gobj);
}

/* MU_AK_GKP_EXPLICIT_DEFAULTS (off): every slot the file leaves to m-ex, written out as the
 * retail function MxDt names for internal fighter 58 (run-source/rel09-ace-native/gigabowser/
 * FUNCTIONS.md lists the addresses). The registry resolves the same functions by itself from
 * MxDt when the fields are NULL, which is how the mod works and what this build uses. The switch
 * is only a way to tell a fault of that lookup from a fault of the fighter: it cannot name
 * slots 38 to 40 (the results screen callbacks and the demo state table), which have no field. */
#ifdef MU_AK_GKP_EXPLICIT_DEFAULTS
#include <melee/ft/kinds/ftCommon/ftCo_JumpAerial.h>
#include <melee/ft/kinds/ftKoopa/ftkoopaspecialhi.h>
#include <melee/ft/kinds/ftKoopa/ftkoopaspeciallw.h>
#include <melee/ft/kinds/ftKoopa/ftkoopaspecialn.h>
#include <melee/ft/kinds/ftKoopa/ftkoopaspecials.h>

/* The item events take a second argument (HSD_GObj*, bool); the registry writes them into the
 * game's item event tables as they are. */
#define GKP_ITEM_EVENT(fn) ((MuAkEvent) (void (*)(void)) (fn))
#define GKP_DEFAULTS                                                          \
    .ondeath = ftGk_Init_OnDeath,                                             \
    .specialn = ftKp_SpecialN_Enter,                                          \
    .specialairn = ftKp_SpecialAirN_Enter,                                    \
    .specials = ftKp_SpecialS_Enter,                                          \
    .specialairs = ftKp_SpecialAirS_Enter,                                    \
    .specialhi = ftKp_SpecialHi_Enter,                                        \
    .specialairhi = ftKp_SpecialAirHi_Enter,                                  \
    .speciallw = ftKp_SpecialLw_Enter,                                        \
    .specialairlw = ftKp_SpecialAirLw_Enter,                                  \
    .onitempickup = GKP_ITEM_EVENT(ftGk_Init_OnItemPickup),                   \
    .onmakeiteminvisible = ftGk_Init_OnItemInvisible,                         \
    .onmakeitemvisible = ftGk_Init_OnItemVisible,                             \
    .onitemdrop = GKP_ITEM_EVENT(ftGk_Init_OnItemDrop),                       \
    .onitemcatch = GKP_ITEM_EVENT(ftGk_Init_OnItemPickup),                    \
    .onunknownitemrelated = GKP_ITEM_EVENT(ftGk_Init_OnItemDrop),             \
    .onhit = ftGk_Init_OnKnockbackEnter,                                      \
    .onunknowneyetexturerelated = ftGk_Init_OnKnockbackExit,                  \
    .onframe = ftGk_Init_UnkMotionStates3,                                    \
    .onrespawn = ftGk_Init_LoadSpecialAttrs,                                  \
    .enterdoublejump = ftCo_JumpAerial_Enter_Basic,                           \
    .move_logic = ftGk_Init_MotionStateTable,                                 \
    .move_logic_count = ftKp_MS_SelfCount,
#else
#define GKP_DEFAULTS
#endif

/* Nothing has run yet, so the fighter is not marked MU_AK_READY. No article of his own: the
 * flame is the retail item kind, created by Bowser's code. */
const MuAkFighter mu_ak_gigabowser = {
    .name = "Giga Bowser",
    .file = "PlGkp.dat",
    .onload = ftGkp_Init_OnLoad,
    GKP_DEFAULTS
    .flags = 0,
};
