// The GX colour combiner on the CPU, and bright effects as lights: the path tracer's effects and the
// stage's glowing art that light the scene.
// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include <array>
#include <cstdint>
#include <unordered_map>
#include <vector>

namespace gx {
struct DrawCall;
struct Frame;

// The draw's TEV (colour combiner) for one texel: what the surface shows under neutral light. Only
// texmap `own_map` is the given texel (other textures read white), the rasterized colour is `ras`
// (0..255); comparison modes are taken as plain adds. Output RGBA 0..255.
void tev_texel(const DrawCall& d, int own_map, const float texel[4], const float ras[4], float out[4]);

// A bright effect as a small sphere light: its world-space centre and size, its colour (linear 0..1,
// weighted by how it adds to the picture) and a rank for keeping the brightest.
struct FxLight { float pos[3]; float radius; float rgb[3]; float power; bool glow = false; };   // glow: the stage's own glowing art

class FxLightCollector {
 public:
  // One additive effect draw (fire, sparks, flashes, glows) as a light, its vertices taken to world
  // space with view_to_world. Its colour is its combiner over its texture's mean texel, so the
  // per-particle colour registers count. False when it has no usable vertices.
  // `segments` above 1 splits a long draw (a glowing strip: Battlefield's platform edges) into that many
  // lights along its longest side.
  bool add(const Frame& f, const DrawCall& d, const float view_to_world[12], std::vector<FxLight>& out, int segments = 1);

  // A stage piece that reads as glowing art without being additive: unlit, a thin strip (much longer
  // than it is wide) and bright and saturated, or bright white (Battlefield's neon edges, Final
  // Destination's lines). Added as a row of lights along it; false when it is not one.
  bool add_glow_strip(const Frame& f, const DrawCall& d, const float view_to_world[12], std::vector<FxLight>& out);

 private:
  std::unordered_map<uint64_t, std::array<float, 4>> texel_mean_;   // by texture content hash
  bool strip_test_ = false;   // add() for add_glow_strip: only long, thin, bright pieces
};

// Where the match is: the middle of the fighters this camera draws (world space through view_to_world).
// False when it draws none.
bool fx_focus(const Frame& f, int camera, const float view_to_world[12], float out[3]);

// Keeps the lights that can light the match: within `reach` of `focus`, the nearest and brightest first
// (power over 1 + (distance / 100)^2), at most `keep`. A stage's far background glows and sky effects
// (Final Destination's shooting stars, thousands of units away) took every slot from a fireball.
void fx_keep_near(std::vector<FxLight>& lights, const float focus[3], float reach, size_t keep);

}  // namespace gx
