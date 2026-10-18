/* PlLb.dat's local path routines. Native math only; no guest-code execution. */
#ifndef MU_ACE_LUIGIBOO_MATH_H
#define MU_ACE_LUIGIBOO_MATH_H

#include <Runtime/platform.h>
#include <dolphin/mtx.h>

typedef float (*ftLb_Ease)(float);

/* Console +1E88 stores only each vector's x and y. The cubic sampler reads
 * only those eight floats. The z and spare slots have no readers. */
typedef struct ftLb_Path {
    Vec3 start;         /* +00 */
    Vec3 control1;      /* +0C */
    Vec3 end;           /* +18 */
    Vec3 control2;      /* +24 */
    u32 spare;          /* +30, never read */
} ftLb_Path;

_Static_assert(sizeof(ftLb_Path) == 0x34, "PlLb path copy length");

/* Fighter +1E88, article +4884. The callback is invoked four times, in the
 * original order, even though the first two results are discarded. */
ftLb_Path* ftLb_PathSolve(ftLb_Path* out, const Vec3* start, const Vec3* end,
                        int steps, ftLb_Ease ease, float height, float angle);
/* Article +4AE4. The result is always on the XY plane. */
Vec3* ftLb_PathSample(Vec3* out, const ftLb_Path* path, ftLb_Ease ease, float t);
/* Fighter +1E80 and article +487C. */
float ftLb_EaseSquare(float t);
/* Article +4BB8. */
float ftLb_VectorLength(const Vec3* vector);

#endif
