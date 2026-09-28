/* See include/mu_math_audit.h. Counts calls and calls with a non-finite input per routine, logs the
 * first few offending calls, and reports the totals from the retrace shim. */
#ifdef MU_MATH_AUDIT
#include "mu_shim.h"

int snprintf(char* dst, __SIZE_TYPE__ size, const char* format, ...);

static const char* const audit_names[MU_AUDIT_COUNT] = {
    "PSMTXInverse", "PSMTXRotAxisRad", "PSMTXQuat", "PSMTXMultVec", "PSMTXMultVecSR", "sinf",
    "lbVector_Normalize", "lbVector_NormalizeXY", "lbVector_CrossprodNormalized",
    "lbVector_Angle", "lbVector_AngleXY", "lbVector_RotateAboutUnitAxis",
    "lbVector_CosAngle", "lbVector_ApplyEulerRotation", "lbVector_CreateEulerMatrix",
};
static unsigned long long audit_calls[MU_AUDIT_COUNT], audit_nonfinite[MU_AUDIT_COUNT];

static int finite_bits(const float* value)
{
    unsigned int bits;
    __builtin_memcpy(&bits, value, sizeof bits);
    return (bits & 0x7F800000u) != 0x7F800000u;
}

void mu_math_audit(int routine, const float* values, int count)
{
    int i;
    if (routine < 0 || routine >= MU_AUDIT_COUNT)
        return;
    ++audit_calls[routine];
    for (i = 0; i < count; ++i) {
        if (!finite_bits(&values[i])) {
            if (++audit_nonfinite[routine] <= 3 && mu_host && mu_host->log) {
                char line[160];
                unsigned int bits;
                __builtin_memcpy(&bits, &values[i], sizeof bits);
                snprintf(line, sizeof line, "[math-audit] non-finite input to %s: value %d of %d = %08X",
                         audit_names[routine], i, count, bits);
                mu_host->log(line);
            }
            return;
        }
    }
}

void mu_math_audit_report(void)
{
    int i;
    if (!mu_host || !mu_host->log)
        return;
    for (i = 0; i < MU_AUDIT_COUNT; ++i) {
        char line[160];
        snprintf(line, sizeof line, "[math-audit] %s checks %llu non-finite %llu",
                 audit_names[i], audit_calls[i], audit_nonfinite[i]);
        mu_host->log(line);
    }
}
#endif
