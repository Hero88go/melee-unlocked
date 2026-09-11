// Immutable authored joint channels captured on the simulation thread.
// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include "PackedAnimation.h"
#include <array>
#include <atomic>
#include <memory>
#include <unordered_map>
#include <vector>
namespace gx {
struct AuthoredJoint {
  uint64_t generation = 0;
  uint32_t flags = 0;
  std::array<float,3> scale{}, rotation{}, translation{};
  std::array<float,12> world{};
  float frame = 0, rate = 0, end = 0;
  std::vector<NativeMelee::PackedTrack> tracks;
};
struct AuthoredPose;
// One bone of a skinned matrix slot: its joint chain, blend weight and inverse-bind (envelope) matrix.
struct AuthoredBone { std::shared_ptr<const AuthoredPose> chain; float weight = 1; std::array<float,12> envelope{}; };
struct AuthoredSlot { std::vector<AuthoredBone> bones; };
struct AuthoredPose {
  std::vector<AuthoredJoint> joints;   // rigid draws and bone chains: root..joint
  // Envelope (skinned) draws: view matrix, skeleton root transform and the weighted bones per matrix slot.
  bool envelope = false;
  bool has_view = false;               // `view` holds the draw's camera (view) matrix
  std::array<float,12> view{};
  std::shared_ptr<const AuthoredPose> chain;   // rigid draws: the joint chain root..joint (shared per joint per frame)
  int right_kind = 0;                  // 0 none, 1 inverse(x.env), 2 inverse(x.world)*m.world, 3 inverse(x.world*x.env)*m.world
  std::array<float,12> right_envelope{};
  std::shared_ptr<const AuthoredPose> right_chain_m, right_chain_x;
  std::vector<AuthoredSlot> slots;
};
// Diagnostics: why capture/sampling declined a draw (indexed by rejection site; see the sources).
struct AuthoredStats { std::atomic<uint32_t> capture[24]{}; std::atomic<uint32_t> sample[24]{}; std::atomic<uint32_t> captured{0}, sampled{0}; };
AuthoredStats& authored_stats();
// Per-presented-frame cache of sampled joint chains: draws of one object share the chain.
struct AuthoredChain { bool ok = false; std::array<float,12> world{}, inverse_current{}; };
struct AuthoredPairHash { size_t operator()(const std::pair<const AuthoredPose*, const AuthoredPose*>& p) const {
  return std::hash<const void*>()(p.first) * 31u ^ std::hash<const void*>()(p.second); } };
using AuthoredCache = std::unordered_map<std::pair<const AuthoredPose*, const AuthoredPose*>, AuthoredChain, AuthoredPairHash>;
// Phase is [0,1] frames forward from current. Returns false at unsupported state or
// animation boundaries; callers must retain the current pose. Never calls guest code.
bool sample_authored(const AuthoredPose& previous, const AuthoredPose& current,
                     double phase, const float matrix[12], float result[12], float normals[9],
                     const float current_normals[9], AuthoredCache* cache = nullptr);
// Skinned draws: rebuilds every envelope matrix slot (and its normal matrix) at the fractional
// frame from the bones' sampled chains. `current_pos/current_nrm` are the draw's matrices (used
// to validate the reconstruction); outputs are the full 64-row position and 32-row normal arrays.
bool sample_authored_envelope(const AuthoredPose& previous, const AuthoredPose& current, double phase,
                              const float current_pos[256], const float current_nrm[96], float out_pos[256], float out_nrm[96],
                              AuthoredCache* cache);
}
