// Render-semantics tokens 0xF3-0xF6 (port/runtime/abi/mu_rtx_tokens.h): the parser the GX decoder
// hands them to. Byte streams are built the way the native game writes them (big-endian).
// SPDX-License-Identifier: GPL-2.0-or-later
#include "rtx_semantics.h"

#include "../runtime/abi/mu_rtx_tokens.h"

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <vector>

namespace {

int g_failures = 0;
void check(bool ok, const char* what) {
  if (!ok) {
    std::fprintf(stderr, "rtx semantics test failed: %s\n", what);
    ++g_failures;
  }
}

struct Stream {
  std::vector<uint8_t> bytes;
  void u8(uint8_t v) { bytes.push_back(v); }
  void u16(uint16_t v) { u8((uint8_t)(v >> 8)); u8((uint8_t)v); }
  void u32(uint32_t v) { u16((uint16_t)(v >> 16)); u16((uint16_t)v); }
  void f32(float v) { uint32_t bits; std::memcpy(&bits, &v, 4); u32(bits); }
};

void textures(Stream& s, int count) {
  s.u8(MU_RTX_TOKEN_MATERIAL); s.u8(MU_RTX_MATERIAL_TEXTURES); s.u8((uint8_t)count); s.u8(0);
  for (int i = 0; i < count; ++i) {
    s.u8((uint8_t)i);          // texmap
    s.u8(i == 1 ? 1 : 0);      // coord: the second is a reflection map
    s.u8(4);                   // texgen source
    s.u8(0);
    s.u32(0x10u << i);         // flags
    s.f32(0.5f + i);
    s.u16((uint16_t)(64 << i)); s.u16(32);
  }
}

void material(Stream& s, uint32_t rendermode, uint32_t identity) {
  s.u8(MU_RTX_TOKEN_MATERIAL); s.u8(MU_RTX_MATERIAL_BEGIN); s.u8(0); s.u8(0);
  s.u32(rendermode);
  s.u32(0x10203040); s.u32(0xC0C0C0FF); s.u32(0xFFFFFFFF);
  s.f32(0.75f); s.f32(40.0f);
  s.u32(identity);
}

void lights(Stream& s) {
  s.u8(MU_RTX_TOKEN_LIGHTS); s.u8(2); s.u8(0); s.u8(0);
  // An infinite light in slot 0 with both world points resolved.
  s.u8(1); s.u8(0); s.u8(0xFF); s.u8(MU_RTX_LIGHT_POSITION_RESOLVED | MU_RTX_LIGHT_INTEREST_RESOLVED);
  s.u32(0x4); s.u32(0xFFEEDDFF);
  s.f32(1); s.f32(2); s.f32(3); s.f32(4); s.f32(5); s.f32(6);
  for (int k = 0; k < 6; ++k) s.u32(0x100 + k);
  s.f32(0); s.u32(0xAABBCCDD);
  // The ambient light in the ambient slot.
  s.u8(0); s.u8(8); s.u8(0xFF); s.u8(0);
  s.u32(0); s.u32(0x404040FF);
  for (int k = 0; k < 6; ++k) s.f32(0);
  for (int k = 0; k < 6; ++k) s.u32(0);
  s.f32(0); s.u32(0x11223344);
}

void camera(Stream& s) {
  s.u8(MU_RTX_TOKEN_CAMERA); s.u8(0); s.u8(1); s.u8(0);
  s.u32(0xCAFEF00D);
  for (int k = 0; k < 12; ++k) s.f32((float)k);
  s.f32(10); s.f32(20); s.f32(30);
  s.f32(0); s.f32(0); s.f32(-1);
  s.f32(30.0f); s.f32(1.2173333f); s.f32(0); s.f32(0);
  s.f32(1.0f); s.f32(32768.0f);
  s.f32(0); s.f32(640); s.f32(0); s.f32(480);
}

size_t feed(const Stream& s, gx::RtxDecodeState& state, gx::RtxFrameSemantics& out) {
  size_t at = 0, tokens = 0;
  while (at < s.bytes.size()) {
    size_t need = 0;
    const size_t used = gx::parse_rtx_token(s.bytes.data() + at, s.bytes.size() - at, state, out, &need);
    if (used == 0) {
      check(false, "complete stream reported incomplete");
      break;
    }
    at += used;
    ++tokens;
  }
  return tokens;
}

}  // namespace

