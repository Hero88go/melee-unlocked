/* Slippi replay playback, the ABI side (see mu_replay.c for the game side and
 * run-source/slp-playback-20260925/design.md for what Slippi's playback codes do).
 *
 * The host parses the .slp and answers what those codes ask Dolphin for over EXI: the game info
 * block, one frame's inputs and seed, whether a dead teams player stole a stock. This file only moves
 * that data across the host boundary in plain C types; game headers stay out of it, as in mu_match.c
 * (random.h's ssize_t collides with the one mu_host.h's stdint.h brings in).
 */
#include "mu_host.h"
#include "mu_shim.h"

static const MuReplayStart* start_info;
static int start_checked;
static MuReplayFrame frame_data;
static int frame_fetched;

int mu_replay_abi_active(void)
{
    if (!start_checked) {
        start_checked = 1;
        if (mu_host->version >= 9 && mu_host->replay_start != NULL) {
            start_info = mu_host->replay_start();
        }
    }
    return start_info != NULL;
}

unsigned int mu_replay_abi_codes(void)
{
    return mu_replay_abi_active() ? start_info->codes : 0;
}

/* The 0x138-byte game info block (big-endian, console layout) and the match's starting seed. */
const unsigned char* mu_replay_abi_game_info(unsigned int* seed)
{
    if (!mu_replay_abi_active()) {
        return 0;
    }
    *seed = start_info->random_seed;
    return start_info->game_info;
}

int mu_replay_abi_resync(void)
{
    return mu_replay_abi_active() && start_info->resync;
}

int mu_replay_abi_ps_frozen_toggle(void)
{
    return mu_replay_abi_active() ? start_info->ps_frozen_toggle : 0;
}

/* The replay was played with Frozen Pokemon Stadium (game start isFrozenPS): Slippi's playback
 * applies FreezePokemon (801D45FC: b to the function exit), so no transformation ever starts. */
int mu_replay_ps_frozen(void)
{
    return mu_replay_abi_active() && start_info->frozen_ps;
}

/* Fetches frame `frame` (CMD_GET_FRAME). Returns MU_REPLAY_CONTINUE or MU_REPLAY_TERMINATE. */
int mu_replay_abi_fetch(int frame)
{
    mu_host->replay_frame(frame, &frame_data);
    frame_fetched = 1;
    return frame_data.result;
}

/* The last fetch's result, as Slippi's EXI buffer keeps it until the next fetch. */
int mu_replay_abi_last_result(void)
{
    return frame_fetched ? frame_data.result : MU_REPLAY_WAIT;
}

/* The last fetched frame's starting seed; zero when the frame had none. */
int mu_replay_abi_frame_seed(unsigned int* seed)
{
    if (!frame_fetched || !frame_data.seed_exists) {
        return 0;
    }
    *seed = frame_data.seed;
    return 1;
}

/* One character's record in the last fetched frame. f receives joystick x, y, c-stick x, y,
 * trigger, x, y, facing and percent; raw the joystick and c-stick bytes. Zero when absent. */
int mu_replay_abi_character(int port, int follower, float* f, unsigned int* buttons,
                            unsigned int* action_state, unsigned int* seed, unsigned char* raw)
{
    const MuReplayCharacter* c;

    if (!frame_fetched || port < 0 || port > 3) {
        return 0;
    }
    c = &frame_data.character[port][follower ? 1 : 0];
    f[0] = c->joystick_x;
    f[1] = c->joystick_y;
    f[2] = c->cstick_x;
    f[3] = c->cstick_y;
    f[4] = c->trigger;
    f[5] = c->x;
    f[6] = c->y;
    f[7] = c->facing;
    f[8] = c->percent;
    *buttons = c->buttons;
    *action_state = c->action_state;
    *seed = c->random_seed;
    raw[0] = c->joystick_x_raw;
    raw[1] = c->joystick_y_raw;
    raw[2] = c->cstick_x_raw;
    raw[3] = c->cstick_y_raw;
    return c->present != 0;
}

int mu_replay_abi_stock_steal(int frame, int port)
{
    return mu_host->replay_stock_steal(frame, port) != 0;
}

void mu_replay_abi_event(unsigned int command, const unsigned char* payload, unsigned int size)
{
    mu_host->replay_event((uint8_t) command, payload, size);
}

void mu_replay_abi_finished(void)
{
    mu_host->replay_finished();
}

void mu_replay_abi_log(const char* text)
{
    if (mu_host->log) {
        mu_host->log(text);
    }
}

_Static_assert(MU_RC_UCF084 == MU_REPLAY_CODE_UCF084 && MU_RC_PREVENT_WOBBLING == MU_REPLAY_CODE_PREVENT_WOBBLING &&
               MU_RC_PS_MONITOR == MU_REPLAY_CODE_PS_MONITOR && MU_RC_DEAD_UP_FALL == MU_REPLAY_CODE_DEAD_UP_FALL,
               "mu_native.h MU_RC_* must match mu_host.h MU_REPLAY_CODE_*");

#ifdef MU_NATIVE
/* Rollback snapshot exclusions: host plumbing, not game state (the host's replay data cache). */
MU_EXCLUSIONS(replay_abi,
              MU_EXCLUDE(start_info),
              MU_EXCLUDE(start_checked),
              MU_EXCLUDE(frame_data),
              MU_EXCLUDE(frame_fetched))
#endif
