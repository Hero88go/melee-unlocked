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

/* The rest are real functions (shim/mu_math.c) because MetroTRK/intrinsics.h prototypes them by
 * these names, and a prototype must not meet a static inline. */
double mu_fabs(double x);
float mu_fabsf(float x);
float mu_fnmsubs(float a, float b, float c);
int mu_rlwinm(int value, int sh, int mb, int me);
int mu_rlwimi(int dst, int src, int sh, int mb, int me);
#define __fabs mu_fabs
#define __fabsf mu_fabsf
#define __fnmsubs mu_fnmsubs
#define __rlwinm mu_rlwinm
#define __rlwimi mu_rlwimi

#include "math_native.h"

#endif
