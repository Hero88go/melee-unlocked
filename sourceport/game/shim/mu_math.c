/* What the console compiler provided as intrinsics, and its square root. */
#include "math_native.h"
#include "mu_native.h"

/* MSL math_ppc.h, sqrtf: estimate plus three Newton steps. The double arithmetic here is compiled
 * with the same contraction rule as the rest of the game, which is what the parity harness checks. */
float mu_sqrtf(float x)
{
    volatile float y;
    if (x > 0.0f) {
        double guess = mu_frsqrte((double) x);
        guess = 0.5 * guess * (3.0 - guess * guess * x);
        guess = 0.5 * guess * (3.0 - guess * guess * x);
        guess = 0.5 * guess * (3.0 - guess * guess * x);
        y = (float) (x * guess);
        return y;
    }
    return x;
}

float mu_sqrtf_accurate(float x)
{
    volatile float y;
    if (x > 0.0f) {
        double guess = mu_frsqrte((double) x);
        guess = 0.5 * guess * (3.0 - guess * guess * x);
        guess = 0.5 * guess * (3.0 - guess * guess * x);
        guess = 0.5 * guess * (3.0 - guess * guess * x);
        guess = 0.5 * guess * (3.0 - guess * guess * x);
        y = (float) (x * guess);
        return y;
    }
    return x;
}

double mu_fabs(double x) { return __builtin_fabs(x); }
float mu_fabsf(float x) { return __builtin_fabsf(x); }

/* fnmsubs: -(a*b - c), one rounding in double, then to single. */
float mu_fnmsubs(float a, float b, float c)
{
    return (float) -__builtin_fma((double) a, (double) b, -(double) c);
}

static unsigned int ppc_mask(int mb, int me)
{
    unsigned int begin = 0xFFFFFFFFu >> mb, end = 0x7FFFFFFFu >> me, m = begin ^ end;
    return me < mb ? ~m : m;
}
static unsigned int rotl32(unsigned int v, int sh) { sh &= 31; return sh ? (v << sh) | (v >> (32 - sh)) : v; }

int mu_rlwinm(int value, int sh, int mb, int me)
{
    return (int) (rotl32((unsigned int) value, sh) & ppc_mask(mb, me));
}

int mu_rlwimi(int dst, int src, int sh, int mb, int me)
{
    unsigned int m = ppc_mask(mb, me);
    return (int) ((rotl32((unsigned int) src, sh) & m) | ((unsigned int) dst & ~m));
}

/* Two paired-single vector routines no shipped code path reaches (they are absent from the retail
 * program), kept so the SDK's C wrappers link. Written as the assembly computes them: x*x, then y
 * and z folded in with fused multiply-adds. */
typedef struct { float x, y, z; } MuVec;
float PSVECSquareMag(MuVec* v)
{
    return __builtin_fmaf(v->z, v->z, __builtin_fmaf(v->y, v->y, v->x * v->x));
}
float PSVECSquareDistance(MuVec* a, MuVec* b)
{
    const float dx = a->x - b->x, dy = a->y - b->y, dz = a->z - b->z;
    return __builtin_fmaf(dz, dz, __builtin_fmaf(dy, dy, dx * dx));
}
