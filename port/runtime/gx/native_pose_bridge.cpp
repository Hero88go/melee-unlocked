// SPDX-License-Identifier: GPL-2.0-or-later
#include "native_pose_bridge.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <unordered_map>
#include <utility>

namespace gx {
namespace {
bool finite_values(const float* values, size_t count) {
  for (size_t i = 0; i < count; ++i)
    if (!std::isfinite(values[i])) return false;
  return true;
}

bool valid_channel(uint8_t channel) {
  return (channel >= 1 && channel <= 3) || (channel >= 5 && channel <= 10);
}
// Copies snapshot.joints (with their tracks, which must be contiguous in joint order).
bool copy_joints(const MuNativePoseSnapshot& snapshot, std::vector<AuthoredJoint>& out) {
  uint32_t expected_track = 0;
  out.reserve(snapshot.joint_count);
  for (uint32_t i = 0; i < snapshot.joint_count; ++i) {
    const MuNativePoseJoint& source = snapshot.joints[i];
    if (!source.generation || source.first_track != expected_track ||
        source.first_track > snapshot.track_count ||
        source.track_count > snapshot.track_count - source.first_track ||
        source.track_count > 32 ||
        !finite_values(source.scale, 3) || !finite_values(source.rotation, 3) ||
        !finite_values(source.translation, 3) || !finite_values(source.quaternion, 4) ||
        !finite_values(source.world, 12) ||
        !std::isfinite(source.frame) || !std::isfinite(source.rate) ||
        !std::isfinite(source.end) || !std::isfinite(source.rewind))
      return false;

    AuthoredJoint joint;
    joint.generation = source.generation;
    joint.flags = source.flags;
    joint.anim_flags = source.anim_flags;
    std::copy_n(source.scale, 3, joint.scale.begin());
    std::copy_n(source.rotation, 3, joint.rotation.begin());
    std::copy_n(source.translation, 3, joint.translation.begin());
    std::copy_n(source.quaternion, 4, joint.quat.begin());
    std::copy_n(source.world, 12, joint.world.begin());
    joint.quaternion = (source.flags & 0x20000u) != 0;
    joint.frame = source.frame;
    joint.rate = source.rate;
    joint.end = source.end;
    joint.rewind = source.rewind;
    joint.tracks.reserve(source.track_count);

    for (uint32_t t = 0; t < source.track_count; ++t) {
      const MuNativePoseTrack& source_track = snapshot.tracks[source.first_track + t];
      if (!valid_channel(source_track.channel) ||
          source_track.byte_offset > snapshot.track_byte_count ||
          source_track.byte_length > snapshot.track_byte_count - source_track.byte_offset)
        return false;
      NativeMelee::PackedTrack track;
      track.start_frame = source_track.start_frame;
      track.channel = source_track.channel;
      track.value_format = source_track.value_format;
      track.slope_format = source_track.slope_format;
      if (source_track.byte_length != 0) {
        const uint8_t* begin = snapshot.track_bytes + source_track.byte_offset;
        track.bytes.assign(begin, begin + source_track.byte_length);
      }
      joint.tracks.push_back(std::move(track));
    }
    expected_track += source.track_count;
    out.push_back(std::move(joint));
  }
  return expected_track == snapshot.track_count;
}

bool same_track(const NativeMelee::PackedTrack& a, const NativeMelee::PackedTrack& b) {
  return a.start_frame == b.start_frame && a.channel == b.channel && a.value_format == b.value_format &&
         a.slope_format == b.slope_format && a.bytes == b.bytes;
}

// Bit for bit (memcmp, so -0 and +0 differ): a shared chain must sample exactly as its copies would.
bool same_joint(const AuthoredJoint& a, const AuthoredJoint& b) {
  if (a.generation != b.generation || a.flags != b.flags || a.anim_flags != b.anim_flags ||
      a.quaternion != b.quaternion || std::memcmp(&a.scale, &b.scale, sizeof a.scale) ||
      std::memcmp(&a.rotation, &b.rotation, sizeof a.rotation) ||
      std::memcmp(&a.translation, &b.translation, sizeof a.translation) ||
      std::memcmp(&a.quat, &b.quat, sizeof a.quat) || std::memcmp(&a.world, &b.world, sizeof a.world) ||
      std::memcmp(&a.frame, &b.frame, sizeof a.frame) || std::memcmp(&a.rate, &b.rate, sizeof a.rate) ||
      std::memcmp(&a.end, &b.end, sizeof a.end) || std::memcmp(&a.rewind, &b.rewind, sizeof a.rewind) ||
      a.tracks.size() != b.tracks.size() || !a.constraints.empty() || !b.constraints.empty())
    return false;
  for (size_t t = 0; t < a.tracks.size(); ++t)
    if (!same_track(a.tracks[t], b.tracks[t])) return false;
  return true;
}

// The sampler caches one sampled chain per pair of chain OBJECTS per presented frame
// (AuthoredCache, keyed on addresses). The translated build's observer hands every draw of one
// joint the same chain object for the frame, so a fighter's bones are sampled once. Snapshots used
// to build a new chain per PObj, so every skinned piece and every rigid PObj of a joint sampled the
// same bones again on every presented frame. Chains equal in every captured field now share one
// object for the simulation frame; a joint whose state changed between two draws stays separate.
std::unordered_map<uint64_t, std::vector<std::shared_ptr<const AuthoredPose>>> g_chains;

uint64_t chain_hash(const std::vector<AuthoredJoint>& joints) {
  uint64_t h = 1469598103934665603ull;
  auto mix = [&h](uint64_t v) { h ^= v; h *= 1099511628211ull; };
  mix(joints.size());
  for (const AuthoredJoint& j : joints) {
    uint32_t frame_bits;
    std::memcpy(&frame_bits, &j.frame, sizeof frame_bits);
    mix(j.generation); mix(j.flags); mix(frame_bits);
  }
  return h;
}

std::shared_ptr<const AuthoredPose> intern_chain(std::vector<AuthoredJoint>&& joints) {
  std::vector<std::shared_ptr<const AuthoredPose>>& bucket = g_chains[chain_hash(joints)];
  for (const auto& existing : bucket) {
    if (existing->joints.size() != joints.size()) continue;
    bool equal = true;
    for (size_t i = 0; equal && i < joints.size(); ++i) equal = same_joint(existing->joints[i], joints[i]);
    if (equal) return existing;
  }
  auto chain = std::make_shared<AuthoredPose>();
  chain->joints = std::move(joints);
  bucket.push_back(chain);
  return chain;
}
}  // namespace

void native_pose_bridge_frame_reset() { g_chains.clear(); }

std::shared_ptr<const AuthoredPose> copy_native_pose_snapshot(
    const MuNativePoseSnapshot& snapshot) {
  if (snapshot.kind != MU_NATIVE_POSE_RIGID ||
      snapshot.reject_reason != MU_NATIVE_POSE_REJECT_NONE ||
      snapshot.joint_count == 0 ||
      snapshot.joint_count > MU_NATIVE_POSE_MAX_JOINTS ||
      snapshot.track_count > MU_NATIVE_POSE_MAX_TRACKS ||
      snapshot.track_byte_count > MU_NATIVE_POSE_MAX_TRACK_BYTES ||
      !snapshot.joints ||
      (snapshot.track_count && !snapshot.tracks) ||
      (snapshot.track_byte_count && !snapshot.track_bytes) ||
      snapshot.has_view > 1 || snapshot.quake > 1 ||
      (snapshot.has_view && !finite_values(snapshot.view, 12)))
    return {};

  auto pose = std::make_shared<AuthoredPose>();
  pose->has_view = snapshot.has_view != 0;
  pose->quake = snapshot.quake != 0;
  if (pose->has_view)
    std::memcpy(pose->view.data(), snapshot.view, sizeof(snapshot.view));

  std::vector<AuthoredJoint> joints;
  if (!copy_joints(snapshot, joints)) return {};
  pose->chain = intern_chain(std::move(joints));   // the translated observer's shape: pose + shared chain
  return pose;
}

std::shared_ptr<const AuthoredPose> copy_native_envelope_snapshot(
    const MuNativePoseSnapshot& snapshot) {
  if (snapshot.kind != MU_NATIVE_POSE_ENVELOPE ||
      snapshot.reject_reason != MU_NATIVE_POSE_REJECT_NONE ||
      snapshot.joint_count == 0 ||
      snapshot.joint_count > MU_NATIVE_POSE_MAX_TOTAL_JOINTS ||
      snapshot.track_count > MU_NATIVE_POSE_MAX_TRACKS ||
      snapshot.track_byte_count > MU_NATIVE_POSE_MAX_TRACK_BYTES ||
      snapshot.chain_count == 0 || snapshot.chain_count > MU_NATIVE_POSE_MAX_CHAINS ||
      snapshot.link_count == 0 || snapshot.link_count > MU_NATIVE_POSE_MAX_LINKS ||
      snapshot.bone_count == 0 || snapshot.bone_count > MU_NATIVE_POSE_MAX_BONES ||
      snapshot.slot_count == 0 || snapshot.slot_count > MU_NATIVE_POSE_MAX_SLOTS ||
      snapshot.right_kind > 3 ||
      !snapshot.joints || !snapshot.chains || !snapshot.links || !snapshot.bones || !snapshot.slots ||
      (snapshot.track_count && !snapshot.tracks) ||
      (snapshot.track_byte_count && !snapshot.track_bytes) ||
      snapshot.has_view != 1 || snapshot.quake > 1 ||
      !finite_values(snapshot.view, 12))
    return {};

  std::vector<AuthoredJoint> joints;
  if (!copy_joints(snapshot, joints)) return {};

  // Each chain becomes one shared pose, shared with every other snapshot of the frame that loads
  // the same bones (intern_chain), so the sampler's chain cache hits.
  std::vector<std::shared_ptr<const AuthoredPose>> chains;
  chains.reserve(snapshot.chain_count);
  for (uint32_t c = 0; c < snapshot.chain_count; ++c) {
    const MuNativePoseChain& source = snapshot.chains[c];
    if (source.link_count == 0 || source.link_count > MU_NATIVE_POSE_MAX_JOINTS ||
        source.first_link > snapshot.link_count ||
        source.link_count > snapshot.link_count - source.first_link)
      return {};
    std::vector<AuthoredJoint> links;
    links.reserve(source.link_count);
    for (uint32_t l = 0; l < source.link_count; ++l) {
      const uint32_t joint = snapshot.links[source.first_link + l];
      if (joint >= joints.size()) return {};
      links.push_back(joints[joint]);
    }
    chains.push_back(intern_chain(std::move(links)));
  }

  auto pose = std::make_shared<AuthoredPose>();
  pose->envelope = true;
  pose->has_view = true;
  pose->quake = snapshot.quake != 0;
  std::memcpy(pose->view.data(), snapshot.view, sizeof(snapshot.view));
  pose->right_kind = static_cast<int>(snapshot.right_kind);
  if (pose->right_kind != 0) {
    if (snapshot.right_chain_m >= chains.size() || snapshot.right_chain_x >= chains.size())
      return {};
    pose->right_chain_m = chains[snapshot.right_chain_m];
    pose->right_chain_x = chains[snapshot.right_chain_x];
    if (pose->right_kind != 2) {
      if (!finite_values(snapshot.right_envelope, 12)) return {};
      std::memcpy(pose->right_envelope.data(), snapshot.right_envelope,
                  sizeof(snapshot.right_envelope));
    }
  }

  uint32_t expected_bone = 0;
  pose->slots.reserve(snapshot.slot_count);
  for (uint32_t s = 0; s < snapshot.slot_count; ++s) {
    const MuNativePoseSlot& source = snapshot.slots[s];
    if (source.first_bone != expected_bone || source.bone_count == 0 ||
        source.bone_count > snapshot.bone_count - source.first_bone)
      return {};
    AuthoredSlot slot;
    slot.bones.reserve(source.bone_count);
    for (uint32_t b = 0; b < source.bone_count; ++b) {
      const MuNativePoseBone& bone = snapshot.bones[source.first_bone + b];
      if (bone.chain >= chains.size() || !std::isfinite(bone.weight) ||
          !finite_values(bone.envelope, 12))
        return {};
      AuthoredBone target;
      target.chain = chains[bone.chain];
      target.weight = bone.weight;
      std::memcpy(target.envelope.data(), bone.envelope, sizeof(bone.envelope));
      slot.bones.push_back(std::move(target));
    }
    expected_bone += source.bone_count;
    pose->slots.push_back(std::move(slot));
  }
  if (expected_bone != snapshot.bone_count) return {};
  return pose;
}
}  // namespace gx
