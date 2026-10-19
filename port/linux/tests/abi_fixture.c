// A deliberately small native ELF game for testing the real loader and ABI.
// This is not a substitute for booting or validating the actual game.
// SPDX-License-Identifier: GPL-2.0-or-later
#include "mu_host.h"
#include <string.h>

static const MuHostApi* machine;
static unsigned retraces;
static int completed;
static uint32_t file_length;
static uint8_t header[32];
static void disc_done(int32_t result,void* unused) {
    (void) unused;
    completed=result==sizeof header ? 1 : -1;
}
static void state(MuStatePod* out) {
    memset(out,0,sizeof *out);
    out->rng=0xBADC0DEu;
    out->scene_major=2;
    out->scene=3;
    out->match_frame=retraces;
}
static void tick(void) { ++retraces; }
static void alarm(uint64_t now) { (void) now; }
static void nothing(void) {}
static int32_t practice(int32_t op,int32_t* args,int32_t count) {
    (void) op; (void) args; (void) count; return 0;
}
static uint32_t regions(MuStateRegion* out,uint32_t capacity) {
    if (out && capacity) { out[0].address=(void*)0x80000000u; out[0].size=machine->mem1_size(); }
    return 1;
}
static uint32_t exclusions(MuStateRegion* out,uint32_t capacity) { (void) out; (void) capacity; return 0; }
static int32_t run(void) {
    const int32_t entry=machine->disc_entrynum("/LbRb.dat");
    uint32_t start;
    if (entry<0 || !machine->disc_file(entry,&start,&file_length)) return 31;
    machine->disc_read(start,header,sizeof header,disc_done,0);
    while (!completed) machine->poll();
    if (completed<0) return 32;
    if (((uint32_t)header[0]<<24 | (uint32_t)header[1]<<16 | (uint32_t)header[2]<<8 | header[3])!=file_length) return 33;
    while (retraces<20) machine->vi_wait_retrace();
    machine->log("native ELF ABI fixture: DVD/read interrupt and retrace checks passed");
    return 0;
}
__attribute__((visibility("default"))) int32_t mu_game_entry(const MuHostApi* host,MuGameApi* game) {
    if (!host || host->version!=MU_HOST_API_VERSION || !game) return -1;
    machine=host;
    memset(game,0,sizeof *game);
    game->version=MU_GAME_API_VERSION;
    game->run=run; game->retrace=tick; game->fire_alarms=alarm;
    game->ai_dma_done=nothing; game->state_snapshot=state;
    game->state_regions=regions; game->state_exclusions=exclusions;
    game->snapshot_ranges=regions; game->practice=practice;
    return 0;
}
