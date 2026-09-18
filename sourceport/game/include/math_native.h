/* The console had no square root instruction. Its C library built sqrtf from the reciprocal square
 * root estimate and three Newton steps (MSL math_ppc.h), and the result is not always the correctly
 * rounded one a hardware sqrt gives. The game's numbers depend on it, so the native build computes
 * it the same way, with the same estimate table. */
#ifndef MATH_NATIVE_H
#define MATH_NATIVE_H

double mu_frsqrte(double x); /* the processor's estimate, from the recompiler's table */
double mu_fres(double x);
float mu_sqrtf(float x);
float mu_sqrtf_accurate(float x);

#define sqrtf(x) mu_sqrtf(x)
#define sqrtf__Ff(x) mu_sqrtf(x)
#define sqrtf_accurate(x) mu_sqrtf_accurate(x)
#define __frsqrte(x) mu_frsqrte((double) (x))

#endif
