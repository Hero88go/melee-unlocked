/* Exercise the real preload pinning, fragmented heap compactor and DAT lookup.
 * Only hardware alarms/interrupts are substituted. */
#include "melee/lb/lbdvd.c"
#ifndef PRELOAD_COMPACTOR_SOURCE
#define PRELOAD_COMPACTOR_SOURCE "melee/lb/lbmemory.c"
#endif
#include PRELOAD_COMPACTOR_SOURCE
#include "sysdolphin/baselib/archive.h"

extern int printf(const char*, ...);
static u8 arena[0x100000] __attribute__((aligned(32)));
static OSAlarm* pending;
static OSAlarmHandler pending_handler;
static int failures, completions;

BOOL OSDisableInterrupts(void) { return 1; }
BOOL OSRestoreInterrupts(BOOL level) { return level; }
void OSCreateAlarm(OSAlarm* alarm) { (void) alarm; }
void OSSetAlarm(OSAlarm* alarm, OSTime ticks, OSAlarmHandler handler)
{
    (void) ticks;
    pending = alarm;
    pending_handler = handler;
}
void OSReport(char* fmt, ...) { (void) fmt; }
void __assert(const char* file, u32 line, const char* expression)
{
    printf("unexpected assert: %s:%d %s\n", file, line, expression);
    __builtin_trap();
}
void mu_addr32_failed(const void* p, const char* file, int line)
{
    (void) p; (void) file; (void) line;
    __builtin_trap();
}
int HSD_DevComRequest(int a, uintptr_t b, uintptr_t c, size_t d, int e,
                     int f, HSD_DevComCallback g, uintptr_t h)
{
    (void) a; (void) b; (void) c; (void) d;
    (void) e; (void) f; (void) g; (void) h;
    __builtin_trap(); /* This fixture uses RAM, never ARAM. */
}
static void check(int ok, const char* why)
{
    if (!ok) { printf("FAIL: %s\n", why); failures++; }
}
static void completed(u32 arg) { check(arg == 42, "completion argument"); completions++; }
static void compact(Handle* heap)
{
    int expected = completions;
    int guard = 100;
    if (lbMemory_8001529C(heap, completed, 42)) {
        while (pending_handler && --guard) {
            OSAlarmHandler handler = pending_handler;
            pending_handler = NULL;
            handler(pending, NULL);
        }
        check(guard && completions == expected + 1, "asynchronous compaction finishes once");
    }
}
static void scenario(int layout)
{
    Handle heap = { 0 };
    HSD_AllocEntry *prefix, *gap, *gap_after, *lead, *raw, *metadata, *tail, *replacement;
    HSD_Archive* archive;
    HSD_ArchiveHeader* header;
    HSD_ArchivePublicInfo* public;
    HSD_ArchiveRelocationInfo* reloc;
    void *raw_before, *meta_before, *tail_before;
    u32 i;
    memset(&lbMemory_804318B0, 0, sizeof(lbMemory_804318B0));
    memset(&preloadCache, 0, sizeof(preloadCache));
    memset(arena, 0, sizeof(arena));
    for (i = 0; i + 1 < ARRAY_SIZE(_p(mem)); i++) _p(mem)[i].next = &_p(mem)[i + 1];
    _p(free_mem) = _p(mem);
    heap.lo = arena;
    heap.hi = arena + sizeof(arena);
    prefix = lbMemory_80014FC8(&heap, 0x200);
    lead = lbMemory_80014FC8(&heap, 0x38000);
    memset(lead->addr, 0x93, lead->size);
    raw = lbMemory_80014FC8(&heap, 0x200);
    gap = lbMemory_80014FC8(&heap, 0x200);
    metadata = lbMemory_80014FC8(&heap, sizeof(HSD_Archive));
    gap_after = lbMemory_80014FC8(&heap, 0x200);
    tail = lbMemory_80014FC8(&heap, 0x38000);
    memset(tail->addr, 0xA7, tail->size);
    header = raw->addr;
    header->file_size = 0x200;
    header->data_size = 64;
    header->nb_reloc = 1;
    header->nb_public = 1;
    reloc = (void*) ((u8*) raw->addr + 96);
    reloc->offset = 0;
    public = (void*) ((u8*) raw->addr + 100);
    public->offset = 8;
    public->symbol = 0;
    strcpy((char*) raw->addr + 108, "ftDataFixture");
    ((be_u32*) ((u8*) raw->addr + 32))->v = 16;
    archive = metadata->addr;
    check(HSD_ArchiveParse(archive, raw->addr, raw->size) == 0, "valid fighter DAT parses");
    preloadCache.entries[0].state = layout & 1 ? 3 : 4;
    preloadCache.entries[0].load_state = 2;
    preloadCache.entries[0].raw_data = raw;
    preloadCache.entries[0].archive = metadata;
    /* Cached parsed archives still pin their addresses with negative scores. */
    preloadCache.entries[0].load_score = layout & 1 ? -4 : 4;
    raw_before = raw->addr;
    meta_before = metadata->addr;
    tail_before = tail->addr;
    lbMemFreeToHeap(&heap, prefix->addr);
    lbMemFreeToHeap(&heap, gap->addr);
    lbMemFreeToHeap(&heap, gap_after->addr);
    compact(&heap);
    check(raw->addr == raw_before, "parsed DAT retains its raw allocation address");
    check(metadata->addr == meta_before, "parsed archive retains its descriptor address");
    check(tail->addr < tail_before, "unparsed data still compacts around pinned blocks");
    check(lead->addr == arena, "unparsed data before an archive still compacts");
    for (i = 0; i < lead->size; i++) {
        if (((u8*) lead->addr)[i] != 0x93) {
            check(0, "overlapping move before a pinned block preserves every byte");
            break;
        }
    }
    for (i = 0; i < tail->size; i++) {
        if (((u8*) tail->addr)[i] != 0xA7) {
            check(0, "overlapping multi-slice move preserves every byte");
            break;
        }
    }
    replacement = lbMemory_80014FC8(&heap, 0x200);
    memset(replacement->addr, 0xCC, replacement->size);
    if (raw->addr == raw_before && metadata->addr == meta_before) {
        check(HSD_ArchiveGetPublicAddress(archive, "ftDataFixture") == (u8*) raw->addr + 40,
              "fighter symbol lookup survives compaction and replacement file loading");
        check(((be_u32*) archive->data)->v == mu_addr32(archive->data + 16),
              "relocated internal data pointers remain valid");
    }
    compact(&heap);
    check(raw->addr == raw_before && metadata->addr == meta_before,
          "repeated compaction preserves all live archive references");
    /* Releasing the preload entry removes its pins; memory remains reclaimable. */
    preloadCache.entries[0].state = 0;
    lbMemFreeToHeap(&heap, replacement->addr);
    compact(&heap);
    check(raw->addr != raw_before, "released archives stop pinning heap blocks");
}
int main(void)
{
    int i;
    for (i = 0; i < 20; i++) scenario(i);
    printf("native preload compaction: %d failures\n", failures);
    return failures ? 1 : 0;
}
