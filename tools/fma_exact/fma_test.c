/* Reference check for gcc_fma_wrap.py. Built with -ffp-contract=off; the kernels come from a
 * separately wrapped object. Every result must equal the console rule bit for bit:
 * round_single(round_double(a*b op c)). */
#include <stdint.h>
#include <stdio.h>
#include <string.h>

float k_madd(float, float, float);
float k_msub(float, float, float);
float k_nmadd(float, float, float);
float k_nmsub(float, float, float);
float k_mem(const float*);
float k_mem2(float, const float*);
void k_vec(float*, const float*, const float*, const float*, int);
void k_vecn(float*, const float*, const float*, const float*, int);

static uint64_t s = 0x9E3779B97F4A7C15ull;
static uint32_t rnd(void)
{
    s ^= s << 13; s ^= s >> 7; s ^= s << 17;
    return (uint32_t) s;
}
static float rf(void)
{
    uint32_t u = rnd();
    switch (rnd() & 7) {
    case 0: u = (u & 0x807FFFFFu) | 0x3F800000u; break;   /* near 1 */
    case 1: u = (u & 0x80FFFFFFu) | 0x42000000u; break;   /* game-sized */
    case 2: u &= 0x8000FFFFu; break;                      /* subnormal */
    default: if (((u >> 23) & 0xFF) == 0xFF) u &= 0xBFFFFFFFu; break;
    }
    float f;
    memcpy(&f, &u, 4);
    return f;
}
static uint32_t bits(float f) { uint32_t u; memcpy(&u, &f, 4); return u; }
static float ref(float a, float b, float c, int neg, int sub)
{
    double p = (double) a * (double) b;
    double r = neg ? (sub ? -(p + c) : (double) c - p) : (sub ? p - c : p + c);
    return (float) r;
}

int main(void)
{
    long bad = 0, n = 0, differs_from_single = 0;
    enum { N = 64 };
    float a[N], b[N], c[N], o[N], on[N];
    for (long it = 0; it < 2000000; it++) {
        float x = rf(), y = rf(), z = rf();
        float want[4] = { ref(x, y, z, 0, 0), ref(x, y, z, 0, 1), ref(x, y, z, 1, 0), ref(x, y, z, 1, 1) };
        float got[4] = { k_madd(x, y, z), k_msub(x, y, z), k_nmadd(x, y, z), k_nmsub(x, y, z) };
        for (int k = 0; k < 4; k++, n++) {
            if (bits(want[k]) != bits(got[k]) && !(want[k] != want[k] && got[k] != got[k])) {
                if (bad++ < 10)
                    printf("mismatch k=%d a=%08X b=%08X c=%08X want=%08X got=%08X\n", k, bits(x), bits(y), bits(z), bits(want[k]), bits(got[k]));
            }
        }
        if (bits(__builtin_fmaf(x, y, z)) != bits(want[0])) differs_from_single++;
        float p[3] = { x, y, z };
        if (bits(k_mem(p)) != bits(want[0])) { if (bad++ < 10) printf("mem mismatch\n"); }
        float q[2] = { x, z };
        if (bits(k_mem2(y, q)) != bits(ref(y, x, z, 1, 0))) { if (bad++ < 10) printf("mem2 mismatch\n"); }
        n += 2;
        int i = (int) (it % N);
        a[i] = x; b[i] = y; c[i] = z;
        if (i == N - 1) {
            k_vec(o, a, b, c, N);
            k_vecn(on, a, b, c, N);
            for (int j = 0; j < N; j++, n += 2) {
                if (bits(o[j]) != bits(ref(a[j], b[j], c[j], 0, 0))) { if (bad++ < 10) printf("vec mismatch %d\n", j); }
                if (bits(on[j]) != bits(ref(a[j], b[j], c[j], 1, 0))) { if (bad++ < 10) printf("vecn mismatch %d\n", j); }
            }
        }
    }
    printf("checked %ld results, %ld mismatches; plain single-rounded FMA differs from the console rule in %ld of 2000000 cases\n",
           n, bad, differs_from_single);
    return bad != 0;
}
