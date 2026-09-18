/* The native routines offered to the parity harness. Add a routine here once it is meant to match the
 * console exactly; the harness then holds it to that. */
#include "mu_parity.h"

#define M 12
#define V 3
#define Q 4

void PSMTXIdentity(), PSMTXCopy(), PSMTXConcat(), PSMTXTranspose(), PSMTXRotTrig(), PSMTXRotAxisRad(), PSMTXTrans(),
    PSMTXScale(), PSMTXQuat(), PSMTXMultVec(), PSMTXMultVecSR(), PSVECAdd(), PSVECSubtract(), PSVECScale(), PSVECNormalize(),
    PSVECCrossProduct();
uint32_t PSMTXInverse();
float PSVECMag(), PSVECDotProduct();
float sinf(float), cosf(float), tanf(float);
float mu_sqrtf(float), lbVector_sqrtf_accurate(float), lb_sqrtf_native(float);
float atan2f(float, float), atanf(float), acosf(float), asinf(float), expf(float), powf(float, float), logf(float);
float lbVector_Normalize(), lbVector_NormalizeXY(), lbVector_Angle(), lbVector_AngleXY(), lbVector_CosAngle();
void lbVector_Add(), lbVector_Add_xy(), lbVector_Sub(), lbVector_Diff(), lbVector_CrossprodNormalized(),
    lbVector_RotateAboutUnitAxis(), lbVector_Mirror(), lbVector_ApplyEulerRotation(), lbVector_CreateEulerMatrix();
void __sinit_trigf_c(void);

