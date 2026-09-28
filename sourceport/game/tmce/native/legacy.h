/* Training Mode CE, native build: UnclePunch's original events (Custom Event Code - Rewrite.asm),
 * rewritten in C. The entry points the game reaches.
 *
 * On the console, onEnterVs (0x801BB128) runs the legacy prologue for an event with no event file, then
 * branches through EventJumpTable (Globals.s) by TM_GetJumpTableOffset; afterwards the vanilla
 * onEnterVs continues with the level data the event changed. Natively the shim
 * (shim/mu_tmce_legacy.c, mu_tmce_legacy_enter_vs) runs the prologue and calls mu_tmce_legacy_run,
 * which calls one of these. Each takes the StartMeleeData being filled (MatchInit) and the event's
 * index on its page (the console's r25); the level entry is lg_EventStruct() (legacy_common.h).
 *
 * TM-CE: Training Mode - Community Edition, by UnclePunch and the TM-CE contributors. */
#ifndef TMCE_LEGACY_H
#define TMCE_LEGACY_H

#include "../MexTK/mex.h"

/* EventJumpTable order (Globals.s, events.h enum JumpTableIndex) */
enum {
    LG_JUMP_AMSAHTECH,
    LG_JUMP_ATTACKONSHIELD,
    LG_JUMP_COMBOTRAINING,
    LG_JUMP_ESCAPESHEIK,
    LG_JUMP_GRABMASHOUT,
    LG_JUMP_LEDGESTALL,
    LG_JUMP_LEDGETECH,
    LG_JUMP_LEDGETECHCOUNTER,
    LG_JUMP_MULTISHINE,
    LG_JUMP_REACTION,
    LG_JUMP_REVERSAL,
    LG_JUMP_SDITRAINING,
    LG_JUMP_SHIELDDROP,
    LG_JUMP_SLIDEOFF,
    LG_JUMP_WAVESHINESDI,
    LG_JUMP_COUNT
};

/* The dispatcher (legacy_dispatch.c): runs the event at jump_index; 0 if there is none (the
 * ASM's EventNoExist). md is the StartMeleeData / MatchInit being filled. */
int mu_tmce_legacy_run(int jump_index, void *md, int event_id);

/* Lines 134-2352 (legacy_multishine.c, legacy_reaction.c, legacy_ledgestall.c, legacy_sdi.c,
 * legacy_reversal.c) */
void mu_tmce_legacy_Multishine(MatchInit *match, int event_id);
void mu_tmce_legacy_Reaction(MatchInit *match, int event_id);
void mu_tmce_legacy_LedgeStall(MatchInit *match, int event_id);
void mu_tmce_legacy_SDITraining(MatchInit *match, int event_id);
void mu_tmce_legacy_Reversal(MatchInit *match, int event_id);

/* Lines 2352-8600 */
void mu_tmce_legacy_ShieldDrop(MatchInit *match, int event_id);
void mu_tmce_legacy_AttackOnShield(MatchInit *match, int event_id);
void mu_tmce_legacy_ComboTraining(MatchInit *match, int event_id);
void mu_tmce_legacy_Ledgetech(MatchInit *match, int event_id);
void mu_tmce_legacy_LedgetechCounter(MatchInit *match, int event_id);
void mu_tmce_legacy_AmsahTech(MatchInit *match, int event_id);
void mu_tmce_legacy_WaveshineSDI(MatchInit *match, int event_id);
void mu_tmce_legacy_SlideOff(MatchInit *match, int event_id);
void mu_tmce_legacy_GrabMashOut(MatchInit *match, int event_id);
void mu_tmce_legacy_EscapeSheik(MatchInit *match, int event_id);

#endif
