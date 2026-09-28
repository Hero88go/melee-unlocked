/* Compiled through gcc_fma_wrap.py: every expression below contracts to an FMA3 instruction. */
float k_madd(float a, float b, float c) { return a * b + c; }
float k_msub(float a, float b, float c) { return a * b - c; }
float k_nmadd(float a, float b, float c) { return c - a * b; }
float k_nmsub(float a, float b, float c) { return -(a * b) - c; }
float k_mem(const float* p) { return p[0] * p[1] + p[2]; }
float k_mem2(float a, const float* p) { return p[1] - a * p[0]; }
void k_vec(float* restrict out, const float* restrict a, const float* restrict b,
           const float* restrict c, int n)
{
    for (int i = 0; i < n; i++) {
        out[i] = a[i] * b[i] + c[i];
    }
}
void k_vecn(float* restrict out, const float* restrict a, const float* restrict b,
            const float* restrict c, int n)
{
    for (int i = 0; i < n; i++) {
        out[i] = c[i] - a[i] * b[i];
    }
}
