// The path tracer's light table (rtx_lights.h).
// SPDX-License-Identifier: GPL-2.0-or-later
#include "rtx_lights.h"

#include "gx_core.h"
#include "../abi/mu_rtx_tokens.h"

#include <cmath>
#include <cstring>
#include <unordered_map>

namespace gx {
namespace {

// HSD_LObj flags (sysdolphin forward.h).
constexpr uint32_t kLObjRawParam = 1u << 6;

float srgb_to_linear(uint8_t value) {
  const float c = value / 255.0f;
  return c <= 0.04045f ? c / 12.92f : std::pow((c + 0.055f) / 1.055f, 2.4f);
}

float as_float(uint32_t bits) {
  float f;
  std::memcpy(&f, &bits, sizeof f);
  return f;
}

bool normalize3(float v[3]) {
  const float len2 = v[0] * v[0] + v[1] * v[1] + v[2] * v[2];
  if (!(len2 > 1e-20f) || !std::isfinite(len2)) return false;
  const float k = 1.0f / std::sqrt(len2);
  v[0] *= k; v[1] *= k; v[2] *= k;
  return true;
}

}  // namespace

void rtx_material_params(const RtxMaterial* m, uint32_t* specular_rgba, float* roughness) {
  // A plain dielectric: F0 about 0.04.
  uint8_t spec[3] = {10, 10, 10};
  float rough = 0.85f;
  if (m) {
    constexpr uint32_t kRenderSpecular = 1u << 3;
    bool reflective = false;
    for (uint32_t i = 0; i < m->tobj_count && i < 8; ++i) reflective = reflective || m->tobjs[i].coord == 1;
    if (m->rendermode & kRenderSpecular) {
      for (int c = 0; c < 3; ++c) spec[c] = m->specular[c];
      // Blinn-Phong exponent to GGX: alpha = sqrt(2 / (n + 2)), perceptual roughness = sqrt(alpha).
      const float n = m->shininess > 0.0f ? m->shininess : 1.0f;
      const float alpha = std::sqrt(2.0f / (n + 2.0f));
      rough = std::fmin(std::fmax(std::sqrt(alpha), 0.08f), 0.95f);
    }
    if (reflective) {
      rough = std::fmin(rough, 0.25f);
      if (spec[0] < 64 && spec[1] < 64 && spec[2] < 64) spec[0] = spec[1] = spec[2] = 140;
    }
  }
  *specular_rgba = (uint32_t)spec[0] | ((uint32_t)spec[1] << 8) | ((uint32_t)spec[2] << 16) | (0xFFu << 24);
  *roughness = rough;
}

void rtx_distance_attenuation(float ref_distance, float ref_brightness, uint32_t func, float k[3]) {
  k[0] = 1.0f; k[1] = 0.0f; k[2] = 0.0f;
  if (!(ref_distance > 0.0f) || !(ref_brightness > 0.0f) || ref_brightness >= 1.0f) return;
  const float fall = (1.0f - ref_brightness) / ref_brightness;
  switch (func) {
    case 1: k[1] = fall / ref_distance; break;                               // gentle
    case 2: k[1] = 0.5f * fall / ref_distance;                               // medium
            k[2] = 0.5f * fall / (ref_distance * ref_distance); break;
    case 3: k[2] = fall / (ref_distance * ref_distance); break;             // steep
    default: break;                                                          // off
  }
}

int rtx_main_light_set(const Frame& frame, int camera) {
  if (frame.rtx.light_sets.empty()) return -1;
  for (const DrawCall& d : frame.draws)
    if (d.skinned && d.owner_player < 4 && d.rtx.camera == camera && d.rtx.light_set >= 0) return d.rtx.light_set;
  std::unordered_map<int, uint32_t> uses;
  int best = -1;
  uint32_t best_uses = 0;
  for (const DrawCall& d : frame.draws) {
    if (d.rtx.camera != camera || d.rtx.light_set < 0) continue;
    const uint32_t n = ++uses[d.rtx.light_set];
    if (n > best_uses) { best_uses = n; best = d.rtx.light_set; }
  }
  return best;
}

void rtx_build_lights(const RtxFrameSemantics& semantics, int set, std::vector<RtxLightRecord>& out) {
  out.clear();
  if (set < 0 || (size_t)set >= semantics.light_sets.size()) return;
  const RtxLightSet& ls = semantics.light_sets[(size_t)set];
  for (uint32_t i = 0; i < ls.count && ls.first + i < semantics.lights.size(); ++i) {
    const RtxLight& l = semantics.lights[ls.first + i];
    RtxLightRecord r{};
    r.type = l.type;
    r.flags = l.flags;
    r.cos_cutoff = -1.0f;
    for (int c = 0; c < 3; ++c) r.radiance[c] = srgb_to_linear(l.color[c]);
    r.attenuation[0] = 1.0f;
    const bool have_position = (l.resolved & MU_RTX_LIGHT_POSITION_RESOLVED) != 0;
    const bool have_interest = (l.resolved & MU_RTX_LIGHT_INTEREST_RESOLVED) != 0;
    std::memcpy(r.position, l.position, sizeof r.position);
    switch (l.type) {
      case kRtxLightAmbient:
        break;
      case kRtxLightDirectional: {
        // HSD puts an infinite light's GX light at position * 2^20 (setup_infinite_lightobj): it shines
        // from that direction towards the origin, and its interest is not used.
        if (!have_position) continue;
        for (int c = 0; c < 3; ++c) r.direction[c] = -l.position[c];
        if (!normalize3(r.direction)) continue;
        r.radius = 0.03f;   // angular radius: soft, sun-like shadow edges
        break;
      }
      case kRtxLightPoint:
      case kRtxLightSpot: {
        if (!have_position) continue;
        r.radius = 1.5f;
        if (l.flags & kLObjRawParam) {
          r.attenuation[0] = as_float(l.params[3]);
          r.attenuation[1] = as_float(l.params[4]);
          r.attenuation[2] = as_float(l.params[5]);
        } else {
          // HSD_LightPoint / HSD_LightSpot: cutoff, func, ref_br, ref_dist, dist_func.
          rtx_distance_attenuation(as_float(l.params[3]), as_float(l.params[2]), l.params[4], r.attenuation);
        }
        if (l.type == kRtxLightSpot) {
          if (!have_interest) continue;
          for (int c = 0; c < 3; ++c) r.direction[c] = l.interest[c] - l.position[c];
          if (!normalize3(r.direction)) continue;
          const float cutoff = as_float(l.params[0]);
          if (cutoff > 0.0f && cutoff < 90.0f) r.cos_cutoff = std::cos(cutoff * 3.14159265f / 180.0f);
        }
        break;
      }
      default:
        continue;
    }
    out.push_back(r);
  }
}

}  // namespace gx
