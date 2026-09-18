/* The library's entry point, and the host table everything else in the shim calls through. */
#include "mu_host.h"
#include "mu_shim.h"

const MuHostApi* mu_host;

static int32_t mu_run(void);
static void mu_retrace_from_host(void);
void mu_fire_alarms(uint64_t now);
void mu_ai_dma_done(void);

/* The game's own main (gm/gmmain.c). On the console __start ran it after bringing up the C runtime;
 * natively the host has done that part. */
int main(void);

__declspec(dllexport) int32_t mu_game_entry(const MuHostApi* host, MuGameApi* game)
{
    if (!host || host->version != MU_HOST_API_VERSION || !game)
        return -1;
    mu_host = host;
    game->version = MU_GAME_API_VERSION;
    game->run = mu_run;
    game->retrace = mu_retrace_from_host;
    game->fire_alarms = mu_fire_alarms;
    game->ai_dma_done = mu_ai_dma_done;
    return 0;
}

static int32_t mu_run(void)
{
    return main();
}

static void mu_retrace_from_host(void)
{
    mu_vi_retrace();
}

/* GX hands its command bytes here (gxnative/GXFifo_native.c). */
void mu_host_gx_fifo_bytes(const unsigned char* data, unsigned long size)
{
    mu_host->gx_fifo((const uint8_t*) data, (uint32_t) size);
}
