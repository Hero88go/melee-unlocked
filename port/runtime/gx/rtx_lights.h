// The path tracer's light table: the game's own lights (render semantics token 0xF4) in world space,
// in the layout the lighting integrators read (rtx_integrator.h).
// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once

#include "rtx_semantics.h"

#include <cstdint>
#include <vector>

namespace gx {

struct Frame;

enum RtxLightType : uint32_t {
  kRtxLightAmbient = 0,       // HSD LOBJ_AMBIENT: light from everywhere (the integrators treat it as sky)
  kRtxLightDirectional = 1,   // LOBJ_INFINITE: the sun of the stage
  kRtxLightPoint = 2,         // LOBJ_POINT
  kRtxLightSpot = 3,          // LOBJ_SPOT
};

// One light, 64 bytes, as a StructuredBuffer element.
struct RtxLightRecord {
  float position[3];          // world (point, spot)
  uint32_t type;              // RtxLightType
  float direction[3];         // world, unit: the way the light travels (directional, spot)
  float cos_cutoff;           // spot cone, cos of the half angle (-1 none)
  float radiance[3];          // linear RGB of the game's colour (0..1 per channel before any scale)
  float radius;               // soft-shadow size: sphere radius (point, spot) or angular radius (directional)
  float attenuation[3];       // GX distance attenuation k0 + k1 d + k2 d^2 (point, spot)
  uint32_t flags;             // HSD_LObj flags (LOBJ_DIFFUSE 0x4, LOBJ_SPECULAR 0x8)
};
static_assert(sizeof(RtxLightRecord) == 64, "light record layout");

// The light set the match is lit with: that of the first fighter model drawn with `camera`, else
// the one most of that camera's draws use. -1 when the frame carries none.
int rtx_main_light_set(const Frame& frame, int camera);

// The lights of `set` as records. Lights whose world position the game had not resolved this frame
// (and that need one) are left out. sRGB colours are linearised here.
void rtx_build_lights(const RtxFrameSemantics& semantics, int set, std::vector<RtxLightRecord>& out);

// The surface response the integrators use for an HSD material: specular colour (RGBA8, r in the low
// byte) and perceptual roughness. RENDER_SPECULAR gives the material's specular colour and a
// roughness from its shininess; a reflection-mapped texture (TEX_COORD_REFLECTION) makes the surface
// glossy, its fake reflection replaced by a traced one; everything else is a rough dielectric.
void rtx_material_params(const RtxMaterial* material, uint32_t* specular_rgba, float* roughness);

// GXInitLightDistAttn: the k coefficients for a reference distance and brightness and a falloff kind
// (0 off, 1 gentle, 2 medium, 3 steep).
void rtx_distance_attenuation(float ref_distance, float ref_brightness, uint32_t func, float k[3]);

}  // namespace gx
