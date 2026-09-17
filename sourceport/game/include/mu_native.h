/* Included ahead of every game and SDK source file in the native build (-include). It supplies what
 * the console compiler provided as intrinsics, and nothing else. */
#ifndef MU_NATIVE_H
#define MU_NATIVE_H

#ifndef TRUE
#define TRUE 1
#endif
#ifndef FALSE
#define FALSE 0
#endif

/* Count leading zeros. The PowerPC instruction answers 32 for zero; the x86 one is undefined there. */
static inline int mu_cntlzw(unsigned int value) { return value ? __builtin_clz(value) : 32; }
#define __cntlzw(value) mu_cntlzw((unsigned int) (value))

#endif
