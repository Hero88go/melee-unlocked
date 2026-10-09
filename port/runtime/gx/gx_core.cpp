#include "render_observer.h"
// GX command processor: FIFO -> register state -> captured frames.
// SPDX-License-Identifier: GPL-2.0-or-later
#include "gx_core.h"
#include "pc_settings_shared.h"
#include "host.h"
#include "audio.h"
#include "native_pose_bridge.h"
#include "replay_control.h"
#ifdef MELEE_NO_SLIPPI
#include "netplay_state.h"   // the same names, answered from the neutral netplay state
#else
#include "slippi_online.h"
#endif
#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <vector>
#include <unordered_map>

namespace gx {

namespace {

BPMemory g_bp;
CPMemory g_cp;
XFMemory g_xf;
uint8_t g_tmem[1024 * 1024];
uint64_t g_tmem_generation = 1;
// One per write_fifo_bytes call (a native pipe flush). The native game is stopped while a batch is
// parsed, so memory a texture reads cannot change between two draws of one batch.
uint64_t g_fifo_batch = 1;
uint32_t g_bp_mask = 0xFFFFFF;
int32_t g_tev_colors[4][4], g_tev_kcolors[4][4];
Backend* g_backend = nullptr;
Frame g_frame;
TextureSnapshotCache g_texture_snapshots;
uint64_t g_frame_sequence = 0;
bool g_discontinuity = false;   // simulation thread only, like the rest of this file's state
std::atomic<int> g_stock_hud_scale{100}, g_damage_hud_scale{100};
std::atomic<bool> g_pal_stock_hud{false};
std::atomic<bool> g_low_poly_fighters{false};
std::array<HudPlayerSnapshot, 4> g_native_hud{};   // the native game's players (set_native_hud_player)

bool guest_object(uint32_t address, uint32_t bytes) {
  return address >= 0x80000000u && address <= 0x81800000u - bytes;
}
float guest_float(uint32_t address) {
  const uint32_t bits = host::rd32(address);
  float value;
  std::memcpy(&value, &bits, sizeof value);
  return value;
}
void write_guest_float(uint32_t address, float value) {
  uint32_t bits;
  std::memcpy(&bits, &value, sizeof bits);
  host::wr32(address, bits);
}
struct HudRootScale { uint32_t root = 0; float base_x = 1, base_y = 1; };
std::array<HudRootScale, 4> g_stock_roots{}, g_damage_roots{};

void scale_hud_root(uint32_t gobj, int percent, HudRootScale& cached) {
  if (!guest_object(gobj, 0x2C)) { cached = {}; return; }
  const uint32_t root = host::rd32(gobj + 0x28);
  if (!guest_object(root, 0x44)) { cached = {}; return; }
  if (cached.root != root) {
    cached = {root, guest_float(root + 0x2C), guest_float(root + 0x30)};
    if (!std::isfinite(cached.base_x) || !std::isfinite(cached.base_y) ||
        cached.base_x <= 0.0f || cached.base_y <= 0.0f) { cached = {}; return; }
  }
  const float factor = std::clamp(percent, 75, 175) / 100.0f;
  const float x = cached.base_x * factor, y = cached.base_y * factor;
  if (guest_float(root + 0x2C) == x && guest_float(root + 0x30) == y) return;
  write_guest_float(root + 0x2C, x);
  write_guest_float(root + 0x30, y);
  host::wr32(root + 0x14, host::rd32(root + 0x14) | (1u << 6)); // JOBJ_MTX_DIRTY
}

void capture_match_hud(Frame& frame) {
  if (!frame_in_match(frame)) { g_stock_roots = {}; g_damage_roots = {}; return; }
  frame.player_names = slippi::online::player_names_for_overlay();
  for (int slot = 0; slot < 4; ++slot) {
    auto& out = frame.hud_players[slot];
    const uint32_t status = 0x804A10C8u + slot * 0x64u; // HudIndex.players[slot]
    const uint32_t stock = 0x804A1378u + 8u + slot * 0x50u;
    const uint32_t damage_gobj = host::rd32(status);
    const uint32_t stock_gobj = host::rd32(stock);
    const int damage = (int16_t)host::rd16(status + 0x0A);
    const int stocks = (int32_t)host::rd32(stock + 0x4C);
    out.present = guest_object(damage_gobj, 0x2C) && guest_object(stock_gobj, 0x2C) &&
                  damage >= 0 && damage <= 999 && stocks >= 0 && stocks <= 99;
    if (out.present) { out.damage = damage; out.stocks = stocks; }
    const uint32_t tag_gobj = host::rd32(0x804A1EE0u + slot * 4u);
    if (guest_object(tag_gobj, 0x2C)) {
      const uint32_t tag_root = host::rd32(tag_gobj + 0x28);
      if (guest_object(tag_root, 0x44)) {
        out.tag_x = guest_float(tag_root + 0x38);
        out.tag_y = -guest_float(tag_root + 0x3C);
        const uint32_t child = host::rd32(tag_root + 0x10);
        out.tag_visible = guest_object(child, 0x18) && !(host::rd32(child + 0x14) & 0x10) &&
                          std::isfinite(out.tag_x) && std::isfinite(out.tag_y) &&
                          out.tag_x >= 0 && out.tag_x <= 640 && out.tag_y >= 0 && out.tag_y <= 480;
      }
    }
    if (out.present) {
      if (!g_pal_stock_hud.load(std::memory_order_relaxed))
        scale_hud_root(stock_gobj, g_stock_hud_scale.load(std::memory_order_relaxed), g_stock_roots[slot]);
      scale_hud_root(damage_gobj, g_damage_hud_scale.load(std::memory_order_relaxed), g_damage_roots[slot]);
    }
  }
}
std::vector<uint8_t> g_buf;
size_t g_buf_pos = 0;   // parsed prefix of g_buf
// Bytes the pending (incomplete) command at g_buf_pos needs before it can parse. The game writes the
// FIFO a few bytes at a time, so retrying a large vertex primitive on every write was quadratic.
size_t g_parse_need = 0;
inline size_t incomplete(size_t need) { g_parse_need = need; return 0; }
// Draw identity bookkeeping (reset per frame).
uint32_t g_dl_addr = 0, g_dl_draw_ordinal = 0, g_dl_call_ordinal = 0;
bool g_merge_next_primitive = false;
std::unordered_map<uint32_t, uint32_t> g_dl_calls;        // display list address -> calls this frame
std::unordered_map<uint64_t, uint32_t> g_immediate_draws;  // (tex0, count, prim) -> ordinal this frame
uint64_t g_commands = 0, g_draws = 0, g_vertices = 0;
uint32_t g_efb_copies = 0;
NativeDrawAudit g_native_draw_audit;
std::atomic<bool> g_native_pose_capture_enabled{false};
std::unordered_map<uint32_t, std::shared_ptr<const AuthoredPose>> g_native_pose_scopes;
std::unordered_map<uint32_t, bool> g_native_pose_skinned_scopes;
std::vector<uint32_t> g_native_pose_scope_ids;
// The native game names the joint and the PObj each draw scope belongs to (token 0xF0). A draw in a
// named scope is identified by its PObj, that PObj's ordinal among its scopes this frame (a model
// drawn twice, as a reflection) and the draw's ordinal in the scope; the joint is its object, as
// the translated build's observer names a draw by the object it saw allocated. Display list and
// call order alone shift when a menu drops or adds draws, and the incoming menu panels then paired
// with the outgoing ones and flew in between two frames.
struct NativeDrawScope { uint32_t object, piece, ordinal, draws; };
std::vector<NativeDrawScope> g_native_draw_scopes;
std::unordered_map<uint32_t, uint32_t> g_native_object_scopes;   // PObj -> scopes begun this frame
NativePoseBridgeStats g_native_pose_stats{};
// Native game: draw owners arrive in the stream (token 0xF1), not from the render observer.
bool g_native_owner_source = false;
uint8_t g_native_owner = 0xFF;
bool g_native_skinned = false;   // token 0xF2: the current native PObj is an envelope draw

uint32_t active_native_pose_scope_id() {
  return g_native_pose_scope_ids.empty() ? 0 : g_native_pose_scope_ids.back();
}

inline uint32_t be32(const uint8_t* p) { return ((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16) | ((uint32_t)p[2] << 8) | p[3]; }
inline uint32_t be16(const uint8_t* p) { return ((uint32_t)p[0] << 8) | p[1]; }

// ---------------- vertex attribute description ----------------
struct AttrDesc {
  uint32_t type;      // 0 none, 1 direct, 2 index8, 3 index16
  uint32_t format;    // component format
  uint32_t count;     // elements flag
  uint32_t frac;
  uint32_t array;     // CP array index for indexed
};

struct VertexDesc {
  bool posmtx;
  bool texmtx[8];
  AttrDesc pos, nrm, col[2], tex[8];
  uint32_t nrm_index3;
  uint32_t size;      // bytes per vertex in the stream
};

uint32_t comp_bytes(uint32_t format) { static const uint32_t b[] = {1, 1, 2, 2, 4, 4, 4, 4}; return b[format & 7]; }

VertexDesc build_desc(uint32_t fmt) {
  VertexDesc d{};
  uint32_t lo = g_cp.vcd_lo(), hi = g_cp.vcd_hi();
  uint32_t a = g_cp.vat_a(fmt), b = g_cp.vat_b(fmt), c = g_cp.vat_c(fmt);
  d.posmtx = lo & 1;
  for (int i = 0; i < 8; ++i) d.texmtx[i] = (lo >> (1 + i)) & 1;
  d.pos = {bits(lo, 9, 2), bits(a, 1, 3), bits(a, 0, 1), bits(a, 4, 5), 0};
  d.nrm = {bits(lo, 11, 2), bits(a, 10, 3), bits(a, 9, 1), 0, 1};
  d.nrm_index3 = bits(a, 31, 1);
  d.col[0] = {bits(lo, 13, 2), bits(a, 14, 3), bits(a, 13, 1), 0, 2};
  d.col[1] = {bits(lo, 15, 2), bits(a, 18, 3), bits(a, 17, 1), 0, 3};
  d.tex[0] = {bits(hi, 0, 2), bits(a, 22, 3), bits(a, 21, 1), bits(a, 25, 5), 4};
  d.tex[1] = {bits(hi, 2, 2), bits(b, 1, 3), bits(b, 0, 1), bits(b, 4, 5), 5};
  d.tex[2] = {bits(hi, 4, 2), bits(b, 10, 3), bits(b, 9, 1), bits(b, 13, 5), 6};
  d.tex[3] = {bits(hi, 6, 2), bits(b, 19, 3), bits(b, 18, 1), bits(b, 22, 5), 7};
  d.tex[4] = {bits(hi, 8, 2), bits(b, 28, 3), bits(b, 27, 1), bits(c, 0, 5), 8};
  d.tex[5] = {bits(hi, 10, 2), bits(c, 6, 3), bits(c, 5, 1), bits(c, 9, 5), 9};
  d.tex[6] = {bits(hi, 12, 2), bits(c, 15, 3), bits(c, 14, 1), bits(c, 18, 5), 10};
  d.tex[7] = {bits(hi, 14, 2), bits(c, 24, 3), bits(c, 23, 1), bits(c, 27, 5), 11};
  uint32_t size = 0;
  if (d.posmtx) size += 1;
  for (int i = 0; i < 8; ++i) if (d.texmtx[i]) size += 1;
  auto attr = [&](const AttrDesc& x, uint32_t direct) {
    if (x.type == 1) size += direct; else if (x.type == 2) size += 1; else if (x.type == 3) size += 2;
  };
  attr(d.pos, comp_bytes(d.pos.format) * (d.pos.count ? 3 : 2));
  if (d.nrm.type == 1) size += comp_bytes(d.nrm.format) * (d.nrm.count ? 9 : 3);
  else if (d.nrm.type == 2) size += (d.nrm.count && d.nrm_index3) ? 3 : 1;
  else if (d.nrm.type == 3) size += (d.nrm.count && d.nrm_index3) ? 6 : 2;
  static const uint32_t csize[] = {2, 3, 4, 2, 3, 4, 4, 4};
  for (int i = 0; i < 2; ++i) attr(d.col[i], csize[d.col[i].format]);
  for (int i = 0; i < 8; ++i) attr(d.tex[i], comp_bytes(d.tex[i].format) * (d.tex[i].count ? 2 : 1));
  d.size = size;
  return d;
}

// 2^-frac for the 5-bit vertex fraction field. The values are exact powers of two, identical to
// std::ldexp(1.0f, -frac), which cost a C library call for every component of every vertex.
constexpr float kFracScale[32] = {
    1.0f, 0x1p-1f, 0x1p-2f, 0x1p-3f, 0x1p-4f, 0x1p-5f, 0x1p-6f, 0x1p-7f, 0x1p-8f, 0x1p-9f, 0x1p-10f, 0x1p-11f,
    0x1p-12f, 0x1p-13f, 0x1p-14f, 0x1p-15f, 0x1p-16f, 0x1p-17f, 0x1p-18f, 0x1p-19f, 0x1p-20f, 0x1p-21f, 0x1p-22f,
    0x1p-23f, 0x1p-24f, 0x1p-25f, 0x1p-26f, 0x1p-27f, 0x1p-28f, 0x1p-29f, 0x1p-30f, 0x1p-31f};
inline float read_component(const uint8_t* p, uint32_t format, uint32_t frac) {
  const float scale = kFracScale[frac & 31];
  switch (format) {
    case 0: return (float)p[0] * scale;
    case 1: return (float)(int8_t)p[0] * scale;
    case 2: return (float)(uint16_t)be16(p) * scale;
    case 3: return (float)(int16_t)be16(p) * scale;
    default: { uint32_t u = be32(p); float f; std::memcpy(&f, &u, 4); return f; }
  }
}

void read_color(const uint8_t* p, uint32_t format, uint8_t out[4]) {
  switch (format) {
    case 0: { uint32_t v = be16(p); out[0] = (uint8_t)((v >> 11) * 255 / 31); out[1] = (uint8_t)(((v >> 5) & 63) * 255 / 63); out[2] = (uint8_t)((v & 31) * 255 / 31); out[3] = 255; break; }
    case 1: out[0] = p[0]; out[1] = p[1]; out[2] = p[2]; out[3] = 255; break;
    case 2: out[0] = p[0]; out[1] = p[1]; out[2] = p[2]; out[3] = 255; break;
    case 3: { uint32_t v = be16(p); out[0] = (uint8_t)(((v >> 12) & 15) * 17); out[1] = (uint8_t)(((v >> 8) & 15) * 17); out[2] = (uint8_t)(((v >> 4) & 15) * 17); out[3] = (uint8_t)((v & 15) * 17); break; }
    case 4: { uint32_t v = ((uint32_t)p[0] << 16) | ((uint32_t)p[1] << 8) | p[2];
      out[0] = (uint8_t)(((v >> 18) & 63) * 255 / 63); out[1] = (uint8_t)(((v >> 12) & 63) * 255 / 63); out[2] = (uint8_t)(((v >> 6) & 63) * 255 / 63); out[3] = (uint8_t)((v & 63) * 255 / 63); break; }
    default: out[0] = p[0]; out[1] = p[1]; out[2] = p[2]; out[3] = p[3]; break;
  }
}

const uint8_t* array_ptr(uint32_t array, uint32_t index) {
  uint32_t base = g_cp.array_base(array), stride = g_cp.array_stride(array);
  return host::ptr(0x80000000u | ((base + stride * index) & 0x3FFFFFFFu));
}

// One vertex array resolved once per decode call: its host base and how many bytes follow it in the
// region it lives in. Every indexed attribute of every vertex used to go through array_ptr (two
// register reads and an out-of-line host::ptr range check each, ~47,000 calls a frame in a match).
// An index inside the region reads base + stride * index directly; anything else takes array_ptr,
// which wraps and fails exactly as before, so the bytes read are the same either way.
struct ArrayView { const uint8_t* base = nullptr; uint64_t avail = 0; uint32_t stride = 0, array = 0; };
ArrayView array_view(uint32_t array) {
  ArrayView v;
  v.array = array;
  v.stride = g_cp.array_stride(array);
  const uint32_t off = g_cp.array_base(array) & 0x3FFFFFFFu;
  if (const uint8_t* p = host::try_ptr(0x80000000u | off, 1)) {
    v.base = p;
    if (p >= host::ram && p < host::ram + host::ram_size) v.avail = (uint64_t)(host::ram + host::ram_size - p);
    else if (host::game_image && p >= host::game_image && p < host::game_image + host::game_image_size)
      v.avail = (uint64_t)(host::game_image + host::game_image_size - p);
    else v.base = nullptr;
  }
  return v;
}
inline const uint8_t* array_at(const ArrayView& v, uint32_t index, uint32_t bytes) {
  const uint64_t at = (uint64_t)v.stride * index;
  if (v.base && at + bytes <= v.avail) return v.base + at;
  return array_ptr(v.array, index);
}

// Decodes `count` vertices of format `fmt` from `src` into the frame. Returns components mask.
uint32_t decode_vertices(const VertexDesc& d, const uint8_t* src, uint32_t count, uint32_t fmt) {
  uint32_t components = 0;
  uint32_t mia = g_cp.matrix_index_a(), mib = g_cp.matrix_index_b();
  static const uint32_t csize[] = {2, 3, 4, 2, 3, 4, 4, 4};
  // Arrays and the bytes each attribute reads from them, resolved once for the whole call.
  ArrayView pos_view, nrm_view, col_view[2], tex_view[8];
  const uint32_t pos_bytes = comp_bytes(d.pos.format) * (d.pos.count ? 3 : 2);
  const uint32_t nrm_bytes = comp_bytes(d.nrm.format) * 3;
  if (d.pos.type >= 2) pos_view = array_view(d.pos.array);
  if (d.nrm.type >= 2) nrm_view = array_view(1);
  for (int i = 0; i < 2; ++i) if (d.col[i].type >= 2) col_view[i] = array_view(d.col[i].array);
  for (int i = 0; i < 8; ++i) if (d.tex[i].type >= 2) tex_view[i] = array_view(d.tex[i].array);
  // Everything that depends only on the format is worked out once per call: the components mask,
  // the matrix indices a vertex without its own takes from the CP registers, and the texture
  // coordinate sets that are present. Vertices are decoded straight into the frame's storage
  // (value-initialised, as `Vertex out{}` was), not built on the stack and copied in.
  if (d.posmtx) components |= VB_HAS_POSMTXIDX;
  uint8_t default_texmtx[8];
  for (int i = 0; i < 8; ++i) {
    default_texmtx[i] = (uint8_t)(i < 4 ? bits(mia, 6 + 6 * i, 6) : bits(mib, 6 * (i - 4), 6));
    if (d.texmtx[i]) components |= VB_HAS_TEXMTXIDX0 << i;
  }
  if (d.nrm.type) components |= VB_HAS_NRM0;
  for (int i = 0; i < 2; ++i) if (d.col[i].type) components |= i ? VB_HAS_COL1 : VB_HAS_COL0;
  struct TexSet { int index; uint32_t n, cb, bytes; };
  TexSet tex_sets[8];
  int tex_count = 0;
  for (int i = 0; i < 8; ++i) {
    if (!d.tex[i].type) continue;
    const uint32_t n = d.tex[i].count ? 2 : 1, cb = comp_bytes(d.tex[i].format);
    tex_sets[tex_count++] = {i, n, cb, cb * n};
    components |= VB_HAS_UV0 << i;
  }
  const uint32_t pos_n = d.pos.count ? 3 : 2, pos_cb = comp_bytes(d.pos.format);
  const uint32_t nrm_cb = comp_bytes(d.nrm.format);
  const uint32_t nrm_frac = d.nrm.format == 1 ? 6 : d.nrm.format == 3 ? 14 : d.nrm.format == 0 ? 7 : d.nrm.format == 2 ? 15 : 0;
  const uint32_t nrm_direct = nrm_cb * 3 * (d.nrm.count ? 3 : 1);
  const uint32_t nrm_skip8 = (d.nrm.count && d.nrm_index3) ? 3 : 1, nrm_skip16 = (d.nrm.count && d.nrm_index3) ? 6 : 2;
  const size_t base = g_frame.vertices.size();
  g_frame.vertices.resize(base + count);
  Vertex* const outv = g_frame.vertices.data() + base;
  for (uint32_t v = 0; v < count; ++v) {
    Vertex& out = outv[v];
    const uint8_t* p = src + (size_t)v * d.size;
    // matrix indices
    if (d.posmtx) out.posmtx = *p++;
    else out.posmtx = (uint8_t)(mia & 63);
    for (int i = 0; i < 8; ++i) out.texmtx[i] = d.texmtx[i] ? *p++ : default_texmtx[i];
    auto fetch = [&](const AttrDesc& x, const ArrayView& view, uint32_t direct_size) -> const uint8_t* {
      const uint8_t* q = nullptr;
      if (x.type == 1) { q = p; p += direct_size; }
      else if (x.type == 2) { q = array_at(view, *p, direct_size); p += 1; }
      else if (x.type == 3) { q = array_at(view, be16(p), direct_size); p += 2; }
      return q;
    };
    // position
    {
      const uint8_t* q = fetch(d.pos, pos_view, pos_bytes);
      if (q) for (uint32_t k = 0; k < pos_n; ++k) out.pos[k] = read_component(q + k * pos_cb, d.pos.format, d.pos.frac);
    }
    // normal (first vector only; binormal/tangent ignored for now)
    if (d.nrm.type) {
      const uint8_t* q = nullptr;
      if (d.nrm.type == 1) { q = p; p += nrm_direct; }
      else if (d.nrm.type == 2) { q = array_at(nrm_view, *p, nrm_bytes); p += nrm_skip8; }
      else { q = array_at(nrm_view, be16(p), nrm_bytes); p += nrm_skip16; }
      if (q) for (int k = 0; k < 3; ++k) out.nrm[k] = read_component(q + k * nrm_cb, d.nrm.format, nrm_frac);
    }
    // colors
    for (int i = 0; i < 2; ++i) {
      if (!d.col[i].type) { continue; }
      const uint8_t* q = fetch(d.col[i], col_view[i], csize[d.col[i].format]);
      uint8_t* dst = i ? out.col1 : out.col0;
      if (q) read_color(q, d.col[i].format, dst);
      if (!d.col[i].count) dst[3] = 255;
    }
    // texcoords
    for (int t = 0; t < tex_count; ++t) {
      const TexSet& set = tex_sets[t];
      const AttrDesc& x = d.tex[set.index];
      const uint8_t* q = fetch(x, tex_view[set.index], set.bytes);
      if (q) for (uint32_t k = 0; k < set.n; ++k) out.uv[set.index][k] = read_component(q + k * set.cb, x.format, x.frac);
    }
  }
  return components;
}

// ---------------- draw snapshot ----------------
void snapshot_textures(DrawCall& dc) {
  host::SimCostScope cost(host::SIM_SNAPSHOT);
  uint32_t stages = g_bp.numtevstages() + 1;
  for (uint32_t s = 0; s < stages; ++s) {
    if (!g_bp.order_enable(s)) continue;
    int map = g_bp.order_texmap(s);
    TextureRef& t = dc.textures[map];
    if (t.used) continue;
    t.used = true;
    uint32_t i0 = g_bp.teximage0(map);
    t.width = (i0 & 0x3FF) + 1;
    t.height = ((i0 >> 10) & 0x3FF) + 1;
    t.format = (i0 >> 20) & 0xF;
    t.addr = (g_bp.teximage3(map) & 0xFFFFFF) << 5;
    uint32_t tl = g_bp.textlut(map);
    t.tlut_addr = (tl & 0x3FF) << 9;
    t.tlut_format = (tl >> 10) & 3;
    t.mode0 = g_bp.texmode0(map);
    t.mode1 = g_bp.texmode1(map);
    uint32_t min_filter = (t.mode0 >> 5) & 7;
    // GX low two filter bits select mip filtering; high bit selects minification.
    bool use_mips = (min_filter & 3) != 0;
    uint32_t max_lod = (t.mode1 >> 8) & 0xFF;
    t.mip_levels = texture_mip_count(t.width, t.height, use_mips ? ((max_lod + 15) / 16) + 1 : 1);
  }
  // Indirect stages reference textures too.
  uint32_t ind = g_bp.numindstages();
  for (uint32_t i = 0; i < ind; ++i) {
    int map = bits(g_bp.iref(), 6 * i, 3);
    TextureRef& t = dc.textures[map];
    if (t.used) continue;
    t.used = true;
    uint32_t i0 = g_bp.teximage0(map);
    t.width = (i0 & 0x3FF) + 1; t.height = ((i0 >> 10) & 0x3FF) + 1; t.format = (i0 >> 20) & 0xF;
    t.addr = (g_bp.teximage3(map) & 0xFFFFFF) << 5;
    uint32_t tl = g_bp.textlut(map);
    t.tlut_addr = (tl & 0x3FF) << 9; t.tlut_format = (tl >> 10) & 3;
    t.mode0 = g_bp.texmode0(map); t.mode1 = g_bp.texmode1(map);
    t.mip_levels = 1;
  }
  for (TextureRef& t : dc.textures) {
    if (!t.used) continue;
    uint32_t total = texture_chain_bytes(t.width, t.height, t.format, t.mip_levels);
    uint32_t palette_bytes = t.format == 8 ? 32 : t.format == 9 ? 512 : t.format == 10 ? 32768 : 0;
    const uint8_t* texels = host::try_ptr(t.addr, total);
    uint32_t readable = total;
    if (!texels && host::try_ptr(t.addr, 1)) {
      // The texture starts in RAM but runs past its end: a mod's stage code left a 1024x1024 RGBA8
      // texture set at 0140DEC0 (0.8.83 report, Akaneia). The console's GPU reads whatever lies
      // there; the part inside RAM is used and the rest reads as zero, instead of stopping the game.
      uint32_t lo = 1, hi = total;
      while (lo < hi) { uint32_t mid = lo + (hi - lo + 1) / 2; if (host::try_ptr(t.addr, mid)) lo = mid; else hi = mid - 1; }
      readable = lo;
      static thread_local std::vector<uint8_t> padded;
      padded.assign(total, 0);
      std::memcpy(padded.data(), host::try_ptr(t.addr, readable), readable);
      texels = padded.data();
      static bool logged = false;
      if (!logged) { logged = true; host::log("gx: texture %08X+%X runs past RAM; the last %X bytes read as zero", t.addr, total, total - readable); }
    }
    if (!texels || t.tlut_addr > sizeof g_tmem || palette_bytes > sizeof g_tmem - t.tlut_addr)
      host::die("GX texture range invalid: %08X+%X, palette %X+%X", t.addr, total, t.tlut_addr, palette_bytes);
    // Native writes never pass through ppc::mark_ram_write, so the RAM-version shortcut cannot
    // see them. The native game cannot run while one FIFO batch is parsed, though, so the batch
    // number is a valid version: a texture drawn again in the same batch skips the texel compare
    // (that compare, repeated for every draw, was most of the native build's texsnap cost).
    const uint64_t source_version = host::game_image ? g_fifo_batch : ppc::watch_ram_range(t.addr & 0x3FFFFFFFu, readable);
    t.data = g_texture_snapshots.capture(texels, total, g_tmem + t.tlut_addr,
                                         palette_bytes, source_version,
                                         palette_bytes ? g_tmem_generation : 0);
    // MELEE_TEST_TEXSNAP_VERIFY=1 (tests): the snapshot a draw uses must equal the texels in memory.
    // A difference means memory changed without a write version (a host write that was not marked).
    static const bool verify = [] { const char* v = std::getenv("MELEE_TEST_TEXSNAP_VERIFY"); return v && *v == '1'; }();
    if (verify && t.data && (t.data->image.size() != total || std::memcmp(t.data->image.data(), texels, total) != 0)) {
      static uint32_t reported = 0;
      static uint64_t stale = 0;
      ++stale;
      if (reported < 40) {
        ++reported;
        host::log("texsnap: STALE snapshot for the texture at %08X (%u bytes, %ux%u format %u), %llu so far",
                  t.addr, total, (unsigned)t.width, (unsigned)t.height, (unsigned)t.format, (unsigned long long)stale);
      }
    }
  }
}

void record_draw(uint32_t primitive, uint32_t first, uint32_t count, uint32_t components) {
  host::SimCostScope record_cost(host::SIM_RECORD);
  const bool line = primitive == 0xA8 || primitive == 0xB0;
  if (g_merge_next_primitive && !g_frame.commands.empty() &&
      g_frame.commands.back().kind == FrameCommand::Draw) {
    DrawCall& previous = g_frame.draws[g_frame.commands.back().index];
    const bool previous_line = previous.primitive == 0xA8 || previous.primitive == 0xB0;
    if (line == previous_line && previous.first_vertex + previous.vertex_count == first &&
        previous.components == components &&
        previous.first_segment + previous.segment_count == g_frame.segments.size()) {
      g_frame.segments.push_back({first, count, primitive});
      previous.vertex_count += count;
      ++previous.segment_count;
      ++g_draws;
      g_vertices += count;
      return;
    }
  }
  // Built on the stack (hot in cache), then moved in: filling the vector element directly measured
  // worse, because each field write lands in cold memory instead of one sequential copy.
  DrawCall dc{DrawCall::SkipInit{}};
  dc.primitive = primitive;
  dc.first_vertex = first;
  dc.vertex_count = count;
  dc.first_segment = (uint32_t)g_frame.segments.size();
  dc.segment_count = 1;
  dc.components = components;
  dc.bp = g_bp;
  std::memcpy(dc.posMatrices, g_xf.posMatrices, sizeof dc.posMatrices);
  std::memcpy(dc.normalMatrices, g_xf.normalMatrices, sizeof dc.normalMatrices);
  std::memcpy(dc.xf_regs, &g_xf.raw[0x1000], sizeof dc.xf_regs);
  // Post-transform matrices and lights are 1.5 KB of the draw and most draws use neither; the
  // renderer reads them under the same tests (fill_vs_constants), so skipped copies are never read.
  if (dc.xf_regs[0x12] & 1) std::memcpy(dc.postMatrices, g_xf.postMatrices, sizeof dc.postMatrices);
  bool lit = false;
  for (uint32_t j = 0; j < (dc.xf_regs[0x09] & 3); ++j) lit = lit || lit_enable(dc.xf_regs[0x0E + j]) || lit_enable(dc.xf_regs[0x10 + j]);
  if (lit) std::memcpy(dc.lights, g_xf.lights, sizeof dc.lights);
  dc.matrix_index_a = g_cp.matrix_index_a();
  dc.matrix_index_b = g_cp.matrix_index_b();
  std::memcpy(dc.tev_colors, g_tev_colors, sizeof g_tev_colors);
  std::memcpy(dc.tev_kcolors, g_tev_kcolors, sizeof g_tev_kcolors);
  snapshot_textures(dc);
  if (g_dl_addr) {
    dc.identity = hash_bytes(&g_dl_addr, 4) ^ ((uint64_t)g_dl_call_ordinal << 40) ^ ((uint64_t)g_dl_draw_ordinal << 20) ^ 1;
    ++g_dl_draw_ordinal;
  } else {
    uint64_t k = ((uint64_t)dc.textures[0].addr << 32) ^ ((uint64_t)count << 8) ^ primitive;
    uint32_t ordinal = g_immediate_draws[k]++;
    dc.identity = hash_bytes(&k, 8) ^ ((uint64_t)ordinal << 44) ^ 2;
  }
  { host::SimCostScope cost(host::SIM_OBSERVE);
    dc.identity = observed_draw_identity(dc.identity, dc.object_generation);
    if (!dc.object_generation && !g_native_draw_scopes.empty() && g_native_draw_scopes.back().object &&
        g_native_draw_scopes.back().piece) {
      NativeDrawScope& scope = g_native_draw_scopes.back();
      const uint64_t key[] = {scope.piece, scope.ordinal, scope.draws++};
      dc.object_generation = scope.object;
      dc.identity = hash_bytes(key, sizeof key) ^ 3;
    }
    dc.owner_player = g_native_owner_source ? g_native_owner : observed_owner();   // SkipInit above means this has to be written every draw
    if (native_pose_capture_enabled()) {
      dc.skinned = active_native_scope_is_skinned();
      dc.authored_pose = active_native_authored_pose();
    } else {
      dc.skinned = g_native_owner_source ? g_native_skinned : observed_skinned();
      dc.authored_pose = capture_authored_pose();
    }
    if (dc.skinned) ++authored_stats().skinned_draws;
    if (dc.authored_pose) {
      ++authored_stats().posed_draws;
      if (dc.authored_pose->envelope) ++authored_stats().posed_draws_envelope;
    } }
  g_frame.draws.push_back(std::move(dc));
  g_frame.segments.push_back({first, count, primitive});
  g_frame.commands.push_back({FrameCommand::Draw, (uint32_t)g_frame.draws.size() - 1});
  ++g_draws;
  g_vertices += count;
}

// ---------------- BP side effects ----------------
void bp_write(uint32_t value) {
  uint8_t r = (uint8_t)(value >> 24);
  uint32_t v = value & 0xFFFFFF;
  if (r == BP_BP_MASK) { g_bp_mask = v; return; }
  uint32_t masked = (g_bp.reg[r] & ~g_bp_mask) | (v & g_bp_mask);
  g_bp_mask = 0xFFFFFF;
  g_bp.reg[r] = masked;
  if (r >= BP_TEV_COLOR_RA && r <= BP_TEV_COLOR_RA + 7) {
    // TEV color/konst registers share BP slots; the type bit selects the destination at write time.
    int idx = (r - BP_TEV_COLOR_RA) / 2;
    bool bg = r & 1;
    int32_t (*dst)[4] = (masked & (1 << 23)) ? g_tev_kcolors : g_tev_colors;
    if (bg) { dst[idx][2] = sbits(masked, 0, 11); dst[idx][1] = sbits(masked, 12, 11); }
    else { dst[idx][0] = sbits(masked, 0, 11); dst[idx][3] = sbits(masked, 12, 11); }
    return;
  }
  switch (r) {
    case BP_SETDRAWDONE:
      if (masked & 2) host::set_pe_finish_pending();
      break;
    case BP_PE_TOKEN_INT_ID:
      host::set_pe_token_pending((uint16_t)masked);
      break;
    case BP_LOADTLUT1: {
      uint32_t tmem_addr = (masked & 0x3FF) << 9;
      uint32_t count = (masked & 0x1FFC00) >> 5;
      // BP_LOADTLUT0 carries the source address in bits 0..20 only; bits 21..23 are
      // reserved and the GP ignores them. GXInitTlutObj writes just the 21-bit field
      // and the 8-bit register id, so whatever the caller's uninitialised GXTlutObj
      // held in between rides along (tobj.c and psdisp.c both use a stack local).
      // Mask to the real field or those stale bits become a wild address.
      uint32_t src = ((g_bp.reg[BP_LOADTLUT0] & 0x1FFFFFu) << 5) & 0x3FFFFFFFu;
      if (tmem_addr + count <= sizeof g_tmem) {
        std::memcpy(g_tmem + tmem_addr, host::ptr(0x80000000u | src, count), count);
        ++g_tmem_generation;
      }
      break;
    }
    case BP_TRIGGER_EFB_COPY: {
      EfbCopy c{};
      c.dest_addr = g_bp.reg[BP_EFB_ADDR] << 5;
      c.dest_stride = g_bp.reg[BP_MIPMAP_STRIDE] << 5;
      uint32_t tl = g_bp.reg[BP_EFB_TL], br = g_bp.reg[BP_EFB_BR];
      c.src_x = tl & 0x3FF; c.src_y = (tl >> 10) & 0x3FF;
      c.src_w = (br & 0x3FF) + 1; c.src_h = ((br >> 10) & 0x3FF) + 1;
      uint32_t tpf = bits(masked, 3, 4);
      c.format = tpf / 2 + (tpf & 1) * 8;
      c.to_xfb = bits(masked, 14, 1);
      c.clear = bits(masked, 11, 1);
      c.intensity = bits(masked, 15, 1);
      c.half_scale = bits(masked, 9, 1);
      c.is_depth = (g_bp.zcontrol() & 7) == 3;
      c.efb_alpha = (g_bp.zcontrol() & 7) == 1;   // RGBA6_Z24
      c.clear_color = ((g_bp.reg[BP_CLEAR_AR] & 0xFF) << 24) | ((g_bp.reg[BP_CLEAR_AR] & 0xFF00) << 8) |
                      ((g_bp.reg[BP_CLEAR_GB] & 0xFF00) >> 8 << 8) | (g_bp.reg[BP_CLEAR_GB] & 0xFF);
      // clear_color layout: A(31..24) R(23..16) G(15..8) B(7..0)
      c.clear_color = ((g_bp.reg[BP_CLEAR_AR] >> 8) & 0xFF) << 24 | (g_bp.reg[BP_CLEAR_AR] & 0xFF) << 16 |
                      ((g_bp.reg[BP_CLEAR_GB] >> 8) & 0xFF) << 8 | (g_bp.reg[BP_CLEAR_GB] & 0xFF);
      c.clear_z = g_bp.reg[BP_CLEAR_Z] & 0xFFFFFF;
      uint32_t yscale = g_bp.reg[BP_COPYYSCALE];
      c.y_scale = bits(masked, 10, 1) ? 256.0f / (float)yscale : (float)yscale / 256.0f;
      g_frame.copies.push_back(c);
      g_frame.commands.push_back({FrameCommand::Copy, (uint32_t)g_frame.copies.size() - 1});
      ++g_efb_copies;
      if (c.to_xfb) {
        g_frame.sequence = ++g_frame_sequence;
        // Through the host, not guest memory: the native game keeps its scene in its own image, so
        // a read at the console address made every native match look like a menu to the renderer.
        { uint32_t major = 0, minor = 0, match_frame = 0;
          host::current_scene(&major, &minor, &match_frame);
          g_frame.scene_major = (uint8_t)major; g_frame.scene_minor = (uint8_t)minor; }
        // The HUD objects and the guest Settings menu row are read and written at their console
        // addresses, which only the recompiled build keeps in guest RAM: the native game holds
        // them in its own image with its own layout, so it skips both.
        if (host::game_image) {
          // The native game reports its players each drawn frame (set_native_hud_player).
          if (frame_in_match(g_frame)) {
            g_frame.hud_players = g_native_hud;
            g_frame.player_names = slippi::online::player_names_for_overlay();
          } else {
            g_native_hud = {};
          }
        }
        if (!host::game_image) {
          capture_match_hud(g_frame);
          const uint8_t options_menu = host::rd8(0x804A04F0);
          const uint16_t options_selection = host::rd16(0x804A04F2);
          const uint32_t options_buttons = host::rd32(0x804A04FC);
          gx::settings_guest_options_frame(options_menu, options_selection, options_buttons);
          if (options_menu == 4 && options_selection == 3 && (options_buttons & 0x10))
            host::wr8(0x804A0501, 0); // no guest submenu owns the PC Settings row
        }
        {
          static uint16_t last_scene = 0xFFFF;
          const uint16_t scene = (uint16_t)(g_frame.scene_major << 8 | g_frame.scene_minor);
          if (scene != last_scene) { host::log("scene: major %02X minor %02X (frame %llu)", g_frame.scene_major, g_frame.scene_minor, (unsigned long long)g_frame.sequence); last_scene = scene; }
        }
        g_frame.time = host::now_seconds(); // completed snapshot availability anchors presentation
        g_frame.tick = host::tick_timing();
        g_frame.discontinuous = g_discontinuity;
        g_discontinuity = false;
        // A replay viewer on the way to a jump's target simulates frames nobody sees: they are not
        // handed to the renderer, and the next frame that is shown is not the last shown one's neighbour.
        const bool unseen = replay_control::skip_render();
        if (g_backend && !unseen) g_backend->submit_and_recycle(g_frame);   // hands over the buffers, returns recycled ones
        else g_frame.clear();
        if (unseen) g_discontinuity = true;
        g_texture_snapshots.end_frame();
        g_dl_calls.clear(); g_immediate_draws.clear(); g_native_object_scopes.clear(); g_native_draw_scopes.clear(); native_pose_bridge_frame_reset(); finish_observed_frame();
        host::note_frame_submitted();   // the game now only waits for the retrace (audio pacing, host.cpp)
      }
      break;
    }
    default:
      break;
  }
}

// ---------------- XF ----------------
void xf_load(uint32_t address, uint32_t count, const uint8_t* data) {
  for (uint32_t i = 0; i < count; ++i) {
    uint32_t a = address + i;
    if (a < 0x1058) g_xf.raw[a] = be32(data + i * 4);
  }
}

void xf_indexed_load(uint32_t op, uint32_t value) {
  uint32_t index = value >> 16, address = value & 0xFFF, size = ((value >> 12) & 0xF) + 1;
  uint32_t array = 12 + (op - 0x20) / 8;
  const uint8_t* src = array_ptr(array, index);
  xf_load(address, size, src);
}

// ---------------- command parsing ----------------
size_t parse_command(const uint8_t* d, size_t len);

void run_display_list(uint32_t addr, uint32_t size) {
  const uint8_t* p = host::try_ptr(addr, size);
  if (!p) { host::log("gx: display list outside RAM %08X+%X (parent %08X, vcd %08X/%08X)", addr, size, g_dl_addr, g_cp.reg[0x50], g_cp.reg[0x60]); return; }
  uint32_t saved_addr = g_dl_addr, saved_draw = g_dl_draw_ordinal, saved_call = g_dl_call_ordinal;
  g_dl_addr = addr; g_dl_draw_ordinal = 0; g_dl_call_ordinal = g_dl_calls[addr]++;
  size_t used = 0;
  bool previous_primitive = false;
  const bool saved_merge = g_merge_next_primitive;
  while (used < size) {
    const uint8_t op = p[used];
    const bool primitive = op >= 0x80 && op < 0xC0 && used + 3 <= size && be16(p + used + 1) != 0;
    g_merge_next_primitive = previous_primitive && primitive;
    size_t n = parse_command(p + used, size - used);
    if (!n) break;
    used += n;
    previous_primitive = primitive;
  }
  g_merge_next_primitive = saved_merge;
  g_dl_addr = saved_addr; g_dl_draw_ordinal = saved_draw; g_dl_call_ordinal = saved_call;
}

size_t parse_command(const uint8_t* d, size_t len) {
  uint8_t op = d[0];
  if (op == 0x00) return 1;
  if (op == 0x08) { if (len < 6) return incomplete(6); g_cp.reg[d[1]] = be32(d + 2); return 6; }
  if (op == 0x10) {
    if (len < 5) return incomplete(5);
    uint32_t count = (be16(d + 1) & 0xF) + 1, address = be16(d + 3);
    size_t need = 5 + count * 4;
    if (len < need) return incomplete(need);
    xf_load(address, count, d + 5);
    return need;
  }
  if (op == 0x20 || op == 0x28 || op == 0x30 || op == 0x38) {
    if (len < 5) return incomplete(5);
    xf_indexed_load(op, be32(d + 1));
    return 5;
  }
  if (op == 0x40) {
    if (len < 9) return incomplete(9);
    run_display_list(be32(d + 1), be32(d + 5));
    return 9;
  }
  if (op == 0x48) return 1;
  // Private diagnostic token emitted only by MU_NATIVE and consumed before GX rendering.
  // Opcode 0xF0 is not a GX command; it is present only when the optional audit is enabled.
  if (op == 0xF0) {
    if (len < 21) return incomplete(21);
    const NativeRenderScopeEvent event{be32(d + 1), be32(d + 5), be32(d + 9), 0};
    if (event.phase == NATIVE_SCOPE_BEGIN) {
      const uint32_t object = be32(d + 13), piece = be32(d + 17);
      g_native_draw_scopes.push_back({object, piece, piece ? g_native_object_scopes[piece]++ : 0, 0});
    } else if (event.phase == NATIVE_SCOPE_END && !g_native_draw_scopes.empty()) {
      g_native_draw_scopes.pop_back();
    }
    g_native_draw_audit.note_streamed_event(event);
    if (native_pose_capture_enabled()) {
      if (event.phase == NATIVE_SCOPE_BEGIN) {
        g_native_pose_scope_ids.push_back(event.scope_id);
      } else if (event.phase == NATIVE_SCOPE_END) {
        if (!g_native_pose_scope_ids.empty() &&
            g_native_pose_scope_ids.back() == event.scope_id)
          g_native_pose_scope_ids.pop_back();
        else
          g_native_pose_scope_ids.clear();
      }
    }
    if (event.phase == NATIVE_SCOPE_END) finish_native_pose_scope(event.scope_id);
    return 21;
  }
  // Private token from the native fighter render callback: the owning player slot of the draws
  // that follow, 0xFF for none. Only sent while owner tracking is on (MuHostApi.native_owner_tracking).
  if (op == 0xF1) {
    if (len < 2) return incomplete(2);
    g_native_owner = d[1];
    return 2;
  }
  // Private token from native HSD_PObjDisp while owners are tracked: the PObj is an envelope draw.
  if (op == 0xF2) {
    if (len < 2) return incomplete(2);
    g_native_skinned = d[1] != 0;
    return 2;
  }
  if (op == 0x61) { if (len < 5) return incomplete(5); bp_write(be32(d + 1)); return 5; }
  if (op >= 0x80 && op < 0xC0) {
    if (len < 3) return incomplete(3);
    uint32_t fmt = op & 7, count = be16(d + 1);
    VertexDesc desc = build_desc(fmt);
    size_t need = 3 + (size_t)count * desc.size;
    if (len < need) return incomplete(need);
    if (count) {
      g_native_draw_audit.note_decoded_draw();
      uint32_t first = (uint32_t)g_frame.vertices.size();
      uint32_t components = decode_vertices(desc, d + 3, count, fmt);
      record_draw(op & 0xF8, first, count, components);
    }
    return need;
  }
  static int reported = 0;
  if (reported++ < 10) host::log("gx: unknown opcode %02X (list %08X, data %p, vcd %08X/%08X)", op, g_dl_addr, d, g_cp.reg[0x50], g_cp.reg[0x60]);
  return 1;
}

}  // namespace

void mark_discontinuity() { g_discontinuity = true; }

void set_hud_scales(int stocks_percent, int damage_percent, bool pal_stocks) {
  g_stock_hud_scale.store(stocks_percent, std::memory_order_relaxed);
  g_damage_hud_scale.store(damage_percent, std::memory_order_relaxed);
  g_pal_stock_hud.store(pal_stocks, std::memory_order_relaxed);
}

void set_low_poly_fighters(bool on) { g_low_poly_fighters.store(on, std::memory_order_relaxed); }
bool low_poly_fighters_active() { return g_low_poly_fighters.load(std::memory_order_relaxed); }

void set_native_hud_player(int slot, bool present, int damage, int stocks, float tag_x, float tag_y,
                           bool tag_visible) {
  if (slot < 0 || slot >= 4) return;
  auto& out = g_native_hud[slot];
  out.present = present && damage >= 0 && damage <= 999 && stocks >= 0 && stocks <= 99;
  out.damage = out.present ? damage : 0;
  out.stocks = out.present ? stocks : 0;
  out.tag_x = tag_x; out.tag_y = tag_y;
  out.tag_visible = out.present && tag_visible && std::isfinite(tag_x) && std::isfinite(tag_y) &&
                    tag_x >= 0 && tag_x <= 640 && tag_y >= 0 && tag_y <= 480;
}

uint32_t hud_scales_packed() {
  const auto clamp_percent = [](int p) { return (uint32_t)std::clamp(p, 75, 175); };
  return clamp_percent(g_stock_hud_scale.load(std::memory_order_relaxed)) |
         clamp_percent(g_damage_hud_scale.load(std::memory_order_relaxed)) << 8;
}

void init(Backend* backend) {
  g_backend = backend;
  std::memset(&g_bp, 0, sizeof g_bp);
  std::memset(&g_cp, 0, sizeof g_cp);
  std::memset(&g_xf, 0, sizeof g_xf);
  std::memset(g_tmem, 0, sizeof g_tmem);
  g_frame.clear();
  g_native_draw_audit.reset(false);
  g_native_pose_scopes.clear();
  g_native_pose_skinned_scopes.clear();
  g_native_pose_scope_ids.clear();
  g_native_pose_stats = {};
  g_frame.reserve_gameplay_capacity();
}

// Parses whatever complete commands g_buf now holds.
static void drain_fifo();

void write_fifo(uint32_t value, int bytes) {
  const size_t at = g_buf.size();
  g_buf.resize(at + (size_t)bytes);   // one size update per write instead of a push_back per byte
  for (int i = 0; i < bytes; ++i) g_buf[at + i] = (uint8_t)(value >> (8 * (bytes - 1 - i)));
  drain_fifo();
}

// The same stream handed over in bulk, already in the pipe's big-endian byte order.
void write_fifo_bytes(const uint8_t* data, size_t bytes) {
  ++g_fifo_batch;
  g_buf.insert(g_buf.end(), data, data + bytes);
  drain_fifo();
}

static void drain_fifo() {
  if (g_buf.size() - g_buf_pos < g_parse_need) return;   // the pending command is still incomplete
  g_parse_need = 0;
  host::SimCostScope cost(host::SIM_DECODE);   // everything the simulation thread spends turning FIFO bytes into draws
  while (g_buf_pos < g_buf.size()) {
    size_t n = parse_command(g_buf.data() + g_buf_pos, g_buf.size() - g_buf_pos);
    if (!n) break;               // parse_command recorded how many bytes it needs
    g_buf_pos += n;
    g_parse_need = 0;            // a display list inside the command may have left a stale need
    ++g_commands;
  }
  if (g_buf_pos == g_buf.size()) { g_buf.clear(); g_buf_pos = 0; }
  else if (g_buf_pos >= 1 << 16) { g_buf.erase(g_buf.begin(), g_buf.begin() + g_buf_pos); g_buf_pos = 0; }
}

void stats(uint64_t* commands, uint64_t* draws, uint64_t* vertices, uint32_t* efb_copies) {
  if (commands) *commands = g_commands;
  if (draws) *draws = g_draws;
  if (vertices) *vertices = g_vertices;
  if (efb_copies) *efb_copies = g_efb_copies;
}

void set_native_draw_audit(bool enabled) { g_native_draw_audit.reset(enabled); }
void native_render_scope_event(const NativeRenderScopeEvent& event) { g_native_draw_audit.note_submitted_event(event); }
NativeDrawAuditStats native_draw_audit_stats() { return g_native_draw_audit.stats(); }
bool native_draw_audit_enabled() { return g_native_draw_audit.enabled(); }
void set_native_pose_capture_enabled(bool enabled) {
  g_native_pose_capture_enabled.store(enabled, std::memory_order_relaxed);
  if (!enabled) {
    g_native_pose_scopes.clear();
    g_native_pose_skinned_scopes.clear();
    g_native_pose_scope_ids.clear();
  }
}
void set_native_owner_source(bool native) { g_native_owner_source = native; g_native_owner = 0xFF; g_native_skinned = false; }
static std::atomic<bool> g_authored_capture_wanted{true};
void set_authored_capture_wanted(bool wanted) { g_authored_capture_wanted.store(wanted, std::memory_order_relaxed); }
bool authored_capture_wanted() { return g_authored_capture_wanted.load(std::memory_order_relaxed); }
bool native_pose_capture_enabled() {
  return g_native_pose_capture_enabled.load(std::memory_order_relaxed) && authored_capture_wanted();
}
void native_pose_snapshot(const MuNativePoseSnapshot* snapshot) {
  if (!snapshot || !native_pose_capture_enabled()) return;
  ++g_native_pose_stats.snapshots;
  if (snapshot->scope_id == 0) {
    ++g_native_pose_stats.invalid_payloads;
    return;
  }
  const bool envelope = snapshot->kind == MU_NATIVE_POSE_ENVELOPE;
  if (envelope) ++g_native_pose_stats.envelope_scopes;
  g_native_pose_skinned_scopes[snapshot->scope_id] = envelope;
  if ((snapshot->kind != MU_NATIVE_POSE_RIGID && !envelope) ||
      snapshot->reject_reason != MU_NATIVE_POSE_REJECT_NONE) {
    const uint32_t reason = snapshot->reject_reason;
    if (reason > 0 && reason < MU_NATIVE_POSE_REJECT_COUNT)
      ++g_native_pose_stats.rejected[reason];
    else
      ++g_native_pose_stats.invalid_payloads;
    return;
  }
  auto pose = envelope ? copy_native_envelope_snapshot(*snapshot)
                       : copy_native_pose_snapshot(*snapshot);
  if (!pose) {
    ++g_native_pose_stats.invalid_payloads;
    return;
  }
  g_native_pose_scopes[snapshot->scope_id] = std::move(pose);
  ++(envelope ? g_native_pose_stats.envelope_captured : g_native_pose_stats.rigid_captured);
  ++(snapshot->default_setup ? g_native_pose_stats.captured_default_setup
                             : g_native_pose_stats.captured_custom_setup);
  ++authored_stats().captured;
  if (envelope) ++authored_stats().captured_envelope;
}
std::shared_ptr<const AuthoredPose> active_native_authored_pose() {
  const uint32_t scope = active_native_pose_scope_id();
  auto it = g_native_pose_scopes.find(scope);
  return it == g_native_pose_scopes.end() ? std::shared_ptr<const AuthoredPose>{} : it->second;
}
bool active_native_scope_is_skinned() {
  const uint32_t scope = active_native_pose_scope_id();
  auto it = g_native_pose_skinned_scopes.find(scope);
  return it != g_native_pose_skinned_scopes.end() && it->second;
}
NativePoseBridgeStats native_pose_bridge_stats() { return g_native_pose_stats; }
void finish_native_pose_scope(uint32_t scope_id) {
  g_native_pose_scopes.erase(scope_id);
  g_native_pose_skinned_scopes.erase(scope_id);
}

const uint8_t* tmem() { return g_tmem; }

}  // namespace gx
