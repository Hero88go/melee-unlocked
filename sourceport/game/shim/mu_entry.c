/* The library's entry point, and the host table everything else in the shim calls through. */
#include "mu_host.h"
#include "mu_shim.h"
#include "mu_20xx_cpu.h"

_Static_assert(MU_OPTION_NO_SCREEN_SHAKE == MU_GAME_OPTION_NO_SCREEN_SHAKE, "option bits");
_Static_assert(MU_OPTION_PAL_STOCK_ICONS == MU_GAME_OPTION_PAL_STOCK_ICONS, "option bits");
_Static_assert(MU_OPTION_VANILLA == MU_GAME_OPTION_VANILLA, "option bits");
_Static_assert(MU_OPTION_WIDESCREEN == MU_GAME_OPTION_WIDESCREEN, "option bits");
_Static_assert(MU_OPTION_TRUE_WIDESCREEN == MU_GAME_OPTION_TRUE_WIDESCREEN, "option bits");
_Static_assert(MU_OPTION_LANGUAGE_MASK == MU_GAME_OPTION_LANGUAGE_MASK &&
               MU_OPTION_LANGUAGE_SHIFT == MU_GAME_OPTION_LANGUAGE_SHIFT, "option bits");
_Static_assert(MU_OPTION_TE == MU_GAME_OPTION_TE && MU_OPTION_TE_TOURNAMENT == MU_GAME_OPTION_TE_TOURNAMENT &&
               MU_TE_HOLD_START_PAUSE == MU_GAME_OPTION_TE_HOLD_START_PAUSE &&
               MU_TE_FROZEN_STAGES == MU_GAME_OPTION_TE_FROZEN_STAGES &&
               MU_TE_NO_STAR_KO == MU_GAME_OPTION_TE_NO_STAR_KO &&
               MU_TE_INFINITE_SHIELDS == MU_GAME_OPTION_TE_INFINITE_SHIELDS &&
               MU_TE_TAUNT_CANCEL == MU_GAME_OPTION_TE_TAUNT_CANCEL &&
               MU_TE_UNFREEZE_ENDGAME == MU_GAME_OPTION_TE_UNFREEZE_ENDGAME &&
               MU_TE_FIXED_CAMERA == MU_GAME_OPTION_TE_FIXED_CAMERA &&
               MU_TE_CPU_ZELDA_SHEIK == MU_GAME_OPTION_TE_CPU_ZELDA_SHEIK &&
               MU_TE_HANDICAP_STOCKS == MU_GAME_OPTION_TE_HANDICAP_STOCKS, "20XX TE option bits");
_Static_assert(MU_OPTION_LAB == MU_GAME_OPTION_LAB && MU_OPTION_LAB_BUBBLES == MU_GAME_OPTION_LAB_BUBBLES &&
               MU_OPTION_LAB_BUBBLES_ONLY == MU_GAME_OPTION_LAB_BUBBLES_ONLY, "Training Lab option bits");
_Static_assert(MU_OPTION_LAB_DI_MASK == MU_GAME_OPTION_LAB_DI_MASK &&
               MU_OPTION_LAB_REACT_MASK == MU_GAME_OPTION_LAB_REACT_MASK &&
               MU_OPTION_LAB_DI_SHIFT == MU_GAME_OPTION_LAB_DI_SHIFT &&
               MU_OPTION_LAB_REACT_SHIFT == MU_GAME_OPTION_LAB_REACT_SHIFT &&
               MU_OPTION_LAB_TECH_MASK == MU_GAME_OPTION_LAB_TECH_MASK &&
               MU_OPTION_LAB_TECH_SHIFT == MU_GAME_OPTION_LAB_TECH_SHIFT &&
               MU_OPTION_LAB_LOOP == MU_GAME_OPTION_LAB_LOOP, "Training dummy option bits");

unsigned int mu_game_options(void)
{
    return mu_host && mu_host->game_options ? mu_host->game_options() : 0;
}

unsigned int mu_game_options2(void)
{
    return mu_host && mu_host->version >= 14 && mu_host->game_options2 ? mu_host->game_options2() : 0;
}

