/* What a rollback snapshot covers, and what it leaves out.
 *
 * Covered: all of console main memory (the main heap, the persistent Stay and preload heaps and
 * their allocators' bookkeeping, which the game can touch during a match) and the game image's
 * writable sections. Left out: the state each owning file names with MU_EXCLUSIONS (audio driver
 * and mixer state, the renderer bridge, host plumbing) and the online bookkeeping itself. This is
 * the native form of what a console savestate saves and skips.
 */
#include "mu_host.h"
#include "mu_shim.h"

typedef unsigned int (*MuExclusionList)(const MuExclusion** out);

#define PROVIDERS(X)                                                                               \
    X(ax_vpb) X(ax_alloc) X(ax_aux) X(ax_cl) X(ax_out) X(ax_spb) X(ax_prof) X(synth) X(axdriver)  \
    X(lbaudio_reverb) X(video) X(perf) X(pobj_bridge) X(gx_regs) X(gx_fifo) X(os) X(card) X(audio) \
    X(replay_abi) X(lab) X(content)

#define DECLARE(name) unsigned int mu_exclusions_##name(const MuExclusion** out);
PROVIDERS(DECLARE)
#undef DECLARE

#define ENTRY(name) mu_exclusions_##name,
static const MuExclusionList providers[] = { PROVIDERS(ENTRY) };
#undef ENTRY

uint32_t mu_online_state_exclusions(MuStateRegion* out, uint32_t capacity);

_Static_assert(sizeof(MuExclusion) == sizeof(MuStateRegion), "exclusion layout");

uint32_t mu_snapshot_exclusions(MuStateRegion* out, uint32_t capacity)
{
    uint32_t count = 0;
    unsigned int p, i, n;
    for (p = 0; p < sizeof providers / sizeof providers[0]; p++) {
        const MuExclusion* list;
        n = providers[p](&list);
        for (i = 0; i < n; i++, count++) {
            if (out != NULL && count < capacity) {
                out[count].address = list[i].address;
                out[count].size = list[i].size;
            }
        }
    }
    count += mu_online_state_exclusions(out != NULL && count < capacity ? out + count : NULL,
                                        capacity > count ? capacity - count : 0);
    return count;
}

extern char __data_start__[], __data_end__[], __bss_start__[], __bss_end__[];

/* The capture set. Main memory is captured whole: besides the main heap, the persistent heaps'
 * allocators change during a match (measured: a block list in the Stay heap), and a load must
 * never leave them out of step with the game's own records of what they hold. */
void HSD_GetNextArena(void** lo, void** hi);

uint32_t mu_snapshot_ranges(MuStateRegion* out, uint32_t capacity)
{
    MuStateRegion ranges[3];
    uint32_t count = 0, i;

    /* Diagnostic (MELEE_SNAPSHOT_HEAP_ONLY=1): only the scene's main heap, the previous capture set. */
    static int heap_only = -1;
    if (heap_only < 0) {
        char* getenv(const char* name);
        const char* v = getenv("MELEE_SNAPSHOT_HEAP_ONLY");
        heap_only = v != NULL && v[0] == '1';
    }
    {
        void* lo = NULL;
        void* hi = NULL;
        char line[96];
        int snprintf(char* buffer, __SIZE_TYPE__ size, const char* format, ...);
        HSD_GetNextArena(&lo, &hi);
        snprintf(line, sizeof line, "snapshot: main heap %08X-%08X", (unsigned int) (uintptr_t) lo,
                 (unsigned int) (uintptr_t) hi);
        mu_host->log(line);
    }
    if (heap_only) {
        void* lo = NULL;
        void* hi = NULL;
        HSD_GetNextArena(&lo, &hi);
        ranges[count].address = lo;
        ranges[count].size = (uint32_t) ((uintptr_t) hi - (uintptr_t) lo);
    } else {
        ranges[count].address = (void*) (uintptr_t) 0x80000000u;
        ranges[count].size = mu_host->mem1_size();
    }
    count++;
    ranges[count].address = __data_start__;
    ranges[count].size = (uint32_t) (__data_end__ - __data_start__);
    count++;
    ranges[count].address = __bss_start__;
    ranges[count].size = (uint32_t) (__bss_end__ - __bss_start__);
    count++;
    for (i = 0; out != NULL && i < count && i < capacity; i++) {
        out[i] = ranges[i];
    }
    return count;
}
