// Render-semantics tokens 0xF3-0xF6: the parser the GX decoder hands them to (rtx_semantics.h). The
// byte layout is port/runtime/abi/mu_rtx_tokens.h.
// SPDX-License-Identifier: GPL-2.0-or-later
#include "rtx_semantics.h"

#include "../abi/mu_rtx_tokens.h"

#include <atomic>
#include <cstdlib>
#include <cstring>

namespace gx {
namespace {

std::atomic<bool> g_rtx_semantics{false};
RtxSemanticsStats g_rtx_stats;

inline uint32_t rd32(const uint8_t* p) {
  return (uint32_t)p[0] << 24 | (uint32_t)p[1] << 16 | (uint32_t)p[2] << 8 | p[3];
}
inline uint16_t rd16(const uint8_t* p) { return (uint16_t)((uint32_t)p[0] << 8 | p[1]); }
inline float rdf(const uint8_t* p) {
  const uint32_t bits = rd32(p);
  float value;
  std::memcpy(&value, &bits, sizeof value);
  return value;
}

// The draw tables index with int16_t; a frame never comes near this many entries.
constexpr size_t kMaxEntries = 0x7FFF;

}  // namespace

size_t parse_rtx_token(const uint8_t* d, size_t len, RtxDecodeState& state, RtxFrameSemantics& out,
                       size_t* need) {
  const auto incomplete = [need](size_t bytes) -> size_t {
    if (need) *need = bytes;
    return 0;
  };
  const auto malformed = []() -> size_t {
    ++g_rtx_stats.malformed;
    return 1;
  };
  if (len < 1) return incomplete(1);
  switch (d[0]) {
    case MU_RTX_TOKEN_MATERIAL: {
      if (len < 2) return incomplete(2);
      switch (d[1]) {
        case MU_RTX_MATERIAL_END:
          state.material = -1;
          state.pending_count = 0;
          out.present = true;
          return 2;
        case MU_RTX_MATERIAL_TEXTURES: {
          if (len < MU_RTX_TEXTURES_HEADER_BYTES) return incomplete(MU_RTX_TEXTURES_HEADER_BYTES);
          const uint32_t count = d[2];
          if (count > MU_RTX_MAX_TOBJS) return malformed();
          const size_t bytes = MU_RTX_TEXTURES_HEADER_BYTES + (size_t)count * MU_RTX_TOBJ_BYTES;
          if (len < bytes) return incomplete(bytes);
          for (uint32_t i = 0; i < count; ++i) {
            const uint8_t* t = d + MU_RTX_TEXTURES_HEADER_BYTES + i * MU_RTX_TOBJ_BYTES;
            RtxTObj& o = state.pending[i];
            o.texmap = t[0];
            o.coord = t[1];
            o.texgen_src = t[2];
            o.flags = rd32(t + 4);
            o.blending = rdf(t + 8);
            o.width = rd16(t + 12);
            o.height = rd16(t + 14);
          }
          state.pending_count = (uint8_t)count;
          out.present = true;
          return bytes;
        }
        case MU_RTX_MATERIAL_BEGIN: {
          if (len < MU_RTX_MATERIAL_HEADER_BYTES) return incomplete(MU_RTX_MATERIAL_HEADER_BYTES);
          out.present = true;
          if (out.materials.size() >= kMaxEntries) {
            state.material = -1;
            state.pending_count = 0;
            return MU_RTX_MATERIAL_HEADER_BYTES;
          }
          RtxMaterial m;
          m.rendermode = rd32(d + 4);
          std::memcpy(m.ambient, d + 8, 4);
          std::memcpy(m.diffuse, d + 12, 4);
          std::memcpy(m.specular, d + 16, 4);
          m.alpha = rdf(d + 20);
          m.shininess = rdf(d + 24);
          m.identity = rd32(d + 28);
          m.tobj_count = state.pending_count;
          for (uint32_t i = 0; i < state.pending_count; ++i) m.tobjs[i] = state.pending[i];
          state.pending_count = 0;
          state.material = (int32_t)out.materials.size();
          out.materials.push_back(m);
          ++g_rtx_stats.materials;
          return MU_RTX_MATERIAL_HEADER_BYTES;
        }
        default:
          return malformed();
      }
    }
    case MU_RTX_TOKEN_LIGHTS: {
      if (len < MU_RTX_LIGHTS_HEADER_BYTES) return incomplete(MU_RTX_LIGHTS_HEADER_BYTES);
      const uint32_t count = d[1];
      if (count > MU_RTX_MAX_LIGHTS) return malformed();
      const size_t bytes = MU_RTX_LIGHTS_HEADER_BYTES + (size_t)count * MU_RTX_LIGHT_BYTES;
      if (len < bytes) return incomplete(bytes);
      out.present = true;
      if (out.light_sets.size() >= kMaxEntries) {
        state.light_set = -1;
        return bytes;
      }
      RtxLightSet set;
      set.first = (uint32_t)out.lights.size();
      set.count = count;
      for (uint32_t i = 0; i < count; ++i) {
        const uint8_t* p = d + MU_RTX_LIGHTS_HEADER_BYTES + i * MU_RTX_LIGHT_BYTES;
        RtxLight l;
        l.type = p[0];
        l.diffuse_slot = p[1];
        l.specular_slot = p[2];
        l.resolved = p[3];
        l.flags = rd32(p + 4);
        std::memcpy(l.color, p + 8, 4);
        for (int k = 0; k < 3; ++k) {
          l.position[k] = rdf(p + 12 + 4 * k);
          l.interest[k] = rdf(p + 24 + 4 * k);
        }
        for (int k = 0; k < 6; ++k) l.params[k] = rd32(p + 36 + 4 * k);
        l.shininess = rdf(p + 60);
        l.identity = rd32(p + 64);
        out.lights.push_back(l);
      }
      state.light_set = (int32_t)out.light_sets.size();
      out.light_sets.push_back(set);
      g_rtx_stats.lights += count;
      return bytes;
    }
    case MU_RTX_TOKEN_CAMERA: {
      if (len < MU_RTX_CAMERA_BYTES) return incomplete(MU_RTX_CAMERA_BYTES);
      out.present = true;
      if (out.cameras.size() >= kMaxEntries) {
        state.camera = -1;
        return MU_RTX_CAMERA_BYTES;
      }
      RtxCamera c;
      c.render_pass = d[1];
      c.projection = d[2];
      c.identity = rd32(d + 4);
      for (int k = 0; k < 12; ++k) c.view[k] = rdf(d + 8 + 4 * k);
      for (int k = 0; k < 3; ++k) {
        c.eye[k] = rdf(d + 56 + 4 * k);
        c.interest[k] = rdf(d + 68 + 4 * k);
      }
      for (int k = 0; k < 4; ++k) c.projection_params[k] = rdf(d + 80 + 4 * k);
      c.near_plane = rdf(d + 96);
      c.far_plane = rdf(d + 100);
      for (int k = 0; k < 4; ++k) c.viewport[k] = rdf(d + 104 + 4 * k);
      state.camera = (int32_t)out.cameras.size();
      out.cameras.push_back(c);
      ++g_rtx_stats.cameras;
      return MU_RTX_CAMERA_BYTES;
    }
    case MU_RTX_TOKEN_PASS:
      if (len < 2) return incomplete(2);
      state.pass = d[1];
      out.present = true;
      return 2;
    default:
      return malformed();
  }
}

void set_rtx_semantics(bool on) { g_rtx_semantics.store(on, std::memory_order_relaxed); }

bool rtx_semantics_enabled() {
  // Test and measurement switch: the tokens without the renderer that uses them, to prove they
  // change nothing (replays, digests, pairing counters) with them on.
  static const bool forced = [] {
    const char* value = std::getenv("MELEE_RTX_SEMANTICS");
    return value && value[0] == '1';
  }();
  return forced || g_rtx_semantics.load(std::memory_order_relaxed);
}

RtxSemanticsStats& rtx_semantics_stats() { return g_rtx_stats; }

}  // namespace gx
