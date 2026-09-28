// Converts callback-scoped native HSD POD records into host-owned authored poses.
// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once

#include "authored_pose.h"
#include "../abi/mu_native_pose.h"

namespace gx {
// Rigid snapshots only; returns null for anything else or a malformed payload.
std::shared_ptr<const AuthoredPose> copy_native_pose_snapshot(
    const MuNativePoseSnapshot& snapshot);
// Envelope (skinned) snapshots only: the same pose shape the Legacy observer's capture_envelope
// builds (view, skeleton-root transform, weighted bones per matrix slot, shared bone chains).
std::shared_ptr<const AuthoredPose> copy_native_envelope_snapshot(
    const MuNativePoseSnapshot& snapshot);
// Ends one simulation frame's chain sharing (see intern_chain). Called at the frame boundary.
void native_pose_bridge_frame_reset();
}