int main() {
  check(MU_RTX_CAMERA_BYTES == 120, "camera layout size");
  check(MU_RTX_LIGHT_BYTES == 68, "light layout size");

  // Textures wait for the material; the material takes them; the end clears the context.
  {
    gx::RtxDecodeState state;
    gx::RtxFrameSemantics out;
    Stream s;
    textures(s, 2);
    material(s, 0x2000000Cu, 0x1234);
    const size_t tokens = feed(s, state, out);
    check(tokens == 2, "two tokens");
    check(out.present, "frame marked present");
    check(out.materials.size() == 1 && state.material == 0, "material recorded and current");
    const gx::RtxMaterial& m = out.materials[0];
    check(m.rendermode == 0x2000000Cu, "rendermode");
    check(m.ambient[0] == 0x10 && m.ambient[3] == 0x40 && m.diffuse[3] == 0xFF, "colours");
    check(m.alpha == 0.75f && m.shininess == 40.0f && m.identity == 0x1234, "alpha, shininess, identity");
    check(m.tobj_count == 2, "pending textures attached");
    check(m.tobjs[1].texmap == 1 && m.tobjs[1].coord == 1 && m.tobjs[1].flags == 0x20u, "second texture fields");
    check(m.tobjs[1].blending == 1.5f && m.tobjs[1].width == 128 && m.tobjs[1].height == 32, "texture size");
    check(state.pending_count == 0, "pending consumed");
    const gx::RtxDrawInfo info = gx::rtx_draw_info(state);
    check(info.material == 0 && info.camera == -1 && info.light_set == -1, "draw info");
    Stream end;
    end.u8(MU_RTX_TOKEN_MATERIAL); end.u8(MU_RTX_MATERIAL_END);
    feed(end, state, out);
    check(state.material == -1, "end clears the material");
  }

  // A material with no textures before it has none.
  {
    gx::RtxDecodeState state;
    gx::RtxFrameSemantics out;
    Stream s;
    material(s, 0x4, 1);
    feed(s, state, out);
    check(out.materials.size() == 1 && out.materials[0].tobj_count == 0, "material without textures");
  }

  // Lights, camera and pass.
  {
    gx::RtxDecodeState state;
    gx::RtxFrameSemantics out;
    Stream s;
    lights(s);
    camera(s);
    s.u8(MU_RTX_TOKEN_PASS); s.u8(MU_RTX_PASS_PARTICLES);
    check(feed(s, state, out) == 3, "three tokens");
    check(out.light_sets.size() == 1 && out.light_sets[0].first == 0 && out.light_sets[0].count == 2, "light set");
    check(out.lights.size() == 2, "two lights");
    const gx::RtxLight& sun = out.lights[0];
    check(sun.type == 1 && sun.diffuse_slot == 0 && sun.specular_slot == 0xFF, "light type and slots");
    check(sun.resolved == 3 && sun.position[2] == 3.0f && sun.interest[0] == 4.0f, "light world points");
    check(sun.color[0] == 0xFF && sun.color[2] == 0xDD && sun.params[5] == 0x105 && sun.identity == 0xAABBCCDD,
          "light colour, params, identity");
    check(out.lights[1].type == 0 && out.lights[1].diffuse_slot == 8, "ambient light");
    check(out.cameras.size() == 1 && state.camera == 0, "camera recorded");
    const gx::RtxCamera& c = out.cameras[0];
    check(c.render_pass == 0 && c.projection == 1 && c.identity == 0xCAFEF00D, "camera header");
    check(c.view[0] == 0.0f && c.view[11] == 11.0f, "view matrix");
    check(c.eye[1] == 20.0f && c.interest[2] == -1.0f, "eye and interest");
    check(c.projection_params[0] == 30.0f && c.near_plane == 1.0f && c.far_plane == 32768.0f, "projection");
    check(c.viewport[1] == 640.0f && c.viewport[3] == 480.0f, "viewport");
    check(state.pass == MU_RTX_PASS_PARTICLES && gx::rtx_draw_info(state).pass == MU_RTX_PASS_PARTICLES, "pass");
    state.reset();
    check(state.material == -1 && state.camera == -1 && state.pass == 0 && state.pending_count == 0, "reset");
  }

  // Byte-at-a-time delivery: every prefix asks for exactly the token's size, as the FIFO decoder
  // retries once that many bytes have arrived.
  {
    Stream s;
    camera(s);
    gx::RtxDecodeState state;
    gx::RtxFrameSemantics out;
    bool prefixes_ok = true;
    for (size_t len = 1; len < s.bytes.size(); ++len) {
      size_t need = 0;
      if (gx::parse_rtx_token(s.bytes.data(), len, state, out, &need) != 0 || need > s.bytes.size() || need <= len)
        prefixes_ok = false;
    }
    check(prefixes_ok, "camera prefixes are incomplete with a growing need");
    check(out.cameras.empty(), "nothing recorded from a prefix");
    Stream t;
    textures(t, 3);
    size_t need = 0;
    check(gx::parse_rtx_token(t.bytes.data(), 4, state, out, &need) == 0 && need == 4 + 3 * MU_RTX_TOBJ_BYTES,
          "texture header asks for its entries");
  }

  // Malformed: counts out of range and unknown kinds skip only the opcode.
  {
    gx::RtxDecodeState state;
    gx::RtxFrameSemantics out;
    const uint8_t too_many_textures[] = {MU_RTX_TOKEN_MATERIAL, MU_RTX_MATERIAL_TEXTURES, 9, 0};
    const uint8_t too_many_lights[] = {MU_RTX_TOKEN_LIGHTS, 10, 0, 0};
    const uint8_t unknown_kind[] = {MU_RTX_TOKEN_MATERIAL, 7};
    size_t need = 0;
    check(gx::parse_rtx_token(too_many_textures, sizeof too_many_textures, state, out, &need) == 1, "textures count");
    check(gx::parse_rtx_token(too_many_lights, sizeof too_many_lights, state, out, &need) == 1, "lights count");
    check(gx::parse_rtx_token(unknown_kind, sizeof unknown_kind, state, out, &need) == 1, "material kind");
    check(gx::rtx_semantics_stats().malformed >= 3, "malformed counted");
    check(out.materials.empty() && out.lights.empty(), "nothing recorded from malformed tokens");
  }

  // Off unless asked (MELEE_RTX_SEMANTICS forces it for measurements).
  {
    const char* forced = std::getenv("MELEE_RTX_SEMANTICS");
    if (!(forced && forced[0] == '1')) {
      check(!gx::rtx_semantics_enabled(), "off by default");
      gx::set_rtx_semantics(true);
      check(gx::rtx_semantics_enabled(), "on when set");
      gx::set_rtx_semantics(false);
      check(!gx::rtx_semantics_enabled(), "off again");
    }
  }

  if (g_failures) return 1;
  std::printf("rtx semantics test passed\n");
  return 0;
}