_Static_assert(MU_TE2_NEUTRAL_SPAWNS == MU_GAME_OPTION2_TE_NEUTRAL_SPAWNS && MU_TE2_V100 == MU_GAME_OPTION2_TE_V100 &&
               MU_TE2_DL64_QUIET == MU_GAME_OPTION2_TE_DL64_QUIET &&
               MU_TE2_RESET_TOURNAMENT == MU_GAME_OPTION2_TE_RESET_TOURNAMENT &&
               MU_TE2_FROZEN_TOGGLE == MU_GAME_OPTION2_TE_FROZEN_TOGGLE &&
               MU_TE2_SKIP_RESULTS == MU_GAME_OPTION2_TE_SKIP_RESULTS &&
               MU_TE2_RANDOM_MUSIC == MU_GAME_OPTION2_TE_RANDOM_MUSIC &&
               MU_TE2_SHIELD_COLORS == MU_GAME_OPTION2_TE_SHIELD_COLORS &&
               MU_TE2_NO_SCREEN_RUMBLE == MU_GAME_OPTION2_TE_NO_SCREEN_RUMBLE &&
               MU_TE2_LCANCEL_FLASH == MU_GAME_OPTION2_TE_LCANCEL_FLASH &&
               MU_TE2_SPOOF_PLUGINS == MU_GAME_OPTION2_TE_SPOOF_PLUGINS &&
               MU_TE2_LCANCEL_WHEELS == MU_GAME_OPTION2_TE_LCANCEL_WHEELS &&
               MU_TE2_BUBBLES == MU_GAME_OPTION2_TE_BUBBLES &&
               MU_TE2_INPUT_DISPLAY == MU_GAME_OPTION2_TE_INPUT_DISPLAY &&
               MU_TE2_CPU_SMART_DI == MU_GAME_OPTION2_TE_CPU_SMART_DI &&
               MU_TE2_COLOR_OVERLAYS == MU_GAME_OPTION2_TE_COLOR_OVERLAYS &&
               MU_TE2_HANDWARMERS == MU_GAME_OPTION2_TE_HANDWARMERS &&
               MU_TE2_STAGE_STRIKE == MU_GAME_OPTION2_TE_STAGE_STRIKE &&
               MU_TE2_20XX_CPUS == MU_GAME_OPTION2_TE_20XX_CPUS, "20XX TE second word bits");

unsigned int mu_game_options3(void)
{
    return mu_host && mu_host->version >= 17 && mu_host->game_options3 ? mu_host->game_options3() : 0;
}

/* Display-only options, read live (a host before version 18 has none). */
unsigned int mu_display_options(void)
{
    return mu_host && mu_host->version >= 18 && mu_host->display_options ? mu_host->display_options() : 0;
}

int mu_low_poly_fighters(void)
{
    return (mu_display_options() & MU_DISPLAY_LOW_POLY) != 0;
}

_Static_assert(MU_DISPLAY_LOW_POLY == MU_DISPLAY_OPTION_LOW_POLY, "display option bits");

_Static_assert(MU_CPU_TECH_MASK == MU_GAME_OPTION3_CPU_TECH_MASK && MU_CPU_TECH_SHIFT == MU_GAME_OPTION3_CPU_TECH_SHIFT &&
               MU_CPU_GETUP_MASK == MU_GAME_OPTION3_CPU_GETUP_MASK &&
               MU_CPU_GETUP_SHIFT == MU_GAME_OPTION3_CPU_GETUP_SHIFT &&
               MU_CPU_DI_MASK == MU_GAME_OPTION3_CPU_DI_MASK && MU_CPU_DI_SHIFT == MU_GAME_OPTION3_CPU_DI_SHIFT &&
               MU_CPU_SDI_MASK == MU_GAME_OPTION3_CPU_SDI_MASK && MU_CPU_SDI_SHIFT == MU_GAME_OPTION3_CPU_SDI_SHIFT &&
               MU_CPU_NO_TAUNT == MU_GAME_OPTION3_CPU_NO_TAUNT && MU_CPU_LCANCEL == MU_GAME_OPTION3_CPU_LCANCEL &&
               MU_CPU_NO_RAPID_JAB == MU_GAME_OPTION3_CPU_NO_RAPID_JAB &&
               MU_CPU_NO_TRANSFORM == MU_GAME_OPTION3_CPU_NO_TRANSFORM && MU_CPU_ALL == MU_GAME_OPTION3_CPU_ALL,
               "CPU training option bits");

unsigned int mu_mod_flags(void)
{
    return mu_host && mu_host->mod_flags ? mu_host->mod_flags() : 0;
}

unsigned int mu_music_volume(void)
{
    unsigned int level = mu_host && mu_host->music_volume ? mu_host->music_volume() : 100;
    return level > 100 ? 100 : level;
}

