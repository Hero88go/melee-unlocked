/* Where the console's addresses live in the host process.
 *
 * The game keeps addresses in 32-bit words: disc pointer slots, hardware registers, and plain ints all
 * over the decompilation. They hold the console window 0x7f000000..0x84000000 (MEM1 from 0x80000000,
 * this image from 0x82800000), which the x86-64 hosts place at exactly those addresses.
 *
 * Apple silicon Macs keep the whole low 4 GB of a native process unmapped, so the AArch64 Mac host
 * puts the same window at a 4 GB-aligned base instead (port/mac/elf_loader.cpp). A 32-bit word still
 * holds the low half, the very number the x86-64 build stores, so game state reads the same on both;
 * widening a word adds the base back. Words below 16 MB are not window addresses (NULL, ARAM offsets,
 * the marker values some fields hold) and widen unchanged. This image's own ELF header sits at
 * base+0x82800000, so the base is that address with its low half cleared.
 *
 * Compiler types only: no system header may reach game code (see mu_gxpipe.h). */
#ifndef MU_WINDOW_H
#define MU_WINDOW_H

#if defined(__aarch64__)
extern const char __ehdr_start[] __attribute__((visibility("hidden")));
#define MU_WINDOW_BASE ((__UINTPTR_TYPE__) __ehdr_start & ~(__UINTPTR_TYPE__) 0xFFFFFFFFu)
static inline __UINTPTR_TYPE__ mu_widen(__UINT32_TYPE__ word)
{
    return word < 0x01000000u ? (__UINTPTR_TYPE__) word : (__UINTPTR_TYPE__) word | MU_WINDOW_BASE;
}
#define MU_WIDEN(x) mu_widen((__UINT32_TYPE__) (x))
#else
#define MU_WINDOW_BASE ((__UINTPTR_TYPE__) 0)
#define MU_WIDEN(x) ((__UINTPTR_TYPE__) (__UINT32_TYPE__) (x))
#endif

#endif
