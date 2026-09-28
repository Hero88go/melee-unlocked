/* The host side of native online play, kept apart from the game headers (mu_online.c) as
 * mu_replay_abi.c is for replays. */
#include "mu_host.h"
#include "mu_shim.h"

int32_t mu_slippi_command(uint8_t command, const uint8_t* payload, uint32_t payload_size,
                          uint8_t* response, uint32_t response_capacity, uint32_t* response_size);

int mu_online_abi_command(unsigned int command, const unsigned char* payload, unsigned int size,
                          unsigned char* response, unsigned int capacity, unsigned int* response_size)
{
    return (int) mu_slippi_command((uint8_t) command, payload, size, response, capacity,
                                   (uint32_t*) response_size);
}

void mu_online_abi_resim_phase(int entering)
{
    if (mu_host->version >= 13 && mu_host->resim_phase != NULL) {
        mu_host->resim_phase(entering);
    }
}

/* Nonzero when this run is an online test match; the game then boots straight into it. */
int mu_online_abi_test_match(int* mode, int* input_port)
{
    MuOnlineMatch match;
    int kind;
    if (mu_host->version < 13 || mu_host->online_test_match == NULL) {
        return 0;
    }
    kind = mu_host->online_test_match(&match);
    if (!kind) return 0;
    *mode = match.mode;
    *input_port = match.local_port;
    return kind;
}

void* mu_online_state_address(void);
unsigned int mu_online_state_size(void);

/* Appends this module's snapshot exclusions; returns how many it has. */
void* mu_online_audio_state_address(void);
unsigned int mu_online_audio_state_size(void);

uint32_t mu_online_state_exclusions(MuStateRegion* out, uint32_t capacity)
{
    /* The online bookkeeping and the online sound log: a load must restore neither. */
    if (out != NULL && capacity >= 1) {
        out[0].address = mu_online_state_address();
        out[0].size = mu_online_state_size();
    }
    if (out != NULL && capacity >= 2) {
        out[1].address = mu_online_audio_state_address();
        out[1].size = mu_online_audio_state_size();
    }
    return 2;
}

unsigned int mu_online_abi_retrace_count(void)
{
    return mu_host->vi_retrace_count();
}

void mu_online_abi_log(const char* text)
{
    mu_host->log(text);
}
