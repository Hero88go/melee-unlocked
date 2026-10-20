/* rogueMelee, native build: the game-side switch.
 *
 * rogueMelee (a roguelike mode for Melee 1.02) ships as a patched disc whose main.dol is
 * the decompiled game plus its own C. Natively that C is compiled into the game (rogue/melee/rogue) and
 * its edits to the game's source sit in melee-native.patch under #if MU_ROGUE. Rogue Mode replaces
 * All-Star under 1-P Mode > Regular Match only while its disc is the loaded mod (the host's
 * MU_MOD_ROGUE bit) and the session is offline: it is single player, and every other disc keeps
 * All-Star. */
#include <mu_native.h>

#define MU_MOD_ROGUE 0x4u   /* port/runtime/abi/mu_host.h */

int mu_rogue_enabled(void)
{
    if (!(mu_mod_flags() & MU_MOD_ROGUE)) {
        return 0;
    }
    return !(mu_online_active() || mu_online_pending() || mu_slippi_in_online_mode());
}
