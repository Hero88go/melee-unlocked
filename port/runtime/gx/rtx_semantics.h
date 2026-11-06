// Render semantics for the ray-traced renderer (private FIFO tokens 0xF3-0xF6, host API 25).
//
// The native game says what each draw is: its HSD material and textures, the light set, the camera
// and the pass. The GX decoder takes the tokens out of the stream (parse_rtx_token), keeps them in
// per-frame tables (RtxFrameSemantics, carried by Frame) and gives every draw indices into them
// (RtxDrawInfo, carried by DrawCall). Nothing here changes what the raster renderer draws: the tokens
// are display information only, and they are only sent while set_rtx_semantics(true).
//
// The byte layout is defined once, in port/runtime/abi/mu_rtx_tokens.h.
// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once

#include <cstddef>
#include <cstdint>
#include <vector>

namespace gx {

// One HSD texture object of a material.
struct RtxTObj {
  uint8_t texmap = 0xFF;       // GX_TEXMAP0..7 the image is loaded into (DrawCall::textures index)
  uint8_t coord = 0;           // TEX_COORD_*: 0 UV, 1 REFLECTION, 2 HILIGHT, 3 SHADOW, 4 TOON, ...
  uint8_t texgen_src = 0;      // GX_TG_*
  uint32_t flags = 0;          // HSD_TObj.flags: TEX_LIGHTMAP_* role and colour/alpha map operation
  float blending = 0.0f;
  uint16_t width = 0, height = 0;
};

// One HSD material object (HSD_MObj + HSD_Material) as the game set it up for a DObj, with the
// textures its setup loaded (the shadow and toon textures the setup adds included).
struct RtxMaterial {
  uint32_t rendermode = 0;     // RENDER_* (mobj.h): DIFFUSE lit, SPECULAR, CONSTANT, VERTEX, XLU, ...
  uint8_t ambient[4]{}, diffuse[4]{}, specular[4]{};   // RGBA
  float alpha = 1.0f, shininess = 0.0f;
  uint32_t identity = 0;
  uint8_t tobj_count = 0;
  RtxTObj tobjs[8];
};

// One active HSD light of a camera's light set.
struct RtxLight {
  uint8_t type = 0;            // 0 ambient, 1 infinite, 2 point, 3 spot
  uint8_t diffuse_slot = 0xFF, specular_slot = 0xFF;   // GX light index, 0xFF none (8 = ambient slot)
  uint8_t resolved = 0;        // MU_RTX_LIGHT_POSITION_RESOLVED | MU_RTX_LIGHT_INTEREST_RESOLVED
  uint32_t flags = 0;          // HSD_LObj.flags
  uint8_t color[4]{};
  float position[3]{}, interest[3]{};   // world space
  uint32_t params[6]{};        // HSD_LObj.u raw words (spot/point parameters or raw attenuation)
  float shininess = 0.0f;
  uint32_t identity = 0;
};
struct RtxLightSet { uint32_t first = 0, count = 0; };

// One camera as the game made it current.
struct RtxCamera {
  uint8_t render_pass = 0;     // HSD_RP_SCREEN 0, TOPHALF 1, BOTTOMHALF 2, OFFSCREEN 3
  uint8_t projection = 0;      // PROJ_PERSPECTIVE 1, PROJ_FRUSTUM 2, PROJ_ORTHO 3
  uint32_t identity = 0;
  float view[12]{};            // world -> view, row major 3x4
  float eye[3]{}, interest[3]{};
  float projection_params[4]{};  // perspective: fov, aspect; frustum/ortho: top, bottom, left, right
  float near_plane = 0.0f, far_plane = 0.0f;
  float viewport[4]{};         // xmin, xmax, ymin, ymax
};

// The semantics of one frame. Draws point into these tables with RtxDrawInfo.
struct RtxFrameSemantics {
  std::vector<RtxMaterial> materials;
  std::vector<RtxLight> lights;
  std::vector<RtxLightSet> light_sets;
  std::vector<RtxCamera> cameras;
  uint32_t particle_draws = 0;
  bool present = false;        // at least one token arrived this frame
  void clear() {
    materials.clear(); lights.clear(); light_sets.clear(); cameras.clear();
    particle_draws = 0; present = false;
  }
};

// Per draw: indices into the frame's tables (-1 none) and the pass class (MU_RTX_PASS_*).
struct RtxDrawInfo {
  int16_t material = -1;
  int16_t light_set = -1;
  int16_t camera = -1;
  uint8_t pass = 0;
};

// The decoder's current context; reset at every frame boundary (the tables start empty then).
// Textures arrive just before the material they belong to (the setup loads them, then the material
// is announced), so they wait in `pending` until the next material begin takes them.
struct RtxDecodeState {
  int32_t material = -1, light_set = -1, camera = -1;
  uint8_t pass = 0;
  uint8_t pending_count = 0;
  RtxTObj pending[8];
  void reset() { *this = RtxDecodeState{}; }
};

inline bool is_rtx_token(uint8_t op) { return op >= 0xF3 && op <= 0xF6; }

// Parses the token at d (d[0] is 0xF3..0xF6) into `out` and `state`. Returns the bytes consumed;
// 0 when the token is not complete yet, with *need set to the bytes it needs; 1 for a malformed
// token (a count out of range or an unknown kind), so the decoder skips the opcode as it does an
// unknown command.
size_t parse_rtx_token(const uint8_t* d, size_t len, RtxDecodeState& state, RtxFrameSemantics& out,
                       size_t* need);

// The context a draw recorded now belongs to.
inline RtxDrawInfo rtx_draw_info(const RtxDecodeState& state) {
  RtxDrawInfo info;
  info.material = (int16_t)state.material;
  info.light_set = (int16_t)state.light_set;
  info.camera = (int16_t)state.camera;
  info.pass = state.pass;
  return info;
}

// Whether the native game is asked for the tokens (MuHostApi.native_rtx_semantics). Off by default;
// the renderer turns it on while path tracing runs, MELEE_RTX_SEMANTICS=1 forces it for tests.
void set_rtx_semantics(bool on);
bool rtx_semantics_enabled();

// Counters for the coverage log line ("rtx: semantics ...").
struct RtxSemanticsStats {
  uint64_t frames = 0, draws = 0, draws_with_material = 0, draws_with_camera = 0;
  uint64_t particle_draws = 0, materials = 0, lights = 0, cameras = 0, malformed = 0;
};
RtxSemanticsStats& rtx_semantics_stats();

}  // namespace gx
