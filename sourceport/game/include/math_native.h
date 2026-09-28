/* The console had no square root instruction. Its C library built sqrtf from the reciprocal square
 * root estimate and three Newton steps (MSL math_ppc.h), and the result is not always the correctly
 * rounded one a hardware sqrt gives. The game's numbers depend on it, so the native build computes
 * it the same way, with the same estimate table. */
#ifndef MATH_NATIVE_H
#define MATH_NATIVE_H

double mu_frsqrte(double x); /* the SDK's table-driven numerical estimate */
double mu_fres(double x);
float mu_sqrtf(float x);
float mu_sqrtf_accurate(float x);

#define sqrtf(x) mu_sqrtf(x)
#define sqrtf__Ff(x) mu_sqrtf(x)
#define sqrtf_accurate(x) mu_sqrtf_accurate(x)
#define __frsqrte(x) mu_frsqrte((double) (x))

/* The console compiler's single-precision fused multiply-adds, where GCC does not fuse the same
 * expression. The product of two floats is exact in double, so the double expression rounds once to
 * double and once to float: the two roundings Dolphin's fmadds/fnmsubs make, which the recordings
 * the native game is compared with came from. (fnmsubs is mu_fnmsubs in mu_native.h.) */
static inline float mu_fmadds(float a, float c, float b)
{
    return (float) ((double) a * (double) c + (double) b);
}
static inline float mu_fmsubs(float a, float c, float b)
{
    return (float) ((double) a * (double) c - (double) b);
}

#endif
