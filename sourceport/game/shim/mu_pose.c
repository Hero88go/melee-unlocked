/* Keep per-model hit/grab shake separate from smooth camera motion.
 * SPDX-License-Identifier: GPL-2.0-or-later */
#include "mu_native_pose.h"
#include "mu_pose_view.h"
#include <sysdolphin/baselib/cobj.h>

void mu_native_pose_step_view(MuNativePoseSnapshot* snapshot)
{
    HSD_CObj* camera = HSD_CObjGetCurrent();
    float model_shift[2];
    /* ftDrawCommon adds hit/grab shake to the view passed to HSD. The CObj
     * retains the camera-only view. Alternating +a/-a must not be predicted
     * as camera velocity (which exaggerates it to as much as 3a). */
    if (snapshot->has_view && camera != NULL &&
        mu_pose_view_model_shift(snapshot->view, &camera->view_mtx[0][0], model_shift)) {
        snapshot->quake |= MU_NATIVE_POSE_VIEW_MODEL_SHIFT;
        snapshot->quake_shift[0] -= model_shift[0];
        snapshot->quake_shift[1] -= model_shift[1];
    }
}
