/* M0 evidence: whether non-finite values (NaN, infinity) ever reach the maths routines whose native
 * and console results differ only for such inputs. Off unless the build defines MU_MATH_AUDIT
 * (CMake option of the same name), so the shipped DLL carries no check at all. */
#ifndef MU_MATH_AUDIT_H
#define MU_MATH_AUDIT_H

#ifdef MU_MATH_AUDIT
enum {
    MU_AUDIT_PSMTXInverse,
    MU_AUDIT_PSMTXRotAxisRad,
    MU_AUDIT_PSMTXQuat,
    MU_AUDIT_PSMTXMultVec,
    MU_AUDIT_PSMTXMultVecSR,
    MU_AUDIT_sinf,
    MU_AUDIT_lbVector_Normalize,
    MU_AUDIT_lbVector_NormalizeXY,
    MU_AUDIT_lbVector_CrossprodNormalized,
    MU_AUDIT_lbVector_Angle,
    MU_AUDIT_lbVector_AngleXY,
    MU_AUDIT_lbVector_RotateAboutUnitAxis,
    MU_AUDIT_lbVector_CosAngle,
    MU_AUDIT_lbVector_ApplyEulerRotation,
    MU_AUDIT_lbVector_CreateEulerMatrix,
    MU_AUDIT_COUNT
};
void mu_math_audit(int routine, const float* values, int count);
void mu_math_audit_report(void);
#define MU_MATH_AUDIT_IN(routine, values, count) \
    mu_math_audit(MU_AUDIT_##routine, (const float*) (values), (count))
#else
#define MU_MATH_AUDIT_IN(routine, values, count) ((void) 0)
#endif

#endif
