/* Control options in the game (port/runtime/abi/mu_controls.h): tap jump off and tilt stick.
 *
 * The host hands over one byte per player slot as each match starts (gm/gmvs.c calls
 * mu_controls_match_start before the replay or the online match is set up) and the game keeps that
 * word for the whole match. The decisions themselves stay in the decomp's own functions
 * (ftCo_Jump.c, ftCo_JumpAerial.c, ftCo_AttackS4.c, ftCo_AttackHi4.c, ftCo_AttackLw4.c), each behind
 * one mu_control() test, so with the word at 0 every path is exactly the game's.
 *
 * A slot's bits apply only to a person's fighter: a human slot (Nana follows her Popo, so she shares
 * his), never a CPU, never the Pro AI seat a network match runs on this machine, and never inside a
 * TM-CE exercise, which owns its own rules. The word is constant within a match, so a rollback that
 * restores it restores the same value.
 * SPDX-License-Identifier: GPL-2.0-or-later */
#include <dolphin/os.h>
#include <melee/ft/fighter.h>
#include <melee/ft/forward.h>
#include <melee/ft/types.h>
#include <melee/pl/player.h>
#include "mu_native.h"

int mu_online_active(void);
int mu_online_proai_port(void);   /* shim/mu_online.c: the Pro AI seat this machine plays, -1 none */
int mu_tmce_enabled(void);
unsigned int mu_control_flags(void);   /* shim/mu_entry.c: MuHostApi.control_flags, 0 from an older host */
unsigned char gm_GetCurrentGameMode(void);

static unsigned int mu_controls_word;

void mu_controls_match_start(void)
{
    unsigned int word = mu_control_flags();
    /* A TM-CE exercise plays by its own rules (the same test the 20XX TE options use, shim/mu_te.c). */
    if (mu_tmce_enabled() && gm_GetCurrentGameMode() == 0x2B /* GM_EVENT */) {
        word = 0;
    }
    mu_controls_word = word;
    if (word != 0) {
        OSReport("controls: match starts with P1 %02X P2 %02X P3 %02X P4 %02X\n", mu_controls_slot(word, 0),
                 mu_controls_slot(word, 1), mu_controls_slot(word, 2), mu_controls_slot(word, 3));
    }
}

int mu_control(const struct Fighter* fp, unsigned int bit)
{
    int slot;
    if (mu_controls_word == 0 || fp == NULL) {
        return 0;
    }
    slot = fp->player_idx;
    if (slot < 0 || slot > 3 || !(mu_controls_slot(mu_controls_word, slot) & bit)) {
        return 0;
    }
    if (Player_GetPlayerSlotType(slot) != Gm_PKind_Human) {
        return 0;
    }
    if (mu_online_active() && slot == mu_online_proai_port()) {
        return 0;
    }
    return 1;
}
