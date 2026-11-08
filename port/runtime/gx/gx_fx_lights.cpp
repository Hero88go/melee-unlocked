// The GX colour combiner on the CPU, and bright effects as lights (gx_fx_lights.h).
// SPDX-License-Identifier: GPL-2.0-or-later
#include "gx_fx_lights.h"
#include "gx_core.h"
#include "gx_texture.h"
#include "texture_snapshot.h"

#include <algorithm>
#include <cmath>

namespace gx {

// The draw's TEV (colour combiner) for one texel on the CPU, the way Melee composes its colours from
// textures, the rasterized colour and its constant and register colours: what the surface shows under
// neutral light. Remix gets that as its texture, so constant-coloured lines and tinted stages keep their
// colours. Inputs follow GX: only the texmap `own_map` is the given texel (other textures read white),
// the rasterized colour is `ras` (0..255), comparison modes are passed as plain adds.
void tev_texel(const DrawCall& d, int own_map, const float texel[4], const float ras[4], float out[4]) {
  const BPMemory& bp = d.bp;
  float reg[4][4];
  for (int r = 0; r < 4; ++r)
    for (int k = 0; k < 4; ++k) reg[r][k] = (float)d.tev_colors[r][k];
  const uint32_t stages = bp.numtevstages() + 1;
  static const float kconst[8] = {255.0f, 223.0f, 191.0f, 159.0f, 128.0f, 96.0f, 64.0f, 32.0f};
  for (uint32_t n = 0; n < stages && n < 16; ++n) {
    float tex[4] = {255, 255, 255, 255};
    if (bp.order_enable((int)n) && bp.order_texmap((int)n) == own_map)
      for (int k = 0; k < 4; ++k) tex[k] = texel[k];
    float konst[4];
    const int kc = bp.ksel_kc((int)n), ka = bp.ksel_ka((int)n);
    auto kval = [&](int sel, int ch) -> float {
      if (sel < 8) return kconst[sel];
      if (sel >= 12 && sel < 16) return (float)d.tev_kcolors[sel - 12][ch];
      if (sel >= 16 && sel < 32) return (float)d.tev_kcolors[(sel - 16) & 3][(sel - 16) >> 2];
      return 0.0f;
    };
    for (int k = 0; k < 3; ++k) konst[k] = kval(kc, k);
    konst[3] = kval(ka < 12 ? ka : (ka >= 28 ? ka : ka), 3);
    if (ka >= 16 && ka < 32) konst[3] = (float)d.tev_kcolors[(ka - 16) & 3][(ka - 16) >> 2];
    const uint32_t cc = bp.tev_color((int)n), ac = bp.tev_alpha((int)n);
    auto cin = [&](uint32_t sel, int ch) -> float {
      switch (sel) {
        case 0: return reg[0][ch]; case 1: return reg[0][3]; case 2: return reg[1][ch]; case 3: return reg[1][3];
        case 4: return reg[2][ch]; case 5: return reg[2][3]; case 6: return reg[3][ch]; case 7: return reg[3][3];
        case 8: return tex[ch]; case 9: return tex[3]; case 10: return ras[ch]; case 11: return ras[3];
        case 12: return 255.0f; case 13: return 128.0f; case 14: return konst[ch]; default: return 0.0f;
      }
    };
    auto ain = [&](uint32_t sel) -> float {
      switch (sel) {
        case 0: return reg[0][3]; case 1: return reg[1][3]; case 2: return reg[2][3]; case 3: return reg[3][3];
        case 4: return tex[3]; case 5: return ras[3]; case 6: return konst[3]; default: return 0.0f;
      }
    };
    static const float shifts[4] = {1.0f, 2.0f, 4.0f, 0.5f}, biases[3] = {0.0f, 128.0f, -128.0f};
    float result[4];
    for (int ch = 0; ch < 3; ++ch) {
      const float a = cin(bits(cc, 12, 4), ch), b = cin(bits(cc, 8, 4), ch), c = cin(bits(cc, 4, 4), ch), dd = cin(bits(cc, 0, 4), ch);
      const float lerp_v = (a * (255.0f - c) + b * c) / 255.0f;
      const uint32_t bias = bits(cc, 16, 2);
      float v = dd + (bits(cc, 18, 1) ? -lerp_v : lerp_v) + (bias < 3 ? biases[bias] : 0.0f);
      v *= shifts[bits(cc, 20, 2)];
      result[ch] = bits(cc, 19, 1) ? std::clamp(v, 0.0f, 255.0f) : std::clamp(v, -1024.0f, 1023.0f);
    }
    {
      const float a = ain(bits(ac, 13, 3)), b = ain(bits(ac, 10, 3)), c = ain(bits(ac, 7, 3)), dd = ain(bits(ac, 4, 3));
      const float lerp_v = (a * (255.0f - c) + b * c) / 255.0f;
      const uint32_t bias = bits(ac, 16, 2);
      float v = dd + (bits(ac, 18, 1) ? -lerp_v : lerp_v) + (bias < 3 ? biases[bias] : 0.0f);
      v *= shifts[bits(ac, 20, 2)];
      result[3] = bits(ac, 19, 1) ? std::clamp(v, 0.0f, 255.0f) : std::clamp(v, -1024.0f, 1023.0f);
    }
    const uint32_t cdest = bits(cc, 22, 2), adest = bits(ac, 22, 2);
    for (int ch = 0; ch < 3; ++ch) reg[cdest][ch] = result[ch];
    reg[adest][3] = result[3];
    if (n + 1 == stages) for (int k = 0; k < 4; ++k) out[k] = std::clamp(k < 3 ? reg[cdest][k] : reg[adest][3], 0.0f, 255.0f);
  }
}

bool FxLightCollector::add(const Frame& f, const DrawCall& d, const float vw[12], std::vector<FxLight>& out, int segments) {
  if (d.vertex_count == 0 || d.first_vertex + (size_t)d.vertex_count > f.vertices.size()) return false;
  float lo[3] = {1e30f, 1e30f, 1e30f}, hi[3] = {-1e30f, -1e30f, -1e30f};
  for (uint32_t i = 0; i < d.vertex_count; ++i) {
    const Vertex& v = f.vertices[d.first_vertex + i];
    const int row = (int)(v.posmtx & 0x3F);
    if (row + 2 >= 64) continue;
    const float* pm = &d.posMatrices[row * 4];
    float view[3], w[3];
    for (int k = 0; k < 3; ++k) view[k] = pm[k * 4] * v.pos[0] + pm[k * 4 + 1] * v.pos[1] + pm[k * 4 + 2] * v.pos[2] + pm[k * 4 + 3];
    for (int k = 0; k < 3; ++k) {
      w[k] = vw[k * 4] * view[0] + vw[k * 4 + 1] * view[1] + vw[k * 4 + 2] * view[2] + vw[k * 4 + 3];
      lo[k] = std::min(lo[k], w[k]); hi[k] = std::max(hi[k], w[k]);
    }
  }
  if (lo[0] > hi[0]) return false;
  // The colour source the combiner reads (vertex colours or the material register), with alpha.
  float ras[4] = {255.0f, 255.0f, 255.0f, 255.0f};
  if ((d.xf_regs[0x0E] & 1) && (d.components & VB_HAS_COL0)) {
    double sum[4] = {0, 0, 0, 0};
    for (uint32_t i = 0; i < d.vertex_count; ++i)
      for (int k = 0; k < 4; ++k) sum[k] += f.vertices[d.first_vertex + i].col0[k];
    for (int k = 0; k < 4; ++k) ras[k] = (float)(sum[k] / d.vertex_count);
  } else {
    const uint32_t mat = d.xf_regs[0x0C];
    for (int k = 0; k < 4; ++k) ras[k] = (float)((mat >> (24 - 8 * k)) & 0xFF);
  }
  float texel[4] = {255.0f, 255.0f, 255.0f, 255.0f};
  const TextureRef& t = d.textures[0];
  if (t.used && t.data && t.width && t.height && t.width <= 1024 && t.height <= 1024) {
    auto it = texel_mean_.find(t.data->hash);
    if (it == texel_mean_.end()) {
      std::vector<uint8_t> rgba;
      decode_texture(t.data->image.data(), t.width, t.height, t.format, t.data->palette.empty() ? nullptr : t.data->palette.data(),
                     t.tlut_format, rgba);
      // The colour of the texels that show (weighted by their alpha), and how much of the texture
      // shows (a sprite fills part of its square: three times its mean alpha, at most opaque). A
      // plain mean took the transparent surround in and made a fireball's light a third as bright.
      double sum[3] = {0, 0, 0}, alpha = 0;
      const size_t n = rgba.size() / 4;
      for (size_t i = 0; i < n; ++i) {
        const double a = rgba[i * 4 + 3];
        for (int k = 0; k < 3; ++k) sum[k] += rgba[i * 4 + k] * a;
        alpha += a;
      }
      std::array<float, 4> mean{255.0f, 255.0f, 255.0f, 255.0f};
      if (n && alpha > 0.0) {
        for (int k = 0; k < 3; ++k) mean[(size_t)k] = (float)(sum[k] / alpha);
        mean[3] = (float)std::min(255.0, 3.0 * alpha / (double)n);
      }
      if (texel_mean_.size() > 4096) texel_mean_.clear();
      it = texel_mean_.emplace(t.data->hash, mean).first;
    }
    for (int k = 0; k < 4; ++k) texel[k] = it->second[(size_t)k];
  }
  const int own_map = [&] { for (uint32_t n = 0; n <= d.bp.numtevstages() && n < 16; ++n) if (d.bp.order_enable((int)n)) return d.bp.order_texmap((int)n); return 0; }();
  float o[4];
  tev_texel(d, own_map, texel, ras, o);
  const uint32_t src = (d.bp.blendmode() >> 8) & 7;   // GX source factor: 1 one, 4 source alpha
  const float weight = src == 4 ? o[3] / 255.0f : src == 1 ? 1.0f : 0.0f;
  FxLight l{};
  float extent = 0.0f;
  int axis = 0;
  for (int k = 0; k < 3; ++k) {
    l.pos[k] = 0.5f * (lo[k] + hi[k]);
    if (hi[k] - lo[k] > extent) { extent = hi[k] - lo[k]; axis = k; }
  }
  for (int k = 0; k < 3; ++k) l.rgb[k] = std::clamp(o[k] / 255.0f * weight, 0.0f, 1.0f);
  // An effect's light: its hue at three times the drawn brightness, at most full (the particle's own
  // colour registers and fading alpha leave a fireball's drawn colour near a fifth of its glow).
  const float peak = std::max({l.rgb[0], l.rgb[1], l.rgb[2]});
  if (peak > 1e-4f) for (float& c : l.rgb) c *= std::min(1.0f, peak * 3.0f) / peak;
  const int n = std::max(1, segments);
  const float piece = extent / (float)n;   // each light covers its share of the long side
  l.radius = std::clamp((n > 1 ? piece : extent) * 0.2f, 0.3f, 4.0f);
  l.power = (0.2126f * l.rgb[0] + 0.7152f * l.rgb[1] + 0.0722f * l.rgb[2]) * l.radius * l.radius;
  for (int i = 0; i < n; ++i) {
    FxLight s = l;
    if (n > 1) s.pos[axis] = lo[axis] + piece * ((float)i + 0.5f);
    out.push_back(s);
  }
  return true;
}

bool fx_focus(const Frame& f, int camera, const float vw[12], float out[3]) {
  double sum[3] = {0, 0, 0};
  int n = 0;
  for (const DrawCall& d : f.draws) {
    if (!d.skinned || d.owner_player >= 6 || d.rtx.camera != camera || d.vertex_count == 0) continue;
    if (d.first_vertex + (size_t)d.vertex_count > f.vertices.size()) continue;
    const Vertex& v = f.vertices[d.first_vertex];
    const int row = (int)(v.posmtx & 0x3F);
    if (row + 2 >= 64) continue;
    const float* pm = &d.posMatrices[row * 4];
    float view[3];
    for (int k = 0; k < 3; ++k) view[k] = pm[k * 4] * v.pos[0] + pm[k * 4 + 1] * v.pos[1] + pm[k * 4 + 2] * v.pos[2] + pm[k * 4 + 3];
    for (int k = 0; k < 3; ++k) sum[k] += vw[k * 4] * view[0] + vw[k * 4 + 1] * view[1] + vw[k * 4 + 2] * view[2] + vw[k * 4 + 3];
    ++n;
  }
  if (!n) return false;
  for (int k = 0; k < 3; ++k) out[k] = (float)(sum[k] / n);
  return true;
}

void fx_keep_near(std::vector<FxLight>& lights, const float focus[3], float reach, size_t keep) {
  auto dist = [&](const FxLight& l) {
    const float dx = l.pos[0] - focus[0], dy = l.pos[1] - focus[1], dz = l.pos[2] - focus[2];
    return std::sqrt(dx * dx + dy * dy + dz * dz);
  };
  lights.erase(std::remove_if(lights.begin(), lights.end(), [&](const FxLight& l) { return dist(l) > reach; }), lights.end());
  for (FxLight& l : lights) { const float d = dist(l) / 100.0f; l.power /= 1.0f + d * d; }
  std::sort(lights.begin(), lights.end(), [](const FxLight& a, const FxLight& b) { return a.power > b.power; });
  if (lights.size() > keep) lights.resize(keep);
}

}  // namespace gx