/* Whether [p, p + size) lies in the game's memory (MEM1 at its console address): the per-retrace
 * snapshot's check before it follows a fighter pointer, as the recompiled build's reader does. */
int mu_is_game_memory(const void* p, unsigned long size)
{
    const uintptr_t lo = 0x80000000u, hi = lo + (mu_host ? mu_host->mem1_size() : 0);
    const uintptr_t a = (uintptr_t) p;
    return a >= lo && a < hi && size <= hi - a;
}

int mu_native_owner_tracking(void)
{
    return mu_host && mu_host->native_owner_tracking ? mu_host->native_owner_tracking() != 0 : 0;
}

const MuHostApi* mu_host;

static int32_t mu_run(void);
static void mu_retrace_from_host(void);
void mu_fire_alarms(uint64_t now);
void mu_ai_dma_done(void);
void mu_state_snapshot_words(uint32_t* out);
void mu_lcancel_view(MuLcancelView* out);

/* The linker bounds are game-owned writable image sections. Keep the live stack and the host's
 * machine state outside this list: a future rollback driver must capture those explicitly. */
extern char __data_start__[], __data_end__[], __bss_start__[], __bss_end__[];

static uint32_t mu_state_regions(MuStateRegion* out, uint32_t capacity)
{
    MuStateRegion regions[3] = {
        {(void*) 0x80000000u, mu_host->mem1_size()},
        {__data_start__, (uint32_t) (__data_end__ - __data_start__)},
        {__bss_start__, (uint32_t) (__bss_end__ - __bss_start__)},
    };
    uint32_t i;
    if (out)
        for (i = 0; i < capacity && i < 3; ++i)
            out[i] = regions[i];
    return 3;
}

/* shim/mu_exclusions.c: what a rollback snapshot covers and leaves out. */
uint32_t mu_snapshot_exclusions(MuStateRegion* out, uint32_t capacity);
uint32_t mu_snapshot_ranges(MuStateRegion* out, uint32_t capacity);
int mu_practice_bridge(int op, int* args, int count);
_Static_assert(MU_PRACTICE_DIRECT_FIRST_MATCH == 9 && MU_PRACTICE_REQUEST_MAJOR == 6, "practice op numbering (mu_practice.c)");

/* Native game-side Slippi hooks use this instead of the console's EXI memory-mapped device. */
int32_t mu_slippi_command(uint8_t command, const uint8_t* payload, uint32_t payload_size,
                          uint8_t* response, uint32_t response_capacity, uint32_t* response_size)
{
    if (response_size) *response_size = 0;
    if (!mu_host || !mu_host->slippi_command) return -1;
    return mu_host->slippi_command(command, payload, payload_size,
                                   response, response_capacity, response_size);
}

static void mu_state_snapshot(MuStatePod* out)
{
    _Static_assert(sizeof(MuStatePod) == 76 * sizeof(uint32_t), "state POD layout");
    mu_state_snapshot_words((uint32_t*) out);
}

/* The game's own main (gm/gmmain.c). On the console __start ran it after bringing up the C runtime;
 * natively the host has done that part. */
int main(void);
void __sinit_trigf_c(void);

__declspec(dllexport) int32_t mu_game_entry(const MuHostApi* host, MuGameApi* game)
{
    if (!host || host->version != MU_HOST_API_VERSION || !game)
        return -1;
    /* The console runs the .ctors table before main. The native DLL loader does not run this
     * console-specific SECTION_CTORS table, so initialize MSL's range-reduction constants here.
     * Without them HSD_MtxSRT's sinf/cosf disagree with the authored-pose sampler. */
    __sinit_trigf_c();
    mu_host = host;
    game->version = MU_GAME_API_VERSION;
    game->run = mu_run;
    game->retrace = mu_retrace_from_host;
    game->fire_alarms = mu_fire_alarms;
    game->ai_dma_done = mu_ai_dma_done;
    game->state_snapshot = mu_state_snapshot;
    game->lcancel_view = mu_lcancel_view;
    game->state_regions = mu_state_regions;
    game->state_exclusions = mu_snapshot_exclusions;
    game->snapshot_ranges = mu_snapshot_ranges;
    game->practice = (int32_t (*)(int32_t, int32_t*, int32_t)) mu_practice_bridge;
    return 0;
}


void mu_fill_from_dol(void);

/* M6 evidence: the stock icon's texture frame for every character, fighter variant and costume
 * (gm_80168B34, a pure function), logged so it can be compared with the translated game's
 * (port_native_parity --stock-icon-map). Off unless MELEE_DUMP_STOCK_ICON_MAP is set. */
