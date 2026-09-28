/* The processor's caches and special registers. The host's memory is coherent, so flushing and
 * invalidating do nothing; the locked cache the video decoder works in is 16 KB the host maps at the
 * address the console had it (0xE0000000), so the decoder's own pointers to it stay valid. */
#include <dolphin/os.h>

#include "mu_shim.h"

void DCFlushRange(void* addr, u32 nBytes) { (void) addr; (void) nBytes; }
void DCFlushRangeNoSync(void* addr, u32 nBytes) { (void) addr; (void) nBytes; }
void DCStoreRange(void* addr, u32 nBytes) { (void) addr; (void) nBytes; }
void DCInvalidateRange(void* addr, u32 nBytes) { (void) addr; (void) nBytes; }
void DCZeroRange(void* addr, u32 nBytes) { __builtin_memset(addr, 0, nBytes); }

void LCEnable(void) {}
/* Locked-cache DMA runs to completion when it is queued, so there is never anything to wait for. */
void LCQueueWait(u32 len) { (void) len; }

/* The console queued the copy in 4 KB pieces and returned how many it used. */
u32 LCStoreData(void* dest, void* src, u32 nBytes)
{
    __builtin_memcpy(dest, src, nBytes);
    return (nBytes + 4095) / 4096;
}

/* Special registers the game reads for the locked cache and write gathering. */
static u32 mu_hid2, mu_wpar, mu_msr = 0x00009032u;   /* MSR as the game finds it: EE, FP, IR, DR, RI */
u32 PPCMfhid2(void) { return mu_hid2; }
u32 PPCMfmsr(void) { return mu_msr; }
void PPCMtmsr(u32 value) { mu_msr = value; }
u32 PPCMfwpar(void) { return mu_wpar; }
void PPCMtwpar(u32 value) { mu_wpar = value; }
void PPCSync(void) {}
