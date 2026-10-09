// SPDX-License-Identifier: GPL-2.0-or-later
#include "skin_thumbnail.h"
#include <algorithm>
#include <atomic>
#include <cctype>
#include <cmath>
#include <condition_variable>
#include <cstring>
#include <deque>
#include <filesystem>
#include <fstream>
#include <map>
#include <mutex>
#include <set>
#include <thread>
#include "cosmetic_mods.h"
#include "../gx/gx_texture.h"
#include "stb/stb_image_write.h"

namespace host {
// From host.h, which this file does not need the rest of.
void log(const char* fmt, ...);
bool disc_find_file(const std::string& name, uint32_t* offset, uint32_t* size);
bool disc_read(uint32_t offset, void* dst, uint32_t size);
namespace {

// ---- the file: HAL's archive layout, every read checked against the data block ----
struct Dat {
  const uint8_t* d = nullptr;   // the data block (offsets inside the file are relative to it)
  uint32_t size = 0;
  bool ok(uint32_t at, uint32_t n) const { return at <= size && n <= size - at; }
  uint32_t u32(uint32_t at) const { return ok(at, 4) ? (uint32_t)d[at] << 24 | d[at + 1] << 16 | d[at + 2] << 8 | d[at + 3] : 0; }
  uint16_t u16(uint32_t at) const { return ok(at, 2) ? (uint16_t)(d[at] << 8 | d[at + 1]) : 0; }
  uint8_t u8(uint32_t at) const { return ok(at, 1) ? d[at] : 0; }
  float f32(uint32_t at) const { uint32_t v = u32(at); float f; std::memcpy(&f, &v, 4); return std::isfinite(f) ? f : 0.0f; }
};

struct Mat { float m[3][4]; };   // a 3x4 transform
Mat identity() { Mat r{}; r.m[0][0] = r.m[1][1] = r.m[2][2] = 1.0f; return r; }
Mat mul(const Mat& a, const Mat& b) {
  Mat r{};
  for (int i = 0; i < 3; ++i) {
    for (int j = 0; j < 4; ++j) r.m[i][j] = a.m[i][0] * b.m[0][j] + a.m[i][1] * b.m[1][j] + a.m[i][2] * b.m[2][j];
    r.m[i][3] += a.m[i][3];
  }
  return r;
}
// A joint's own transform: scale, then rotation about X, Y, Z in that order, then translation.
Mat joint_local(const Dat& f, uint32_t j) {
  const float rx = f.f32(j + 0x14), ry = f.f32(j + 0x18), rz = f.f32(j + 0x1C);
  const float sx = f.f32(j + 0x20), sy = f.f32(j + 0x24), sz = f.f32(j + 0x28);
  const float cx = std::cos(rx), nx = std::sin(rx), cy = std::cos(ry), ny = std::sin(ry), cz = std::cos(rz), nz = std::sin(rz);
  Mat r{};
  r.m[0][0] = cy * cz * sx; r.m[0][1] = (nx * ny * cz - cx * nz) * sy; r.m[0][2] = (cx * ny * cz + nx * nz) * sz;
  r.m[1][0] = cy * nz * sx; r.m[1][1] = (nx * ny * nz + cx * cz) * sy; r.m[1][2] = (cx * ny * nz - nx * cz) * sz;
  r.m[2][0] = -ny * sx;     r.m[2][1] = nx * cy * sy;                  r.m[2][2] = cx * cy * sz;
  r.m[0][3] = f.f32(j + 0x2C); r.m[1][3] = f.f32(j + 0x30); r.m[2][3] = f.f32(j + 0x34);
  return r;
}

struct Texture { int w = 0, h = 0; float repeat_u = 1, repeat_v = 1; uint32_t wrap_u = 0, wrap_v = 0; std::vector<uint8_t> rgba; };
struct Vertex { float x, y, z, u, v; };
struct Triangle { Vertex v[3]; int texture; uint8_t color[4]; };

constexpr size_t kMaxJoints = 2048, kMaxTriangles = 120000, kMaxTextures = 256;
constexpr uint32_t kJointHidden = 0x10, kJointSplineOrParticle = 0x4020;

struct Model {
  Dat f;
  std::map<uint32_t, Mat> world;                 // joint offset -> its transform in the rest pose
  std::vector<std::pair<uint32_t, bool>> joints; // in tree order, with "hidden by itself or a parent"
  std::map<uint32_t, int> texture_of;            // image descriptor -> index in textures, -1 unreadable
  std::vector<Texture> textures;
  std::vector<Triangle> triangles;
};

void walk_joints(Model& m, uint32_t first, const Mat& parent, bool hidden, int depth) {
  for (uint32_t j = first; j && depth < 64 && m.joints.size() < kMaxJoints; j = m.f.u32(j + 0x0C)) {
    if (!m.f.ok(j, 0x40) || m.world.count(j)) return;   // outside the file, or a loop
    const Mat here = mul(parent, joint_local(m.f, j));
    m.world[j] = here;
    const bool h = hidden || (m.f.u32(j + 4) & kJointHidden);
    m.joints.push_back({j, h});
    walk_joints(m, m.f.u32(j + 8), here, h, depth + 1);
  }
}

int texture_for(Model& m, uint32_t tobj) {
  const uint32_t image = m.f.u32(tobj + 0x4C);
  if (!image || !m.f.ok(image, 0x18)) return -1;
  const auto known = m.texture_of.find(image);
  if (known != m.texture_of.end()) return known->second;
  int index = -1;
  const uint32_t data = m.f.u32(image), format = m.f.u32(image + 8);
  const uint32_t w = m.f.u16(image + 4), h = m.f.u16(image + 6);
  const bool format_ok = format <= 6 || format == 8 || format == 9 || format == 10 || format == 14;
  if (format_ok && w && h && w <= 1024 && h <= 1024 && m.textures.size() < kMaxTextures) {
    const uint32_t bytes = gx::texture_level_bytes(w, h, format);
    const uint32_t tlut = m.f.u32(tobj + 0x50);
    const bool paletted = format == 8 || format == 9 || format == 10;
    uint32_t palette_data = 0, palette_format = 0;
    bool palette_ok = !paletted;
    if (paletted && tlut && m.f.ok(tlut, 0x10)) {
      palette_data = m.f.u32(tlut); palette_format = m.f.u32(tlut + 4);
      // Room for every index the format can name: 16, 256 or 16384 two-byte colours.
      const uint32_t entries = format == 8 ? 16u : format == 9 ? 256u : 16384u;
      palette_ok = palette_format <= 2 && m.f.ok(palette_data, entries * 2);
    }
    if (bytes && m.f.ok(data, bytes) && palette_ok) {
      Texture t; t.w = (int)w; t.h = (int)h;
      t.repeat_u = std::max<float>(1.0f, m.f.u8(tobj + 0x3C)); t.repeat_v = std::max<float>(1.0f, m.f.u8(tobj + 0x3D));
      t.wrap_u = m.f.u32(tobj + 0x34); t.wrap_v = m.f.u32(tobj + 0x38);
      gx::decode_texture(m.f.d + data, w, h, format, paletted ? m.f.d + palette_data : nullptr, palette_format, t.rgba);
      if (t.rgba.size() == (size_t)w * h * 4) { index = (int)m.textures.size(); m.textures.push_back(std::move(t)); }
    }
  }
  m.texture_of[image] = index;
  return index;
}

struct Attribute { uint32_t attr, type, count, comp; uint8_t frac; uint16_t stride; uint32_t data; };

// One component of a position or texture coordinate; advances `at`.
float component(const Dat& f, uint32_t& at, uint32_t comp, uint8_t frac) {
  const float scale = 1.0f / (float)(1u << (frac & 31));
  switch (comp) {
    case 4: { const float v = f.f32(at); at += 4; return v; }
    case 3: { const float v = (float)(int16_t)f.u16(at) * scale; at += 2; return v; }
    case 2: { const float v = (float)f.u16(at) * scale; at += 2; return v; }
    case 1: { const float v = (float)(int8_t)f.u8(at) * scale; at += 1; return v; }
    default: { const float v = (float)f.u8(at) * scale; at += 1; return v; }
  }
}
uint32_t component_bytes(uint32_t comp) { return comp == 4 ? 4u : comp >= 2 ? 2u : 1u; }

void read_polygons(Model& m, uint32_t pobj, uint32_t owner, int texture, const uint8_t color[4]) {
  const Dat& f = m.f;
  std::vector<Attribute> attrs;
  for (uint32_t at = f.u32(pobj + 8); at && f.ok(at, 0x18) && attrs.size() < 32; at += 0x18) {
    const uint32_t attr = f.u32(at);
    if (attr == 0xFF) break;
    attrs.push_back({attr, f.u32(at + 4), f.u32(at + 8), f.u32(at + 0x0C), f.u8(at + 0x10), f.u16(at + 0x12), f.u32(at + 0x14)});
  }
  if (attrs.empty()) return;
  const uint16_t flags = f.u16(pobj + 0x0C);
  const uint32_t blocks = f.u16(pobj + 0x0E), list = f.u32(pobj + 0x10), bind = f.u32(pobj + 0x14);
  if (!f.ok(list, blocks * 32u)) return;
  // Envelopes: per matrix index, the joints a vertex follows. A vertex bound whole to one joint is
  // stored in that joint's space; one shared between joints is stored in the model's own.
  std::vector<uint32_t> rigid;   // the single joint of each envelope, 0 when it has several
  if ((flags & 0x3000) == 0x2000 && bind) {
    for (uint32_t e = bind; f.ok(e, 4) && f.u32(e) && rigid.size() < 256; e += 4) {
      const uint32_t entry = f.u32(e);
      const bool single = f.ok(entry, 16) && f.u32(entry) && f.f32(entry + 4) >= 1.0f && f.u32(entry + 8) == 0;
      rigid.push_back(single ? f.u32(entry) : 0);
    }
  }
  const auto owner_world = m.world.find(owner);
  Mat base = owner_world != m.world.end() ? owner_world->second : identity();
  if ((flags & 0x3000) == 0 && bind) { const auto bound = m.world.find(bind); if (bound != m.world.end()) base = bound->second; }

  const uint32_t end = list + blocks * 32u;
  std::vector<Vertex> verts;
  for (uint32_t at = list; at + 3 <= end;) {
    const uint8_t op = f.u8(at);
    if (op == 0) break;
    const uint8_t prim = op & 0xF8;
    const uint32_t count = f.u16(at + 1);
    at += 3;
    if (prim != 0x80 && prim != 0x90 && prim != 0x98 && prim != 0xA0 && prim != 0xA8 && prim != 0xB0 && prim != 0xB8) return;
    verts.clear();
    for (uint32_t n = 0; n < count; ++n) {
      float pos[3] = {0, 0, 0}, uv[2] = {0, 0};
      uint32_t matrix = 0;
      for (const Attribute& a : attrs) {
        if (at >= end) return;
        if (a.attr <= 8) { if (a.attr == 0) matrix = f.u8(at) / 3u; at += 1; continue; }   // matrix indices: one byte
        if (a.type == 0) continue;
        uint32_t from = at;
        if (a.type == 1) {                                  // the value itself is in the list
          if (a.attr == 11 || a.attr == 12) { static const uint8_t bytes[6] = {2, 3, 4, 2, 3, 4}; at += bytes[a.comp < 6 ? a.comp : 5]; continue; }
          if (a.attr == 10 || a.attr == 25) { at += (a.count == 0 ? 3u : 9u) * component_bytes(a.comp); continue; }
        } else {                                            // an index into the attribute's array
          const uint32_t index = a.type == 2 ? f.u8(at) : f.u16(at);
          at += a.type == 2 ? 1 : 2;
          from = a.data + index * a.stride;
          if (a.attr != 9 && a.attr != 13) continue;
        }
        if (a.attr == 9) { const int k = a.count ? 3 : 2; for (int i = 0; i < k; ++i) pos[i] = component(f, from, a.comp, a.frac); }
        else if (a.attr >= 13 && a.attr <= 20) {
          const int k = a.count ? 2 : 1; float got[2] = {0, 0};
          for (int i = 0; i < k; ++i) got[i] = component(f, from, a.comp, a.frac);
          if (a.attr == 13) { uv[0] = got[0]; uv[1] = got[1]; }
        } else if (a.type == 1) return;                     // a direct attribute this reader does not size
        if (a.type == 1) at = from;
      }
      const Mat* by = &base;
      if (matrix < rigid.size() && rigid[matrix]) { const auto w = m.world.find(rigid[matrix]); if (w != m.world.end()) by = &w->second; }
      Vertex v{};
      v.x = by->m[0][0] * pos[0] + by->m[0][1] * pos[1] + by->m[0][2] * pos[2] + by->m[0][3];
      v.y = by->m[1][0] * pos[0] + by->m[1][1] * pos[1] + by->m[1][2] * pos[2] + by->m[1][3];
      v.z = by->m[2][0] * pos[0] + by->m[2][1] * pos[1] + by->m[2][2] * pos[2] + by->m[2][3];
      v.u = uv[0]; v.v = uv[1];
      if (!std::isfinite(v.x) || !std::isfinite(v.y) || !std::isfinite(v.z)) return;
      verts.push_back(v);
    }
    const auto add = [&](size_t a, size_t b, size_t c) {
      if (m.triangles.size() >= kMaxTriangles) return;
      Triangle t{{verts[a], verts[b], verts[c]}, texture, {color[0], color[1], color[2], color[3]}};
      m.triangles.push_back(t);
    };
    const size_t k = verts.size();
    if (prim == 0x90) for (size_t i = 0; i + 2 < k; i += 3) add(i, i + 1, i + 2);
    else if (prim == 0x98) for (size_t i = 0; i + 2 < k; ++i) add(i, i + 1, i + 2);
    else if (prim == 0xA0) for (size_t i = 1; i + 1 < k; ++i) add(0, i, i + 1);
    else if (prim == 0x80) for (size_t i = 0; i + 3 < k; i += 4) { add(i, i + 1, i + 2); add(i, i + 2, i + 3); }
  }
}

bool load_model(Model& m, const uint8_t* bytes, size_t size) {
  if (size < 0x40 || size > 64u * 1024 * 1024) return false;
  const auto be = [&](size_t at) { return (uint32_t)bytes[at] << 24 | bytes[at + 1] << 16 | bytes[at + 2] << 8 | bytes[at + 3]; };
  const uint32_t data_size = be(4), relocations = be(8), roots = be(12), references = be(16);
  if (data_size > size - 0x20 || relocations > size / 4 || roots == 0 || roots > 4096 || references > 4096) return false;
  const uint64_t root_table = 0x20ull + data_size + (uint64_t)relocations * 4;
  const uint64_t strings = root_table + (uint64_t)(roots + references) * 8;
  if (strings > size) return false;
  m.f.d = bytes + 0x20; m.f.size = data_size;
  // The fighter's joint tree: the root whose name ends in "_joint" (PlyMarth5K_Share_joint).
  uint32_t tree = 0; bool found = false, kirby = false;
  for (uint32_t i = 0; i < roots && !found; ++i) {
    const uint32_t offset = be((size_t)root_table + i * 8), name = be((size_t)root_table + i * 8 + 4);
    const uint64_t at = strings + name;
    if (at >= size) continue;
    const size_t length = strnlen((const char*)bytes + at, (size_t)(size - at));
    if (length >= 6 && std::memcmp(bytes + at + length - 6, "_joint", 6) == 0 &&
        !(length >= 14 && std::memcmp(bytes + at + length - 14, "_matanim_joint", 14) == 0)) {
      tree = offset; found = true;
      kirby = length >= 8 && std::memcmp(bytes + at, "PlyKirby", 8) == 0;
    }
  }
  if (!found || !m.f.ok(tree, 0x40)) return false;
  walk_joints(m, tree, identity(), false, 0);
  bool drew_a_joint = false;
  for (const auto& joint : m.joints) {
    // Kirby's file carries every copy ability's hat on a joint of its own, which the game shows one
    // at a time: only his body, the first joint with parts, is drawn.
    if (kirby && drew_a_joint) break;
    if (joint.second || (m.f.u32(joint.first + 4) & kJointSplineOrParticle)) continue;
    int guard = 0;
    for (uint32_t dobj = m.f.u32(joint.first + 0x10); dobj && m.f.ok(dobj, 0x10) && guard < 512; dobj = m.f.u32(dobj + 4), ++guard) {
      uint8_t color[4] = {255, 255, 255, 255};
      int texture = -1;
      const uint32_t mobj = m.f.u32(dobj + 8);
      if (mobj && m.f.ok(mobj, 0x18)) {
        const uint32_t material = m.f.u32(mobj + 0x0C), tobj = m.f.u32(mobj + 8);
        if (material && m.f.ok(material, 0x14)) for (int i = 0; i < 4; ++i) color[i] = m.f.u8(material + 4 + i);
        if (tobj && m.f.ok(tobj, 0x5C)) texture = texture_for(m, tobj);
      }
      drew_a_joint = true;
      int polygons = 0;
      for (uint32_t pobj = m.f.u32(dobj + 0x0C); pobj && m.f.ok(pobj, 0x18) && polygons < 512; pobj = m.f.u32(pobj + 4), ++polygons)
        read_polygons(m, pobj, joint.first, texture, color);
    }
  }
  return m.triangles.size() >= 16;
}

// A texture coordinate brought into 0..1 the way the texture asks: 0 clamp, 1 repeat, 2 mirror.
float wrapped(float t, uint32_t mode) {
  if (mode == 1) return t - std::floor(t);
  if (mode == 2) { const float period = t - 2.0f * std::floor(t * 0.5f); return period > 1.0f ? 2.0f - period : period; }
  return std::min(1.0f, std::max(0.0f, t));
}

// ---- drawing: a depth-buffered fill at twice the size, averaged down ----
void rasterize(const Model& m, int width, int height, std::vector<uint8_t>* out) {
  const int W = width * 2, H = height * 2;
  // Framed on the model's height, centred: a wide rest pose loses its fingertips, not its size.
  std::vector<float> ys, xs;
  ys.reserve(m.triangles.size() * 3); xs.reserve(m.triangles.size() * 3);
  for (const Triangle& t : m.triangles) for (const Vertex& v : t.v) { ys.push_back(v.y); xs.push_back(v.x); }
  const auto at = [](std::vector<float>& values, double part) {
    const size_t i = (size_t)(part * (double)(values.size() - 1));
    std::nth_element(values.begin(), values.begin() + (ptrdiff_t)i, values.end());
    return values[i];
  };
  const float y0 = at(ys, 0.003), y1 = at(ys, 0.997), x0 = at(xs, 0.02), x1 = at(xs, 0.98);
  const float tall = std::max(y1 - y0, 1e-3f) * 1.06f, wide = std::max(x1 - x0, 1e-3f) * 1.06f;
  // A slim, upright fighter (most of them) is shown from the top of the head down to about the
  // knees, so the face and the costume's colours read at tile size; a round or broad one is shown
  // whole. Arms held out in the rest pose are wider than the picture and run off its sides.
  const float core = std::max(at(xs, 0.80) - at(xs, 0.20), 1e-3f);
  const bool slim = core < (y1 - y0) * 0.35f;
  const float shown = slim ? tall * 0.66f : tall;
  const float scale = std::min((float)H / shown, (float)W / (slim ? core * 2.4f : wide * 0.62f));
  const float cx = (at(xs, 0.20) + at(xs, 0.80)) * 0.5f;
  const float cy = slim ? y1 + (tall - (y1 - y0)) * 0.5f - shown * 0.5f : (y0 + y1) * 0.5f;
  std::vector<float> depth((size_t)W * H, -1e30f);
  std::vector<uint8_t> big((size_t)W * H * 4, 0);
  for (const Triangle& t : m.triangles) {
    float x[3], y[3], z[3];
    for (int i = 0; i < 3; ++i) { x[i] = (t.v[i].x - cx) * scale + (float)W * 0.5f; y[i] = (float)H * 0.5f - (t.v[i].y - cy) * scale; z[i] = t.v[i].z; }
    const float den = (y[1] - y[2]) * (x[0] - x[2]) + (x[2] - x[1]) * (y[0] - y[2]);
    if (std::fabs(den) < 1e-6f) continue;
    const int left = std::max(0, (int)std::floor(std::min({x[0], x[1], x[2]}))), right = std::min(W - 1, (int)std::ceil(std::max({x[0], x[1], x[2]})));
    const int top = std::max(0, (int)std::floor(std::min({y[0], y[1], y[2]}))), bottom = std::min(H - 1, (int)std::ceil(std::max({y[0], y[1], y[2]})));
    if (left > right || top > bottom) continue;
    // One flat shade per face from a light over the viewer's shoulder, so shapes read without the game's lighting.
    const float ax = t.v[1].x - t.v[0].x, ay = t.v[1].y - t.v[0].y, az = t.v[1].z - t.v[0].z;
    const float bx = t.v[2].x - t.v[0].x, by = t.v[2].y - t.v[0].y, bz = t.v[2].z - t.v[0].z;
    const float nx = ay * bz - az * by, ny = az * bx - ax * bz, nz = ax * by - ay * bx;
    const float length = std::sqrt(nx * nx + ny * ny + nz * nz);
    const float shade = length > 0 ? 0.55f + 0.45f * std::fabs((nx * 0.31f + ny * 0.41f + nz * 0.86f) / length) : 1.0f;
    const Texture* texture = t.texture >= 0 && t.texture < (int)m.textures.size() ? &m.textures[(size_t)t.texture] : nullptr;
    for (int py = top; py <= bottom; ++py) {
      for (int px = left; px <= right; ++px) {
        const float fx = (float)px + 0.5f, fy = (float)py + 0.5f;
        const float w0 = ((y[1] - y[2]) * (fx - x[2]) + (x[2] - x[1]) * (fy - y[2])) / den;
        const float w1 = ((y[2] - y[0]) * (fx - x[2]) + (x[0] - x[2]) * (fy - y[2])) / den;
        const float w2 = 1.0f - w0 - w1;
        if (w0 < 0 || w1 < 0 || w2 < 0) continue;
        const float pz = w0 * z[0] + w1 * z[1] + w2 * z[2];
        const size_t index = (size_t)py * W + px;
        if (pz <= depth[index]) continue;
        uint8_t rgb[3] = {t.color[0], t.color[1], t.color[2]};
        if (texture) {
          float u = (w0 * t.v[0].u + w1 * t.v[1].u + w2 * t.v[2].u) * texture->repeat_u;
          float v = (w0 * t.v[0].v + w1 * t.v[1].v + w2 * t.v[2].v) * texture->repeat_v;
          u = wrapped(u, texture->wrap_u); v = wrapped(v, texture->wrap_v);
          const int tx = std::min(texture->w - 1, std::max(0, (int)(u * (float)texture->w)));
          const int ty = std::min(texture->h - 1, std::max(0, (int)(v * (float)texture->h)));
          const uint8_t* texel = &texture->rgba[((size_t)ty * texture->w + tx) * 4];
          if (texel[3] < 128) continue;   // cut-out parts of a texture (hair edges, eyelashes)
          rgb[0] = texel[0]; rgb[1] = texel[1]; rgb[2] = texel[2];
        }
        depth[index] = pz;
        big[index * 4 + 0] = (uint8_t)((float)rgb[0] * shade); big[index * 4 + 1] = (uint8_t)((float)rgb[1] * shade);
        big[index * 4 + 2] = (uint8_t)((float)rgb[2] * shade); big[index * 4 + 3] = 255;
      }
    }
  }
  out->assign((size_t)width * height * 4, 0);
  for (int py = 0; py < height; ++py) {
    for (int px = 0; px < width; ++px) {
      unsigned sum[4] = {0, 0, 0, 0};
      for (int k = 0; k < 4; ++k) {
        const uint8_t* p = &big[((size_t)(py * 2 + (k >> 1)) * W + px * 2 + (k & 1)) * 4];
        sum[0] += p[0] * p[3]; sum[1] += p[1] * p[3]; sum[2] += p[2] * p[3]; sum[3] += p[3];
      }
      uint8_t* o = &(*out)[((size_t)py * width + px) * 4];
      if (sum[3]) { o[0] = (uint8_t)(sum[0] / sum[3]); o[1] = (uint8_t)(sum[1] / sum[3]); o[2] = (uint8_t)(sum[2] / sum[3]); }
      o[3] = (uint8_t)(sum[3] / 4);
    }
  }
}

}  // namespace

bool render_costume_picture(const uint8_t* dat, size_t size, int width, int height, std::vector<uint8_t>* rgba) {
  if (!dat || !rgba || width < 8 || height < 8 || width > 1024 || height > 1024) return false;
  Model model;
  if (!load_model(model, dat, size)) return false;
  rasterize(model, width, height, rgba);
  return true;
}

// ---- the background worker ----
namespace {
// Never destroyed: the worker is detached and may still be waiting when the program ends.
struct Shared {
  std::mutex mutex;
  std::condition_variable wake;
  struct Job { std::string id; uint32_t disc_offset = 0, disc_size = 0; };   // disc_size: a standard costume, read from the disc
  std::deque<Job> queue;
  std::set<std::string> asked;      // queued, being drawn, or failed: not asked for again
  std::set<std::string> ready;      // drawn this session (the file is there)
  bool started = false, stop = false;
};
Shared& shared() { static Shared* s = new Shared; return *s; }

void write_png(void* context, void* data, int size) { ((std::ofstream*)context)->write((const char*)data, size); }

void work() {
  for (;;) {
    Shared::Job job;
    {
      Shared& s = shared();
      std::unique_lock<std::mutex> lock(s.mutex);
      s.wake.wait(lock, [&] { return s.stop || !s.queue.empty(); });
      if (s.stop) return;
      job = s.queue.front(); s.queue.pop_front();
    }
    std::vector<uint8_t> bytes, pixels;
    std::string path;
    const std::string& id = job.id;
    if (job.disc_size) {
      path = cosmetics::costume_thumbnail_path(id);
      bytes.resize(job.disc_size);
      if (path.empty() || !disc_read(job.disc_offset, bytes.data(), job.disc_size)) continue;
    } else if (!cosmetics::costume_file(id, &bytes, &path) || path.empty()) continue;
    constexpr int kWidth = 192, kHeight = 256;
    if (!render_costume_picture(bytes.data(), bytes.size(), kWidth, kHeight, &pixels)) { log("cosmetics: no picture could be drawn for %s", id.c_str()); continue; }
    namespace fs = std::filesystem;
    const fs::path file = fs::u8path(path), temporary = fs::u8path(path + ".tmp");
    std::error_code ec;
    fs::create_directories(file.parent_path(), ec);
    {
      std::ofstream out(temporary, std::ios::binary | std::ios::trunc);
      if (!out || !stbi_write_png_to_func(write_png, &out, kWidth, kHeight, 4, pixels.data(), kWidth * 4) || !out) { fs::remove(temporary, ec); continue; }
    }
    fs::rename(temporary, file, ec);
    if (ec) { fs::remove(temporary, ec); continue; }
    std::lock_guard<std::mutex> lock(shared().mutex);
    shared().ready.insert(id);
  }
}
}  // namespace

std::string skin_thumbnail(const std::string& asset_id) {
  if (asset_id.empty()) return {};
  const std::string path = cosmetics::costume_thumbnail_path(asset_id);
  if (path.empty()) return {};
  Shared& s = shared();
  std::lock_guard<std::mutex> lock(s.mutex);
  if (s.ready.count(asset_id)) return path;
  if (s.asked.count(asset_id)) return {};
  std::error_code ec;
  if (std::filesystem::is_regular_file(std::filesystem::u8path(path), ec)) { s.ready.insert(asset_id); return path; }
  s.asked.insert(asset_id);
  s.queue.push_back({asset_id, 0, 0});
  if (!s.started) { s.started = true; std::thread(work).detach(); }
  s.wake.notify_one();
  return {};
}

std::string standard_costume_thumbnail(const std::string& costume_file_name) {
  // The file is looked up here, on the caller's thread; only the read happens on the worker.
  uint32_t offset = 0, size = 0;
  if (costume_file_name.empty() || !disc_find_file(costume_file_name, &offset, &size) || size < 0x40 || size > 16u * 1024 * 1024) return {};
  // Named by the file and its size, so another disc's costume of the same name gets its own picture.
  std::string id = "standard-" + costume_file_name + "-" + std::to_string(size);
  for (char& c : id) c = (char)std::tolower((unsigned char)c);
  const std::string path = cosmetics::costume_thumbnail_path(id);
  if (path.empty()) return {};
  Shared& s = shared();
  std::lock_guard<std::mutex> lock(s.mutex);
  if (s.ready.count(id)) return path;
  if (s.asked.count(id)) return {};
  std::error_code ec;
  if (std::filesystem::is_regular_file(std::filesystem::u8path(path), ec)) { s.ready.insert(id); return path; }
  s.asked.insert(id);
  s.queue.push_back({id, offset, size});
  if (!s.started) { s.started = true; std::thread(work).detach(); }
  s.wake.notify_one();
  return {};
}

void skin_thumbnails_shutdown() {
  { std::lock_guard<std::mutex> lock(shared().mutex); shared().stop = true; }
  shared().wake.notify_all();
}

}  // namespace host
