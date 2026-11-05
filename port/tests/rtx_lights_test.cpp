// The path tracer's light and material conversion (rtx_lights.h).
// SPDX-License-Identifier: GPL-2.0-or-later
#include "rtx_lights.h"

#include "../runtime/abi/mu_rtx_tokens.h"

#include <cmath>
#include <cstdio>
#include <cstring>

namespace {

int g_failures = 0;
void check(bool ok, const char* what) {
  if (!ok) {
    std::fprintf(stderr, "rtx lights test failed: %s\n", what);
    ++g_failures;
  }
}
bool near(float a, float b, float eps = 1e-4f) { return std::fabs(a - b) <= eps; }
uint32_t bits(float f) { uint32_t u; std::memcpy(&u, &f, 4); return u; }

}  // namespace

int main() {
  // GXInitLightDistAttn's three falloffs at reference distance 10, brightness 0.5.
  float k[3];
  gx::rtx_distance_attenuation(10.0f, 0.5f, 1, k);
  check(near(k[0], 1.0f) && near(k[1], 0.1f) && near(k[2], 0.0f), "gentle falloff");
  gx::rtx_distance_attenuation(10.0f, 0.5f, 2, k);
  check(near(k[1], 0.05f) && near(k[2], 0.005f), "medium falloff");
  gx::rtx_distance_attenuation(10.0f, 0.5f, 3, k);
  check(near(k[1], 0.0f) && near(k[2], 0.01f), "steep falloff");
  gx::rtx_distance_attenuation(10.0f, 0.5f, 0, k);
  check(near(k[0], 1.0f) && near(k[1], 0.0f) && near(k[2], 0.0f), "no falloff");

  gx::RtxFrameSemantics s;
  gx::RtxLight sun;   // infinite: from (0, 100, 0) towards the origin (HSD ignores its interest)
  sun.type = 1; sun.resolved = MU_RTX_LIGHT_POSITION_RESOLVED;
  sun.color[0] = 255; sun.color[1] = 128; sun.color[2] = 0;
  sun.position[1] = 100.0f;
  gx::RtxLight ambient;
  ambient.type = 0; ambient.color[0] = ambient.color[1] = ambient.color[2] = 64;
  gx::RtxLight lost;  // a point light whose position was not resolved this frame
  lost.type = 2;
  gx::RtxLight spot;  // spot from (0, 10, 0) aiming at (0, 0, 0), 30 degree cutoff, medium falloff
  spot.type = 3; spot.resolved = MU_RTX_LIGHT_POSITION_RESOLVED | MU_RTX_LIGHT_INTEREST_RESOLVED;
  spot.position[1] = 10.0f;
  spot.params[0] = bits(30.0f); spot.params[2] = bits(0.5f); spot.params[3] = bits(20.0f); spot.params[4] = 2;
  s.lights = {sun, ambient, lost, spot};
  s.light_sets.push_back({0, 4});
  std::vector<gx::RtxLightRecord> out;
  gx::rtx_build_lights(s, 0, out);
  check(out.size() == 3, "unresolved point light left out");
  check(out[0].type == gx::kRtxLightDirectional && near(out[0].direction[1], -1.0f), "sun direction");
  check(near(out[0].radiance[0], 1.0f) && out[0].radiance[1] > 0.2f && out[0].radiance[1] < 0.23f, "sun colour linearised");
  check(out[1].type == gx::kRtxLightAmbient && out[1].radiance[0] > 0.04f && out[1].radiance[0] < 0.06f, "ambient");
  check(out[2].type == gx::kRtxLightSpot && near(out[2].cos_cutoff, std::cos(30.0f * 3.14159265f / 180.0f)), "spot cone");
  check(near(out[2].attenuation[1], 0.025f) && near(out[2].attenuation[2], 0.00125f), "spot falloff");
  gx::rtx_build_lights(s, 3, out);
  check(out.empty(), "missing set");

  // Materials: a rough dielectric by default, the material's own specular, glossy reflection maps.
  uint32_t spec = 0;
  float rough = 0;
  gx::rtx_material_params(nullptr, &spec, &rough);
  check(spec == 0xFF0A0A0Au && near(rough, 0.85f), "default material");
  gx::RtxMaterial shiny;
  shiny.rendermode = 1u << 3;   // RENDER_SPECULAR
  shiny.specular[0] = shiny.specular[1] = shiny.specular[2] = 200;
  shiny.shininess = 50.0f;
  gx::rtx_material_params(&shiny, &spec, &rough);
  check((spec & 0xFF) == 200 && rough > 0.4f && rough < 0.5f, "specular material");
  gx::RtxMaterial mirror;
  mirror.tobj_count = 1;
  mirror.tobjs[0].coord = 1;    // TEX_COORD_REFLECTION
  gx::rtx_material_params(&mirror, &spec, &rough);
  check(rough <= 0.25f && (spec & 0xFF) == 140, "reflection-mapped material");

  if (g_failures) return 1;
  std::printf("rtx lights test passed\n");
  return 0;
}
