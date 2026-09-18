/* Shared between the shim's own translation units. Game code never includes this. */
#ifndef MU_SHIM_H
#define MU_SHIM_H

#include "mu_host.h"

extern const MuHostApi* mu_host;

/* From the host's C runtime. Declared here because the game's own library (MSL) has no vsnprintf, and
 * the host's stdio.h collides with the game's. */
int vsnprintf(char* buffer, __SIZE_TYPE__ size, const char* format, __builtin_va_list args);

/* The retrace the host drives: runs the game's VI callbacks and its alarms. */
void mu_vi_retrace(void);
/* Interrupts, which the game uses as a critical section around its own data. While they are off
 * the shim defers anything the host would deliver. */
int mu_interrupts_enabled(void);
void mu_deliver_pending(void);
/* Queues fn(a, b) to run as an interrupt would, the next time interrupts are on. */
typedef void (*MuEventFn)(void* a, intptr_t b);
void mu_post(MuEventFn fn, void* a, intptr_t b);
/* One trip to the host, then whatever it delivered. Every wait in the shim goes through this. */
void mu_poll(void);

/* The console's physical addresses: the game hands some hardware a pointer with the cached-segment
 * bit taken off (OSCachedToPhysical). Everything the game owns sits below 4 GB, so adding the bit
 * back in 32-bit arithmetic recovers the pointer exactly, wherever it lives. */
static inline void* mu_phys_ptr(unsigned int phys) { return (void*) (uintptr_t) (unsigned int) (phys + 0x80000000u); }
/* Hardware that took either form (ARAM DMA, audio DMA). The game's memory is the image at
 * 0x50000000 and MEM1 at 0x80000000, both well under 1 GB long, so a value below 0x40000000 or at
 * 0xC0000000 and up can only be one of them with the segment bit taken off. */
static inline void* mu_mem_ptr(unsigned int addr)
{
    return (addr < 0x40000000u || addr >= 0xC0000000u) ? mu_phys_ptr(addr) : (void*) (uintptr_t) addr;
}

#endif