float gm_80168B34(int ckind, int arg1, int costume);
char* getenv(const char* name);
int snprintf(char* dst, __SIZE_TYPE__ size, const char* format, ...);
static void mu_dump_stock_icon_map(void)
{
    int ckind, variant, costume;
    if (!getenv("MELEE_DUMP_STOCK_ICON_MAP"))
        return;
    for (ckind = 0; ckind < 34; ++ckind)
        for (variant = 0; variant < 33; ++variant)
            for (costume = 0; costume < 6; ++costume) {
                char line[96];
                float frame = gm_80168B34(ckind, variant, costume);
                unsigned bits;
                __builtin_memcpy(&bits, &frame, sizeof bits);
                snprintf(line, sizeof line, "[stock-icon-map] %d %d %d %08X", ckind, variant, costume, bits);
                mu_host->log(line);
            }
}

static int32_t mu_run(void)
{
    mu_fill_from_dol();   /* the font atlases the sources take from the retail binary */
    mu_dump_stock_icon_map();
    mu_general_codes_boot();   /* before main: Legacy applies its code set ahead of the game */
    return main();
}

/* Diagnostic (M4 per-voice evidence), off unless MELEE_TEST_EARLY_RNG_SEED="<seed>@<retrace>" is
 * set: writes the seed at the start of that retrace, before the game's retrace work, so both engines
 * reach the pre-match scenes with the same RNG. The recompiled build writes the same seed at the
 * same point (host.cpp retrace()). --rng-seed alone pins the RNG only from the VS item-setup
 * boundary, so random choices made before it (the sound files the scene after the title loads)
 * otherwise differ between the engines. */
char* getenv(const char* name);
unsigned long strtoul(const char* text, char** end, int base);
void mu_apply_seed(uint32_t seed);
unsigned mu_native_retrace_count(void);

static void mu_early_rng_seed(void)
{
    static int parsed, enabled;
    static uint32_t seed;
    static unsigned at;
    if (!parsed) {
        const char* text = getenv("MELEE_TEST_EARLY_RNG_SEED");
        char* end = 0;
        parsed = 1;
        if (text) {
            seed = (uint32_t) strtoul(text, &end, 0);
            if (end && *end == '@') {
                at = (unsigned) strtoul(end + 1, &end, 0);
                enabled = end && *end == '\0' && at != 0;
            }
        }
    }
    if (!enabled || mu_native_retrace_count() != at)
        return;
    mu_apply_seed(seed);
    if (mu_host->log) {
        char line[96];
        snprintf(line, sizeof line, "rng-seed: early %08X at retrace %u", (unsigned) seed, at);
        mu_host->log(line);
    }
}

static void mu_retrace_from_host(void)
{
    mu_early_rng_seed();
    mu_vi_retrace();
}

/* GX hands its command bytes here (gxnative/GXFifo_native.c). */
void mu_host_gx_fifo_bytes(const unsigned char* data, unsigned long size)
{
    mu_host->gx_fifo((const uint8_t*) data, (uint32_t) size);
}

/* Optional diagnostic hook used by HSD_PObjDisp without exposing the host ABI header to HSD
 * sources, whose game platform.h defines a conflicting ssize_t. */
int mu_native_render_scope_enabled(void)
{
    return mu_host != NULL && (mu_host->native_render_scope_event != NULL ||
                               (mu_host->native_draw_identity != NULL && mu_host->native_draw_identity()));
}

void mu_native_render_scope_event(unsigned int sequence, unsigned int scope_id,
                                  unsigned int phase)
{
    MuNativeRenderScopeEvent event;
    if (!mu_host || !mu_host->native_render_scope_event)
        return;
    event.sequence = sequence;
    event.scope_id = scope_id;
    event.phase = phase;
    event.reserved = 0;
    mu_host->native_render_scope_event(&event);
}

/* Native authored-pose callback. The host copies all borrowed POD arrays before returning. */
int mu_native_pose_capture_enabled(void)
{
    return mu_host != NULL && mu_host->native_pose_capture_enabled != NULL &&
           mu_host->native_pose_capture_enabled();
}

void mu_native_pose_snapshot(const MuNativePoseSnapshot* snapshot)
{
    if (mu_host != NULL && mu_host->native_pose_snapshot != NULL)
        mu_host->native_pose_snapshot(snapshot);
}