static const MuParityEntry table[] = {
    /* name, fn, n_ptr, {floats}, {written}, char, scalars, double, ret */
    { "PSMTXIdentity", PSMTXIdentity, 1, { M }, { 1 }, 0, 0, 0, MU_RET_NONE },
    { "PSMTXCopy", PSMTXCopy, 2, { M, M }, { 0, 1 }, 0, 0, 0, MU_RET_NONE },
    { "PSMTXConcat", PSMTXConcat, 3, { M, M, M }, { 0, 0, 1 }, 0, 0, 0, MU_RET_NONE },
    { "PSMTXTranspose", PSMTXTranspose, 2, { M, M }, { 0, 1 }, 0, 0, 0, MU_RET_NONE },
    { "PSMTXInverse", PSMTXInverse, 2, { M, M }, { 0, 1 }, 0, 0, 0, MU_RET_U32 },
    { "PSMTXRotTrig", PSMTXRotTrig, 1, { M }, { 1 }, 1, 2, 0, MU_RET_NONE },
    { "PSMTXRotAxisRad", PSMTXRotAxisRad, 2, { M, V }, { 1, 0 }, 0, 1, 0, MU_RET_NONE },
    { "PSMTXTrans", PSMTXTrans, 1, { M }, { 1 }, 0, 3, 0, MU_RET_NONE },
    { "PSMTXScale", PSMTXScale, 1, { M }, { 1 }, 0, 3, 0, MU_RET_NONE },
    { "PSMTXQuat", PSMTXQuat, 2, { M, Q }, { 1, 0 }, 0, 0, 0, MU_RET_NONE },
    { "PSMTXMultVec", PSMTXMultVec, 3, { M, V, V }, { 0, 0, 1 }, 0, 0, 0, MU_RET_NONE },
    { "PSMTXMultVecSR", PSMTXMultVecSR, 3, { M, V, V }, { 0, 0, 1 }, 0, 0, 0, MU_RET_NONE },
    { "PSVECAdd", PSVECAdd, 3, { V, V, V }, { 0, 0, 1 }, 0, 0, 0, MU_RET_NONE },
    { "PSVECSubtract", PSVECSubtract, 3, { V, V, V }, { 0, 0, 1 }, 0, 0, 0, MU_RET_NONE },
    { "PSVECScale", PSVECScale, 2, { V, V }, { 0, 1 }, 0, 1, 0, MU_RET_NONE },
    { "PSVECNormalize", PSVECNormalize, 2, { V, V }, { 0, 1 }, 0, 0, 0, MU_RET_NONE },
    { "PSVECMag", PSVECMag, 1, { V }, { 0 }, 0, 0, 0, MU_RET_REAL },
    { "PSVECDotProduct", PSVECDotProduct, 2, { V, V }, { 0, 0 }, 0, 0, 0, MU_RET_REAL },
    { "PSVECCrossProduct", PSVECCrossProduct, 3, { V, V, V }, { 0, 0, 1 }, 0, 0, 0, MU_RET_NONE },
    { "sinf", sinf, 0, { 0 }, { 0 }, 0, 1, 0, MU_RET_REAL },
    { "cosf", cosf, 0, { 0 }, { 0 }, 0, 1, 0, MU_RET_REAL },
    { "tanf", tanf, 0, { 0 }, { 0 }, 0, 1, 0, MU_RET_REAL },
    /* the console's square root, and the game's own trig, exp and vector libraries */
    { "sqrtf__Ff", mu_sqrtf, 0, { 0 }, { 0 }, 0, 1, 0, MU_RET_REAL },
    { "lb_sqrtf", lb_sqrtf_native, 0, { 0 }, { 0 }, 0, 1, 0, MU_RET_REAL },
    { "lbVector_sqrtf_accurate", lbVector_sqrtf_accurate, 0, { 0 }, { 0 }, 0, 1, 0, MU_RET_REAL },
    { "atan2f", atan2f, 0, { 0 }, { 0 }, 0, 2, 0, MU_RET_REAL },
    { "atanf", atanf, 0, { 0 }, { 0 }, 0, 1, 0, MU_RET_REAL },
    { "acosf", acosf, 0, { 0 }, { 0 }, 0, 1, 0, MU_RET_REAL },
    { "asinf", asinf, 0, { 0 }, { 0 }, 0, 1, 0, MU_RET_REAL },
    { "expf", expf, 0, { 0 }, { 0 }, 0, 1, 0, MU_RET_REAL, { -10 }, { 10 } },   /* a Taylor series that overflows to NaN and never converges past about 30 */
    { "powf", powf, 0, { 0 }, { 0 }, 0, 2, 0, MU_RET_REAL, { 0.1f, -2 }, { 10, 2 } },   /* a log series, so a positive base, and expf after it */
    { "logf", logf, 0, { 0 }, { 0 }, 0, 1, 0, MU_RET_REAL },
    { "lbVector_Normalize", lbVector_Normalize, 1, { V }, { 1 }, 0, 0, 0, MU_RET_REAL },
    { "lbVector_NormalizeXY", lbVector_NormalizeXY, 1, { V }, { 1 }, 0, 0, 0, MU_RET_REAL },
    { "lbVector_Add", lbVector_Add, 2, { V, V }, { 1, 0 }, 0, 0, 0, MU_RET_NONE },
    { "lbVector_Add_xy", lbVector_Add_xy, 2, { V, V }, { 1, 0 }, 0, 0, 0, MU_RET_NONE },
    { "lbVector_Sub", lbVector_Sub, 2, { V, V }, { 1, 0 }, 0, 0, 0, MU_RET_NONE },
    { "lbVector_Diff", lbVector_Diff, 3, { V, V, V }, { 0, 0, 1 }, 0, 0, 0, MU_RET_NONE },
    { "lbVector_CrossprodNormalized", lbVector_CrossprodNormalized, 3, { V, V, V }, { 0, 0, 1 }, 0, 0, 0, MU_RET_NONE },
    { "lbVector_Angle", lbVector_Angle, 2, { V, V }, { 0, 0 }, 0, 0, 0, MU_RET_REAL },
    { "lbVector_AngleXY", lbVector_AngleXY, 2, { V, V }, { 0, 0 }, 0, 0, 0, MU_RET_REAL },
    { "lbVector_RotateAboutUnitAxis", lbVector_RotateAboutUnitAxis, 2, { V, V }, { 1, 0 }, 0, 1, 0, MU_RET_NONE },
    { "lbVector_Mirror", lbVector_Mirror, 2, { V, V }, { 1, 0 }, 0, 0, 0, MU_RET_NONE },
    { "lbVector_CosAngle", lbVector_CosAngle, 2, { V, V }, { 0, 0 }, 0, 0, 0, MU_RET_REAL },
    { "lbVector_ApplyEulerRotation", lbVector_ApplyEulerRotation, 2, { V, V }, { 1, 0 }, 0, 0, 0, MU_RET_NONE },
    { "lbVector_CreateEulerMatrix", lbVector_CreateEulerMatrix, 2, { M, Q }, { 1, 0 }, 0, 0, 0, MU_RET_NONE },
};

__declspec(dllexport) const MuParityEntry* mu_parity_table(int* count)
{
    static int started;
    if (!started) {
        started = 1;
        __sinit_trigf_c(); /* the console ran static initialisers from a table; the native game must too */
    }
    *count = (int) (sizeof table / sizeof table[0]);
    return table;
}
