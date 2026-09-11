// Immutable authored joint channels captured on the simulation thread.
// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include "PackedAnimation.h"
#include <array>
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
struct AuthoredPose { std::vector<AuthoredJoint> joints; };
// Diagnostics: why capture/sampling declined a draw (indexed by rejection site; see the sources).
struct AuthoredStats { uint32_t capture[24] = {}; uint32_t sample[24] = {}; uint32_t captured = 0, sampled = 0; };
AuthoredStats& authored_stats();
// Per-presented-frame cache of sampled joint chains: draws of one object share the chain.
struct AuthoredChain { bool ok = false; std::array<float,12> world{}, inverse_current{}; };
struct AuthoredPairHash { size_t operator()(const std::pair<const AuthoredPose*, const AuthoredPose*>& p) const {
  return std::hash<const void*>()(p.first) * 31u ^ std::hash<const void*>()(p.second); } };
using AuthoredCache = std::unordered_map<std::pair<const AuthoredPose*, const AuthoredPose*>, AuthoredChain, AuthoredPairHash>;
// Returns false at unsupported state/animation discontinuities. Never calls guest code.
bool sample_authored(const AuthoredPose& previous, const AuthoredPose& current,
                     double phase, const float matrix[12], float result[12], float normals[9],
                     const float current_normals[9], AuthoredCache* cache = nullptr);
}
