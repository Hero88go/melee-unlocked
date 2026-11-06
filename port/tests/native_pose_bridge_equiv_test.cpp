// SPDX-License-Identifier: GPL-2.0-or-later
// The pose bridge compares a snapshot with the frame's chains before copying it. This keeps the
// previous copy-then-compare version as a reference and feeds both the same random frames (chains
// that repeat, chains that differ in one field, -0 against +0, broken payloads), requiring the same
// accept/reject outcome, the same captured values and the same chain SHARING: the sub-frame
// sampler caches by chain object, so which snapshots share a chain is part of the output.
#include "native_pose_bridge.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <limits>
#include <map>
#include <random>
#include <unordered_map>
#include <vector>

using gx::AuthoredJoint;
using gx::AuthoredPose;

namespace ref {
namespace {
// The previous implementation, verbatim apart from the namespace.
bool finite_values(const float* values, size_t count) {
  for (size_t i = 0; i < count; ++i)
    if (!std::isfinite(values[i])) return false;
  return true;
}
bool valid_channel(uint8_t channel) { return (channel >= 1 && channel <= 3) || (channel >= 5 && channel <= 10); }
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
         a.slope_format == b.slope_format && NativeMelee::SameBytes(a, b);
}
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

void frame_reset() { g_chains.clear(); }

std::shared_ptr<const AuthoredPose> copy_native_pose_snapshot(const MuNativePoseSnapshot& snapshot) {
  if (snapshot.kind != MU_NATIVE_POSE_RIGID || snapshot.reject_reason != MU_NATIVE_POSE_REJECT_NONE ||
      snapshot.joint_count == 0 || snapshot.joint_count > MU_NATIVE_POSE_MAX_JOINTS ||
      snapshot.track_count > MU_NATIVE_POSE_MAX_TRACKS || snapshot.track_byte_count > MU_NATIVE_POSE_MAX_TRACK_BYTES ||
      !snapshot.joints || (snapshot.track_count && !snapshot.tracks) ||
      (snapshot.track_byte_count && !snapshot.track_bytes) ||
      snapshot.has_view > 1 || (snapshot.quake & ~MU_NATIVE_POSE_VIEW_SHIFT_MASK) ||
      (snapshot.has_view && !finite_values(snapshot.view, 12)))
    return {};
  auto pose = std::make_shared<AuthoredPose>();
  pose->has_view = snapshot.has_view != 0;
  pose->quake = snapshot.quake != 0;
  pose->model_shift = (snapshot.quake & MU_NATIVE_POSE_VIEW_MODEL_SHIFT) != 0;
  if (pose->quake && finite_values(snapshot.quake_shift, 2))
    pose->quake_shift = {snapshot.quake_shift[0], snapshot.quake_shift[1]};
  if (pose->has_view) std::memcpy(pose->view.data(), snapshot.view, sizeof(snapshot.view));
  std::vector<AuthoredJoint> joints;
  if (!copy_joints(snapshot, joints)) return {};
  pose->chain = intern_chain(std::move(joints));
  return pose;
}

std::shared_ptr<const AuthoredPose> copy_native_envelope_snapshot(const MuNativePoseSnapshot& snapshot) {
  if (snapshot.kind != MU_NATIVE_POSE_ENVELOPE || snapshot.reject_reason != MU_NATIVE_POSE_REJECT_NONE ||
      snapshot.joint_count == 0 || snapshot.joint_count > MU_NATIVE_POSE_MAX_TOTAL_JOINTS ||
      snapshot.track_count > MU_NATIVE_POSE_MAX_TRACKS || snapshot.track_byte_count > MU_NATIVE_POSE_MAX_TRACK_BYTES ||
      snapshot.chain_count == 0 || snapshot.chain_count > MU_NATIVE_POSE_MAX_CHAINS ||
      snapshot.link_count == 0 || snapshot.link_count > MU_NATIVE_POSE_MAX_LINKS ||
      snapshot.bone_count == 0 || snapshot.bone_count > MU_NATIVE_POSE_MAX_BONES ||
      snapshot.slot_count == 0 || snapshot.slot_count > MU_NATIVE_POSE_MAX_SLOTS || snapshot.right_kind > 3 ||
      !snapshot.joints || !snapshot.chains || !snapshot.links || !snapshot.bones || !snapshot.slots ||
      (snapshot.track_count && !snapshot.tracks) || (snapshot.track_byte_count && !snapshot.track_bytes) ||
      snapshot.has_view != 1 || (snapshot.quake & ~MU_NATIVE_POSE_VIEW_SHIFT_MASK) || !finite_values(snapshot.view, 12))
    return {};
  std::vector<AuthoredJoint> joints;
  if (!copy_joints(snapshot, joints)) return {};
  std::vector<std::shared_ptr<const AuthoredPose>> chains;
  chains.reserve(snapshot.chain_count);
  for (uint32_t c = 0; c < snapshot.chain_count; ++c) {
    const MuNativePoseChain& source = snapshot.chains[c];
    if (source.link_count == 0 || source.link_count > MU_NATIVE_POSE_MAX_JOINTS ||
        source.first_link > snapshot.link_count || source.link_count > snapshot.link_count - source.first_link)
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
  pose->model_shift = (snapshot.quake & MU_NATIVE_POSE_VIEW_MODEL_SHIFT) != 0;
  if (pose->quake && finite_values(snapshot.quake_shift, 2))
    pose->quake_shift = {snapshot.quake_shift[0], snapshot.quake_shift[1]};
  std::memcpy(pose->view.data(), snapshot.view, sizeof(snapshot.view));
  pose->right_kind = static_cast<int>(snapshot.right_kind);
  if (pose->right_kind != 0) {
    if (snapshot.right_chain_m >= chains.size() || snapshot.right_chain_x >= chains.size()) return {};
    pose->right_chain_m = chains[snapshot.right_chain_m];
    pose->right_chain_x = chains[snapshot.right_chain_x];
    if (pose->right_kind != 2) {
      if (!finite_values(snapshot.right_envelope, 12)) return {};
      std::memcpy(pose->right_envelope.data(), snapshot.right_envelope, sizeof(snapshot.right_envelope));
    }
  }
  uint32_t expected_bone = 0;
  pose->slots.reserve(snapshot.slot_count);
  for (uint32_t s = 0; s < snapshot.slot_count; ++s) {
    const MuNativePoseSlot& source = snapshot.slots[s];
    if (source.first_bone != expected_bone || source.bone_count == 0 ||
        source.bone_count > snapshot.bone_count - source.first_bone)
      return {};
    gx::AuthoredSlot slot;
    slot.bones.reserve(source.bone_count);
    for (uint32_t b = 0; b < source.bone_count; ++b) {
      const MuNativePoseBone& bone = snapshot.bones[source.first_bone + b];
      if (bone.chain >= chains.size() || !std::isfinite(bone.weight) || !finite_values(bone.envelope, 12)) return {};
      gx::AuthoredBone target;
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
}  // namespace ref

namespace {
// Chain objects of one frame: the reference's and the new code's must pair one to one.
struct Sharing {
  std::map<const AuthoredPose*, const AuthoredPose*> forward, backward;
  bool pair(const AuthoredPose* a, const AuthoredPose* b) {
    if (!a || !b) return !a && !b;
    auto f = forward.emplace(a, b), r = backward.emplace(b, a);
    return f.first->second == b && r.first->second == a;
  }
};

bool same_bits(const void* a, const void* b, size_t n) { return std::memcmp(a, b, n) == 0; }

bool same_joint_value(const AuthoredJoint& a, const AuthoredJoint& b) {
  if (a.generation != b.generation || a.flags != b.flags || a.anim_flags != b.anim_flags || a.quaternion != b.quaternion ||
      !same_bits(&a.scale, &b.scale, sizeof a.scale) || !same_bits(&a.rotation, &b.rotation, sizeof a.rotation) ||
      !same_bits(&a.translation, &b.translation, sizeof a.translation) || !same_bits(&a.quat, &b.quat, sizeof a.quat) ||
      !same_bits(&a.world, &b.world, sizeof a.world) || !same_bits(&a.frame, &b.frame, 4) || !same_bits(&a.rate, &b.rate, 4) ||
      !same_bits(&a.end, &b.end, 4) || !same_bits(&a.rewind, &b.rewind, 4) || a.tracks.size() != b.tracks.size() ||
      a.constraints.size() != b.constraints.size())
    return false;
  for (size_t t = 0; t < a.tracks.size(); ++t) {
    const auto &x = a.tracks[t], &y = b.tracks[t];
    if (x.start_frame != y.start_frame || x.channel != y.channel || x.value_format != y.value_format ||
        x.slope_format != y.slope_format || !NativeMelee::SameBytes(x, y))
      return false;
  }
  return true;
}

bool same_chain(const AuthoredPose* a, const AuthoredPose* b, Sharing& sharing) {
  if (!sharing.pair(a, b)) return false;
  if (!a) return true;
  if (a->joints.size() != b->joints.size()) return false;
  for (size_t i = 0; i < a->joints.size(); ++i)
    if (!same_joint_value(a->joints[i], b->joints[i])) return false;
  return true;
}

bool same_pose(const AuthoredPose& a, const AuthoredPose& b, Sharing& sharing) {
  if (a.envelope != b.envelope || a.has_view != b.has_view || a.quake != b.quake || a.model_shift != b.model_shift ||
      !same_bits(&a.quake_shift, &b.quake_shift, sizeof a.quake_shift) || !same_bits(&a.view, &b.view, sizeof a.view) ||
      a.right_kind != b.right_kind || !same_bits(&a.right_envelope, &b.right_envelope, sizeof a.right_envelope) ||
      !a.joints.empty() || !b.joints.empty() || a.slots.size() != b.slots.size())
    return false;
  if (!same_chain(a.chain.get(), b.chain.get(), sharing) ||
      !same_chain(a.right_chain_m.get(), b.right_chain_m.get(), sharing) ||
      !same_chain(a.right_chain_x.get(), b.right_chain_x.get(), sharing))
    return false;
  for (size_t s = 0; s < a.slots.size(); ++s) {
    if (a.slots[s].bones.size() != b.slots[s].bones.size()) return false;
    for (size_t k = 0; k < a.slots[s].bones.size(); ++k) {
      const auto &x = a.slots[s].bones[k], &y = b.slots[s].bones[k];
      if (!same_bits(&x.weight, &y.weight, 4) || !same_bits(&x.envelope, &y.envelope, sizeof x.envelope) ||
          !same_chain(x.chain.get(), y.chain.get(), sharing))
        return false;
    }
  }
  return true;
}

// A joint of the random pool, with its own tracks.
struct PoolJoint {
  MuNativePoseJoint joint{};
  std::vector<MuNativePoseTrack> tracks;
  std::vector<std::vector<uint8_t>> bytes;
};

// One snapshot's arrays, built from pool joints in order (tracks contiguous in joint order).
struct Built {
  std::vector<MuNativePoseJoint> joints;
  std::vector<MuNativePoseTrack> tracks;
  std::vector<uint8_t> bytes;
  std::vector<MuNativePoseChain> chains;
  std::vector<uint32_t> links;
  std::vector<MuNativePoseBone> bones;
  std::vector<MuNativePoseSlot> slots;
  MuNativePoseSnapshot s{};
};

void add_joint(Built& b, const PoolJoint& p) {
  MuNativePoseJoint j = p.joint;
  j.first_track = static_cast<uint32_t>(b.tracks.size());
  j.track_count = static_cast<uint32_t>(p.tracks.size());
  for (size_t t = 0; t < p.tracks.size(); ++t) {
    MuNativePoseTrack tr = p.tracks[t];
    tr.byte_offset = static_cast<uint32_t>(b.bytes.size());
    tr.byte_length = static_cast<uint32_t>(p.bytes[t].size());
    b.bytes.insert(b.bytes.end(), p.bytes[t].begin(), p.bytes[t].end());
    b.tracks.push_back(tr);
  }
  b.joints.push_back(j);
}

void finish(Built& b) {
  b.s.joint_count = static_cast<uint32_t>(b.joints.size());
  b.s.joints = b.joints.data();
  b.s.track_count = static_cast<uint32_t>(b.tracks.size());
  b.s.tracks = b.tracks.empty() ? nullptr : b.tracks.data();
  b.s.track_byte_count = static_cast<uint32_t>(b.bytes.size());
  b.s.track_bytes = b.bytes.empty() ? nullptr : b.bytes.data();
  b.s.chain_count = static_cast<uint32_t>(b.chains.size());
  b.s.chains = b.chains.empty() ? nullptr : b.chains.data();
  b.s.link_count = static_cast<uint32_t>(b.links.size());
  b.s.links = b.links.empty() ? nullptr : b.links.data();
  b.s.bone_count = static_cast<uint32_t>(b.bones.size());
  b.s.bones = b.bones.empty() ? nullptr : b.bones.data();
  b.s.slot_count = static_cast<uint32_t>(b.slots.size());
  b.s.slots = b.slots.empty() ? nullptr : b.slots.data();
}
}  // namespace

int main() {
  std::mt19937 rng(20261105);
  auto pick = [&](uint32_t n) { return static_cast<uint32_t>(rng() % n); };
  auto chance = [&](uint32_t per_thousand) { return pick(1000) < per_thousand; };
  uint64_t compared = 0, accepted = 0, shared = 0, rejected = 0;

  for (int frame = 0; frame < 400; ++frame) {
    // This frame's joint pool: 24 joints, a few tracks each; poses advance between frames.
    std::vector<PoolJoint> pool(24);
    for (uint32_t i = 0; i < pool.size(); ++i) {
      PoolJoint& p = pool[i];
      p.joint.generation = i + 1;
      p.joint.flags = (i % 5 == 0) ? 0x20000u : (i % 3) << 4;
      p.joint.anim_flags = i & 3;
      for (int k = 0; k < 3; ++k) p.joint.scale[k] = 1.0f + 0.01f * static_cast<float>(i % 4);
      for (int k = 0; k < 3; ++k) p.joint.rotation[k] = 0.1f * static_cast<float>((i + k + frame) % 7);
      for (int k = 0; k < 3; ++k) p.joint.translation[k] = static_cast<float>(i) - static_cast<float>(k);
      p.joint.quaternion[3] = 1.0f;
      for (int k = 0; k < 12; ++k) p.joint.world[k] = (k % 5 == 0) ? 1.0f : 0.0f;
      p.joint.world[3] = static_cast<float>(frame % 3);
      p.joint.frame = static_cast<float>((frame + i) % 40);
      p.joint.rate = 1.0f;
      p.joint.end = 40.0f;
      const uint32_t tracks = pick(4);
      for (uint32_t t = 0; t < tracks; ++t) {
        MuNativePoseTrack tr{};
        tr.start_frame = static_cast<int16_t>(t * 3);
        tr.channel = static_cast<uint8_t>(1 + t);
        tr.value_format = static_cast<uint8_t>(t & 1);
        tr.slope_format = static_cast<uint8_t>((t + 1) & 1);
        p.tracks.push_back(tr);
        std::vector<uint8_t> bytes(t == 2 ? 0 : 4 + pick(12));
        for (auto& v : bytes) v = static_cast<uint8_t>(rng());
        p.bytes.push_back(std::move(bytes));
      }
    }
    gx::native_pose_bridge_frame_reset();
    ref::frame_reset();
    Sharing sharing;
    std::vector<std::shared_ptr<const AuthoredPose>> keep_ref, keep_new;   // chains live for the frame, as in the game

    for (int n = 0; n < 160; ++n) {
      Built b;
      const bool envelope = chance(400);
      b.s.kind = envelope ? MU_NATIVE_POSE_ENVELOPE : MU_NATIVE_POSE_RIGID;
      b.s.has_view = envelope ? 1 : pick(2);
      for (int k = 0; k < 12; ++k) b.s.view[k] = (k % 5 == 0) ? 1.0f : 0.0f;
      if (chance(150)) { b.s.quake = MU_NATIVE_POSE_VIEW_QUAKE; b.s.quake_shift[0] = 1.5f; b.s.quake_shift[1] = -0.5f; }
      // A chain walks down the pool from a few common roots, so most snapshots repeat an earlier one.
      const uint32_t root = pick(3) * 8, length = 1 + pick(6);
      std::vector<PoolJoint> chosen;
      for (uint32_t i = 0; i < length; ++i) chosen.push_back(pool[(root + i) % pool.size()]);
      // Perturbations: a value differs, a sign of zero differs, a track byte differs, or the payload is broken.
      if (chance(120)) chosen[pick(length)].joint.frame += 1.0f;
      if (chance(80)) chosen[pick(length)].joint.world[7] = -0.0f;
      if (chance(80)) { auto& p = chosen[pick(length)]; if (!p.bytes.empty() && !p.bytes[0].empty()) p.bytes[0][0] ^= 1; }
      if (chance(40)) { auto& p = chosen[pick(length)]; if (!p.tracks.empty()) p.tracks[0].start_frame += 1; }
      if (chance(20)) chosen[pick(length)].joint.world[pick(12)] = std::numeric_limits<float>::quiet_NaN();
      if (chance(10)) chosen[pick(length)].joint.frame = std::numeric_limits<float>::infinity();
      if (chance(10)) chosen[pick(length)].joint.rate = -std::numeric_limits<float>::infinity();
      if (chance(10)) chosen[pick(length)].joint.scale[pick(3)] = std::numeric_limits<float>::denorm_min();   // finite
      if (chance(10)) chosen[pick(length)].joint.translation[pick(3)] = std::numeric_limits<float>::max();   // finite
      if (chance(15)) chosen[pick(length)].joint.generation = 0;
      if (chance(15)) { auto& p = chosen[pick(length)]; if (!p.tracks.empty()) p.tracks[0].channel = 4; }
      for (const auto& p : chosen) add_joint(b, p);
      if (chance(10) && !b.tracks.empty()) b.tracks.back().byte_offset = b.s.track_byte_count + 1000;
      if (envelope) {
        // Chains are prefixes and branches of the chosen joints; slots weight them.
        const uint32_t chains = 1 + pick(3);
        for (uint32_t c = 0; c < chains; ++c) {
          MuNativePoseChain ch{static_cast<uint32_t>(b.links.size()), 1 + pick(length)};
          for (uint32_t l = 0; l < ch.link_count; ++l) b.links.push_back(l);
          if (c == 2 && length > 1) b.links.back() = length - 1;   // a branch reusing a deeper joint
          b.chains.push_back(ch);
        }
        if (chance(15)) b.links[pick(static_cast<uint32_t>(b.links.size()))] = length + 3;   // out of range
        const uint32_t slots = 1 + pick(3);
        for (uint32_t s = 0; s < slots; ++s) {
          MuNativePoseSlot slot{static_cast<uint32_t>(b.bones.size()), 1 + pick(2)};
          for (uint32_t k = 0; k < slot.bone_count; ++k) {
            MuNativePoseBone bone{};
            bone.chain = pick(chains);
            bone.weight = k ? 0.25f : 0.75f;
            if (chance(15)) bone.weight = std::numeric_limits<float>::infinity();
            if (chance(15)) bone.envelope[pick(12)] = std::numeric_limits<float>::quiet_NaN();
            for (int e = 0; e < 12; ++e) bone.envelope[e] = (e % 5 == 0) ? 1.0f : 0.0f;
            b.bones.push_back(bone);
          }
          b.slots.push_back(slot);
        }
        b.s.right_kind = pick(4);
        b.s.right_chain_m = pick(chains);
        b.s.right_chain_x = chance(20) ? chains + 1 : pick(chains);
        for (int e = 0; e < 12; ++e) b.s.right_envelope[e] = (e % 5 == 0) ? 1.0f : 0.0f;
      }
      finish(b);
      if (chance(10)) b.s.kind = envelope ? MU_NATIVE_POSE_RIGID : MU_NATIVE_POSE_ENVELOPE;   // wrong converter

      const auto old_pose = envelope ? ref::copy_native_envelope_snapshot(b.s) : ref::copy_native_pose_snapshot(b.s);
      const auto new_pose = envelope ? gx::copy_native_envelope_snapshot(b.s) : gx::copy_native_pose_snapshot(b.s);
      ++compared;
      if (!old_pose != !new_pose) {
        std::fprintf(stderr, "frame %d snapshot %d: reference %s, new %s\n", frame, n,
                     old_pose ? "accepted" : "rejected", new_pose ? "accepted" : "rejected");
        return 1;
      }
      if (!old_pose) { ++rejected; continue; }
      ++accepted;
      const size_t before = sharing.forward.size();
      if (!same_pose(*old_pose, *new_pose, sharing)) {
        std::fprintf(stderr, "frame %d snapshot %d: captured pose or chain sharing differs\n", frame, n);
        return 1;
      }
      if (sharing.forward.size() == before) ++shared;
      keep_ref.push_back(old_pose);
      keep_new.push_back(new_pose);
    }
  }
  if (accepted < compared / 3 || rejected == 0 || shared < accepted / 4) {
    std::fprintf(stderr, "test did not exercise the cases: %llu compared, %llu accepted, %llu rejected, %llu reused chains\n",
                 (unsigned long long)compared, (unsigned long long)accepted, (unsigned long long)rejected, (unsigned long long)shared);
    return 1;
  }
  std::printf("pose bridge matches the copy-then-compare reference: %llu snapshots, %llu accepted, %llu rejected, %llu reused a chain\n",
              (unsigned long long)compared, (unsigned long long)accepted, (unsigned long long)rejected, (unsigned long long)shared);
  return 0;
}
