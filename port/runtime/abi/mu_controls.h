/* Control options: tap jump off and tilt stick, per player, as the later Smash games offer them.
 *
 * Each player slot carries one byte of MU_CONTROL_* bits for the whole match. The host decides the
 * four bytes once, when the match starts (MuHostApi.control_flags), and the game keeps them until the
 * next match: offline every human port gets this PC's choices, a network match gets the bytes every
 * machine agreed on (port/runtime/host/controls_wire.h), and replay playback gets the bytes the replay
 * recorded ("muControls"). With every bit clear the game runs exactly as it always has.
 *
 * The bits change the game's own decisions, never the inputs: the stick and the C-stick the game
 * reads are the player's, so DI, SDI, shield drops and everything else that reads them is untouched.
 *   TAPJUMP_OFF          pushing the control stick up does not jump (X and Y still do; up tilt and
 *                        up smash with the stick are unchanged)
 *   TILT_STICK           the C-stick does tilt attacks where the A button could (standing, walking,
 *                        crouching, turning, landing, out of a jab); in the air it stays an aerial
 *   TILT_DASH_ATTACK     with TILT_STICK: the C-stick forward in a dash's first frames does a dash
 *                        attack (as Ultimate), else Melee's forward smash
 *   TILT_ITEM_TILT_THROW with TILT_STICK: the C-stick with an item does the light (tilt) throw, else
 *                        Melee's smash throw
 *   AUTO_LCANCEL         host only: this player's automatic L-cancel was agreed (a pad input, never
 *                        read by the game)
 *   20XX103              20XX Melee 1.03's controller fix ruleset for the whole match (shim/mu_20xx103.c):
 *                        set in any slot, every fighter plays it (an agreed match only ever has it
 *                        when every player agreed)
 * Plain C, no host state. SPDX-License-Identifier: GPL-2.0-or-later */
#ifndef MU_CONTROLS_H
#define MU_CONTROLS_H
/* No <stdint.h>: the game force-includes this through mu_native.h into every decomp file. */

#define MU_CONTROL_TAPJUMP_OFF 0x01u
#define MU_CONTROL_TILT_STICK 0x02u
#define MU_CONTROL_TILT_DASH_ATTACK 0x04u
#define MU_CONTROL_TILT_ITEM_TILT_THROW 0x08u
#define MU_CONTROL_AUTO_LCANCEL 0x10u
#define MU_CONTROL_20XX103 0x20u     /* 20XX Melee 1.03's ruleset (Hax$, Altimor): one player's bit turns it on for the match */
#define MU_CONTROL_GAME_BITS 0x2Fu   /* the bits the game reads */
#define MU_CONTROL_ALL_BITS 0x3Fu

/* The sub-choices mean nothing without tilt stick: dropped, so a byte names only what it does. */
static inline unsigned mu_controls_normalize(unsigned bits)
{
    bits &= MU_CONTROL_ALL_BITS;
    if (!(bits & MU_CONTROL_TILT_STICK)) bits &= ~(MU_CONTROL_TILT_DASH_ATTACK | MU_CONTROL_TILT_ITEM_TILT_THROW);
    return bits;
}

/* The match word: slot 0 in the low byte. */
static inline unsigned mu_controls_slot(unsigned int word, int slot)
{
    return slot >= 0 && slot < 4 ? (unsigned) ((word >> (8 * slot)) & 0xFFu) : 0u;
}
static inline unsigned int mu_controls_with_slot(unsigned int word, int slot, unsigned bits)
{
    if (slot < 0 || slot >= 4) return word;
    return (word & ~(0xFFu << (8 * slot))) | ((unsigned int) (bits & 0xFFu) << (8 * slot));
}
#endif
