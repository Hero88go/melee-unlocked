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
