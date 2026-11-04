/* Recognize ftDrawCommon's view * T(model-shake-x, model-shake-y, 0).
 * Shared by native and translated capture; never changes game state.
 * SPDX-License-Identifier: GPL-2.0-or-later */
#ifndef MU_POSE_VIEW_H
#define MU_POSE_VIEW_H
static inline float mu_pose_abs(float x) { return x < 0 ? -x : x; }
static inline int mu_pose_view_model_shift(const float* view, const float* camera, float* shift)
{
    float xx = 0, xy = 0, yy = 0, xd = 0, yd = 0, delta[3], det, x, y;
    int r, c;
    /* Another orientation is another camera, not a model shake. */
    for (r = 0; r < 3; ++r) {
        for (c = 0; c < 3; ++c)
            if (view[r * 4 + c] != camera[r * 4 + c]) return 0;
        delta[r] = view[r * 4 + 3] - camera[r * 4 + 3];
        xx += camera[r * 4] * camera[r * 4];
        xy += camera[r * 4] * camera[r * 4 + 1];
        yy += camera[r * 4 + 1] * camera[r * 4 + 1];
        xd += camera[r * 4] * delta[r];
        yd += camera[r * 4 + 1] * delta[r];
    }
    det = xx * yy - xy * xy;
    if (!(det > 1e-12f)) return 0;
    x = (xd * yy - yd * xy) / det;
    y = (yd * xx - xd * xy) / det;
    if (!(mu_pose_abs(x) <= 30 && mu_pose_abs(y) <= 30)) return 0;
    /* Reject depth offsets and malformed matrices, including NaNs. */
    for (r = 0; r < 3; ++r)
        if (!(mu_pose_abs(delta[r] - camera[r * 4] * x - camera[r * 4 + 1] * y) <= .0002f)) return 0;
    if (x == 0 && y == 0) return 0;
    shift[0] = x; shift[1] = y;
    return 1;
}
#endif
