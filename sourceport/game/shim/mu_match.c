/* The match a scripted run asked for.
 *
 * The sweep has to play every character on every stage. Driving the character
 * and stage screens by cursor position for each combination is slow and
 * fragile: one mistimed press and the run is testing the wrong thing without
 * saying so. The host takes --match instead and the game asks for it once, at
 * the point where a VS match's rules and players are finalised
 * (gmvsmelee.c, gmVsMelee_EnterVs). The menus still run exactly as they do for
 * a player; only the outcome is forced.
 *
 * Game code never includes mu_shim.h, so gmvsmelee.c declares this as plain
 * ints and the ABI stays on this side of the line.
 */
#include "mu_host.h"
#include "mu_shim.h"

int mu_match_request(int* stage, int* kind, int* cpu, int* level, int* costume,
                     int count)
{
    const MuMatchOverride* over;
    int i;

    if (mu_host->version < 2 || mu_host->match_override == NULL) {
        return 0;   /* an older host: nothing to force */
    }
    over = mu_host->match_override();
    if (over == NULL) {
        return 0;   /* the ordinary case: no --match on the command line */
    }

    *stage = over->stage;
    for (i = 0; i < count; i++) {
        if (i >= (int) (sizeof over->players / sizeof over->players[0])) {
            kind[i] = -1;
            continue;
        }
        kind[i] = over->players[i].kind;
        cpu[i] = over->players[i].cpu;
        level[i] = over->players[i].cpu_level;
        costume[i] = over->players[i].costume;
    }
    return 1;
}

/* --rng-seed (M7 lockstep parity): forces the RNG and marks the match start at the first GS_VS
 * frame callback, after scene setup and immediately before the match simulation begins. The
 * native game and recomp guest use the same callback, so a scripted run reaches identical RNG
 * from the first in-match frame. Marks the match start unconditionally (mark_match_start, for
 * @match-relative scripts on an ordinary run too); overwrites the seed only when --rng-seed was
 * given (rng_seed_override sets has_value). A pre-version-3 host leaves both fields NULL.
 *
 * The actual write through HSD_RandSeedPtr happens in mu_apply_seed (mu_snapshot.c): that file
 * already includes the game's random.h, and random.h drags in Runtime/platform.h, whose ssize_t
 * conflicts with the one mu_host.h gets from the Windows SDK's stdint.h when both land in the same
 * translation unit (the same reason mu_entry.c keeps game headers out of itself). This function
 * stays the ABI side, with no game headers, exactly like mu_match_request above it. */
void mu_apply_seed(uint32_t seed);

void mu_seed_request(void)
{
    if (mu_host->version >= 3 && mu_host->mark_match_start != NULL) {
        mu_host->mark_match_start();
    }
    if (mu_host->version >= 3 && mu_host->rng_seed_override != NULL) {
        uint32_t seed = 0;
        int32_t has_value = 0;
        mu_host->rng_seed_override(&seed, &has_value);
        if (has_value) {
            mu_apply_seed(seed);
            if (mu_host->log) mu_host->log("rng-seed: forced (first GS_VS frame)");
        }
    }
}
