/* Training Mode CE, native build: EventJumpTable (Globals.s) and the jump through it
 * (SkipPageList / EventNoExist, Custom Event Code - Rewrite.asm lines 98-120).
 *
 * TM-CE: Training Mode - Community Edition, by UnclePunch and the TM-CE contributors. */
#include "legacy.h"

typedef void (*LgEventEntry)(MatchInit *match, int event_id);

/* the order of the `bl` list in EventJumpTable */
static const LgEventEntry event_jump_table[LG_JUMP_COUNT] = {
    mu_tmce_legacy_AmsahTech,
    mu_tmce_legacy_AttackOnShield,
    mu_tmce_legacy_ComboTraining,
    mu_tmce_legacy_EscapeSheik,
    mu_tmce_legacy_GrabMashOut,
    mu_tmce_legacy_LedgeStall,
    mu_tmce_legacy_Ledgetech,
    mu_tmce_legacy_LedgetechCounter,
    mu_tmce_legacy_Multishine,
    mu_tmce_legacy_Reaction,
    mu_tmce_legacy_Reversal,
    mu_tmce_legacy_SDITraining,
    mu_tmce_legacy_ShieldDrop,
    mu_tmce_legacy_SlideOff,
    mu_tmce_legacy_WaveshineSDI,
};

int mu_tmce_legacy_run(int jump_index, void *md, int event_id)
{
    /* EventNoExist: the table's -1 terminator (index 15), or anything else outside it */
    if (jump_index < 0 || jump_index >= LG_JUMP_COUNT) {
        OSReport("[tmce] legacy event: no event at jump index %d\n", jump_index);
        return 0;
    }
    event_jump_table[jump_index]((MatchInit *) md, event_id);
    return 1;
}
