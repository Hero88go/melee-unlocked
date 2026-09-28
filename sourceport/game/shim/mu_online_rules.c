/* Per-match online rules the gameplay codes read (set at online match start, constant for the
 * match, so plain statics are rollback safe). */
#include "mu_native.h"

int mu_replay_abi_ps_frozen_toggle(void);

static int rules_mode = -1;
static int rules_frozen_stadium;
static int rules_is_teams;
static int rules_local_port = -1;

void mu_online_rules_set(int mode, int frozen_stadium, int is_teams, int local_port)
{
    rules_mode = mode;
    rules_frozen_stadium = frozen_stadium != 0;
    if (mu_slippi_menus_enabled()) {
        /* InitOnlinePlay copies the match's alt stage mode into the toggle byte the SSS shows */
        mu_slippi_state()->frozen_toggle = (unsigned char) rules_frozen_stadium;
    }
    rules_is_teams = is_teams != 0;
    rules_local_port = local_port;
}

void mu_online_rules_clear(void)
{
    rules_mode = -1;
    rules_frozen_stadium = 0;
    rules_is_teams = 0;
    rules_local_port = -1;
}

int mu_online_rules_mode(void)
{
    return rules_mode;
}

int mu_online_frozen_stadium(void)
{
    return rules_frozen_stadium;
}

int mu_online_rules_is_teams(void)
{
    return rules_is_teams;
}

int mu_online_rules_local_port(void)
{
    return rules_local_port;
}

/* IngameCheckIfFrozen toggle byte: the match-state byte online, the SSS toggle offline with the
 * Slippi menus on (the cave byte Legacy reads), the replay's value in playback. */
int mu_ps_frozen_toggle(void)
{
    if (mu_online_active()) {
        return rules_frozen_stadium;
    }
    if (mu_slippi_menus_enabled()) {
        return mu_slippi_sss_frozen();
    }
    return mu_replay_abi_ps_frozen_toggle();
}
