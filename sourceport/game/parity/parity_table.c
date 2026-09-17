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
