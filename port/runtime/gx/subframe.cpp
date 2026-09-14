// SPDX-License-Identifier: GPL-2.0-or-later
#include "subframe.h"
#include "authored_pose.h"
#include <cmath>
#include <cstring>
#include <thread>
#include <mutex>
#include <functional>
#include <condition_variable>

namespace gx {
namespace {

// Command latches describe earlier FIFO operations, not the draw's material.
// In particular PE tokens and alternating XFB destinations change every frame.
bool same_draw_bp(const BPMemory& a, const BPMemory& b, uint32_t& changed) {
  for (unsigned i = 0; i < 256; ++i) {
    if (i == BP_SETDRAWDONE || i == BP_PE_TOKEN_ID || i == BP_PE_TOKEN_INT_ID ||
        (i >= BP_EFB_TL && i <= 0x54) || (i >= BP_PRELOAD_ADDR && i <= BP_TEXINVALIDATE) ||
        i == BP_BP_MASK || (i >= 0x8C && i <= 0x97) || (i >= 0xAC && i <= 0xB7) ||
        (i >= BP_TEV_COLOR_RA && i <= BP_TEV_COLOR_RA + 7)) continue;
    // 0xE0..0xE7 are the TEV constant colours: a draw that fades or flashes is still the same
    // object, and the colours actually rendered come from the current frame's draw either way.
    // 0x8C..0x97 are the texture TMEM layout and source address: where the texture lives, not what
    // it is. Effects regenerate their textures into a different buffer every frame, and comparing
    // the addresses made every one of those draws unpairable. same_textures compares the content.
    if (a.reg[i] != b.reg[i]) { changed = i; return false; }
  }
  return true;
}

bool same_textures(const DrawCall& a, const DrawCall& b) {
  for (unsigned i = 0; i < 8; ++i) {
    const auto& x = a.textures[i]; const auto& y = b.textures[i];
    if (x.used != y.used) return false;
    if (!x.used) continue;
    // The address is deliberately not compared: the same material can be regenerated into a
    // different buffer each frame. Shape, format and sampler state must still match.
    if (x.width != y.width || x.height != y.height ||
        x.format != y.format || x.mip_levels != y.mip_levels || x.tlut_format != y.tlut_format ||
        x.mode0 != y.mode0 || x.mode1 != y.mode1) return false;
    if (x.data == y.data) continue;
    if (!x.data || !y.data || x.data->hash != y.data->hash ||
        x.data->image != y.data->image || x.data->palette != y.data->palette) return false;
  }
  return true;
}

uint32_t texture_matrix_index(const DrawCall& d, unsigned generator) {
  return generator < 4 ? bits(d.matrix_index_a, 6 + 6 * generator, 6)
                       : bits(d.matrix_index_b, 6 * (generator - 4), 6);
}

bool same_matrix_bindings(const DrawCall& a, const DrawCall& b) {
  if (!(b.components & VB_HAS_POSMTXIDX) && (a.matrix_index_a & 63) != (b.matrix_index_a & 63)) return false;
  if (a.xf_regs[0x3F] != b.xf_regs[0x3F] || (a.xf_regs[0x12] & 1) != (b.xf_regs[0x12] & 1)) return false;
  for (unsigned k = 0; k < std::min(8u, b.xf_regs[0x3F] & 15); ++k) {
    if (a.xf_regs[0x40 + k] != b.xf_regs[0x40 + k]) return false;
    if ((b.xf_regs[0x12] & 1) && a.xf_regs[0x50 + k] != b.xf_regs[0x50 + k]) return false;
    if (!(b.components & (VB_HAS_TEXMTXIDX0 << k)) &&
        texture_matrix_index(a, k) != texture_matrix_index(b, k)) return false;
  }
  return true;
}

// 3x4 row-major affine (XF layout): rows r0..r2, translation in column 3.
struct Affine { float m[12]; };

Affine mul(const Affine& a, const Affine& b) {   // a * b
  Affine r;
  for (int i = 0; i < 3; ++i)
    for (int j = 0; j < 4; ++j) {
      float s = a.m[i * 4 + 0] * b.m[0 * 4 + j] + a.m[i * 4 + 1] * b.m[1 * 4 + j] + a.m[i * 4 + 2] * b.m[2 * 4 + j];
      if (j == 3) s += a.m[i * 4 + 3];
      r.m[i * 4 + j] = s;
    }
  return r;
}

bool invert(const Affine& a, Affine& out) {
  const float* m = a.m;
  double det = (double)m[0] * (m[5] * m[10] - m[6] * m[9]) - (double)m[1] * (m[4] * m[10] - m[6] * m[8]) + (double)m[2] * (m[4] * m[9] - m[5] * m[8]);
  if (std::fabs(det) < 1e-12) return false;
  double id = 1.0 / det;
  float inv[9] = {
      (float)((m[5] * m[10] - m[6] * m[9]) * id), (float)((m[2] * m[9] - m[1] * m[10]) * id), (float)((m[1] * m[6] - m[2] * m[5]) * id),
      (float)((m[6] * m[8] - m[4] * m[10]) * id), (float)((m[0] * m[10] - m[2] * m[8]) * id), (float)((m[2] * m[4] - m[0] * m[6]) * id),
      (float)((m[4] * m[9] - m[5] * m[8]) * id),  (float)((m[1] * m[8] - m[0] * m[9]) * id),  (float)((m[0] * m[5] - m[1] * m[4]) * id)};
  for (int i = 0; i < 3; ++i) {
    out.m[i * 4 + 0] = inv[i * 3 + 0]; out.m[i * 4 + 1] = inv[i * 3 + 1]; out.m[i * 4 + 2] = inv[i * 3 + 2];
    out.m[i * 4 + 3] = -(inv[i * 3 + 0] * m[3] + inv[i * 3 + 1] * m[7] + inv[i * 3 + 2] * m[11]);
  }
  return true;
}

struct Quat { double w, x, y, z; };

Quat quat_from_rotation(const double r[9]) {   // row-major 3x3, assumed orthonormal
  Quat q;
  double tr = r[0] + r[4] + r[8];
  if (tr > 0) {
    double s = std::sqrt(tr + 1.0) * 2;
    q.w = 0.25 * s; q.x = (r[7] - r[5]) / s; q.y = (r[2] - r[6]) / s; q.z = (r[3] - r[1]) / s;
  } else if (r[0] > r[4] && r[0] > r[8]) {
    double s = std::sqrt(1.0 + r[0] - r[4] - r[8]) * 2;
    q.w = (r[7] - r[5]) / s; q.x = 0.25 * s; q.y = (r[1] + r[3]) / s; q.z = (r[2] + r[6]) / s;
  } else if (r[4] > r[8]) {
    double s = std::sqrt(1.0 + r[4] - r[0] - r[8]) * 2;
    q.w = (r[2] - r[6]) / s; q.x = (r[1] + r[3]) / s; q.y = 0.25 * s; q.z = (r[5] + r[7]) / s;
  } else {
    double s = std::sqrt(1.0 + r[8] - r[0] - r[4]) * 2;
    q.w = (r[3] - r[1]) / s; q.x = (r[2] + r[6]) / s; q.y = (r[5] + r[7]) / s; q.z = 0.25 * s;
  }
  double n = std::sqrt(q.w * q.w + q.x * q.x + q.y * q.y + q.z * q.z);
  if (n > 0) { q.w /= n; q.x /= n; q.y /= n; q.z /= n; }
  return q;
}

void rotation_from_quat(const Quat& q, double r[9]) {
  double xx = q.x * q.x, yy = q.y * q.y, zz = q.z * q.z, xy = q.x * q.y, xz = q.x * q.z, yz = q.y * q.z, wx = q.w * q.x, wy = q.w * q.y, wz = q.w * q.z;
  r[0] = 1 - 2 * (yy + zz); r[1] = 2 * (xy - wz);     r[2] = 2 * (xz + wy);
  r[3] = 2 * (xy + wz);     r[4] = 1 - 2 * (xx + zz); r[5] = 2 * (yz - wx);
  r[6] = 2 * (xz - wy);     r[7] = 2 * (yz + wx);     r[8] = 1 - 2 * (xx + yy);
}

}  // namespace

void SubFrameSolver::interpolate_matrix(const float prev[12], const float cur[12], double t, float out[12]) {
  fractional(prev, cur, t, true, 40.0f, 1.2f, out, nullptr, nullptr, nullptr, nullptr);
}

void SubFrameSolver::extrapolate_matrix(const float prev[12], const float cur[12], double t, float out[12]) {
  fractional(prev, cur, t, false, 40.0f, 1.2f, out, nullptr, nullptr, nullptr, nullptr);
}

void SubFrameSolver::fractional(const float prev[12], const float cur[12], double t, bool interpolate,
                                float max_translation, float max_rotation, float out_pos[12], float out_nrm[9],
                                const float cur_nrm[9], const float prev_nrm[9], SubFrameStats* stats) {
  const float* base = interpolate ? prev : cur;
  const float* base_nrm = interpolate ? prev_nrm : cur_nrm;
  auto exact = [&](bool current = true) {
    std::memcpy(out_pos, current ? cur : base, 12 * sizeof(float));
    const float* normals = current ? cur_nrm : base_nrm;
    if (out_nrm && normals) std::memcpy(out_nrm, normals, 9 * sizeof(float));
  };
  if (!std::isfinite(t)) { exact(); if (stats) ++stats->cuts; return; }
  for (int i = 0; i < 12; ++i) if (!std::isfinite(prev[i]) || !std::isfinite(cur[i])) {
    exact(); if (stats) ++stats->cuts; return;
  }
  if (interpolate && t >= 1.0) { exact(); return; }
  if (t <= 0.0) { exact(false); return; }
  t = std::min(t, 1.0);
  if (std::memcmp(prev, cur, 12 * sizeof(float)) == 0) { exact(); return; }
  Affine P, C, Pi;
  std::memcpy(P.m, prev, sizeof P.m); std::memcpy(C.m, cur, sizeof C.m);
  if (!invert(P, Pi)) { exact(); if (stats) ++stats->cuts; return; }
  Affine D = mul(C, Pi);
  // Decompose the 3x3 of D into scale per column and a rotation; test near-rigidity.
  double col_len[3];
  for (int j = 0; j < 3; ++j) col_len[j] = std::sqrt((double)D.m[j] * D.m[j] + (double)D.m[4 + j] * D.m[4 + j] + (double)D.m[8 + j] * D.m[8 + j]);
  bool rigid = true;
  double r[9];
  for (int j = 0; j < 3; ++j) {
    if (col_len[j] < 0.5 || col_len[j] > 2.0) rigid = false;
    for (int i = 0; i < 3; ++i) r[i * 3 + j] = col_len[j] > 0 ? D.m[i * 4 + j] / col_len[j] : 0.0;
  }
  if (rigid) {
    for (int a = 0; a < 3 && rigid; ++a)
      for (int b = a + 1; b < 3; ++b) {
        double dot = r[a] * r[b] + r[3 + a] * r[3 + b] + r[6 + a] * r[6 + b];
        if (std::fabs(dot) > 0.05) { rigid = false; break; }
      }
    double det = r[0] * (r[4] * r[8] - r[5] * r[7]) - r[1] * (r[3] * r[8] - r[5] * r[6]) + r[2] * (r[3] * r[7] - r[4] * r[6]);
    if (det < 0.5) rigid = false;   // reflection or degenerate
  }
  double tx = D.m[3], ty = D.m[7], tz = D.m[11];
  double tlen = std::sqrt(tx * tx + ty * ty + tz * tz);
  if (tlen > max_translation) { exact(); if (stats) ++stats->cuts; return; }
  Affine F;   // fractional delta
  if (rigid) {
    Quat q = quat_from_rotation(r);
    if (q.w < 0) { q.w = -q.w; q.x = -q.x; q.y = -q.y; q.z = -q.z; }
    double angle = 2.0 * std::acos(std::min(1.0, q.w));
    if (angle > max_rotation) { exact(); if (stats) ++stats->cuts; return; }
    // Screw motion: rotate by t*angle about the same axis through the same point, slide t of the
    // way along the axis. F(0) = I, F(1) = D, and F(t) stays on the true rigid path between them.
    double half = angle * t * 0.5, sh = std::sin(half), axis_n = std::sqrt(q.x * q.x + q.y * q.y + q.z * q.z);
    double n[3] = {0, 0, 1};
    if (axis_n > 1e-9) { n[0] = q.x / axis_n; n[1] = q.y / axis_n; n[2] = q.z / axis_n; }
    Quat qt{std::cos(half), n[0] * sh, n[1] * sh, n[2] * sh};
    double rt[9]; rotation_from_quat(qt, rt);
    double st[3];
    for (int j = 0; j < 3; ++j) st[j] = std::pow(col_len[j], t);
    double T[3] = {tx, ty, tz}, ft[3];
    if (angle < 1e-6) {
      for (int i = 0; i < 3; ++i) ft[i] = T[i] * t;
    } else {
      double along = T[0] * n[0] + T[1] * n[1] + T[2] * n[2];
      double perp[3] = {T[0] - along * n[0], T[1] - along * n[1], T[2] - along * n[2]};
      double cross[3] = {n[1] * perp[2] - n[2] * perp[1], n[2] * perp[0] - n[0] * perp[2], n[0] * perp[1] - n[1] * perp[0]};
      double cot = std::cos(angle * 0.5) / std::sin(angle * 0.5);
      double p[3];   // a point on the screw axis: (I - R) p = perp
      for (int i = 0; i < 3; ++i) p[i] = 0.5 * (perp[i] + cot * cross[i]);
      for (int i = 0; i < 3; ++i) {
        double rp = rt[i * 3 + 0] * p[0] + rt[i * 3 + 1] * p[1] + rt[i * 3 + 2] * p[2];
        ft[i] = p[i] - rp + along * t * n[i];
      }
    }
    for (int i = 0; i < 3; ++i) {
      for (int j = 0; j < 3; ++j) F.m[i * 4 + j] = (float)(rt[i * 3 + j] * st[j]);
      F.m[i * 4 + 3] = (float)ft[i];
    }
    if (stats) ++stats->rigid;
  } else {
    // Shear/reflection/large scale deltas are not a reliable animation path.
    exact(); if (stats) ++stats->cuts; return;
  }
  Affine B; std::memcpy(B.m, base, sizeof B.m);
  Affine R = mul(F, B);
  std::memcpy(out_pos, R.m, sizeof R.m);
  if (out_nrm && base_nrm) {
    // Normals transform by inverse transpose, including non-uniform scale.
    Affine inverse;
    if (!invert(F, inverse)) { exact(); if (stats) ++stats->cuts; return; }
    for (int i = 0; i < 3; ++i)
      for (int j = 0; j < 3; ++j)
        out_nrm[i * 3 + j] = inverse.m[i] * base_nrm[j] +
            inverse.m[4 + i] * base_nrm[3 + j] + inverse.m[8 + i] * base_nrm[6 + j];
  }
}

void SubFrameSolver::set_frames(const Frame* prev, const Frame* cur) {
  if (prev && cur && cur->sequence != prev->sequence + 1) prev = nullptr;
  prev_ = prev; cur_ = cur;
  pairs_.clear(); prev_index_.clear();
  stats_ = SubFrameStats{};
  if (!cur) return;
  stats_.draws = (uint32_t)cur->draws.size();
  if (prev) for (size_t i = 0; i < prev->draws.size(); ++i) prev_index_.emplace(prev->draws[i].identity, (int)i);
  pairs_.resize(cur->draws.size());
  for (size_t i = 0; i < cur->draws.size(); ++i) {
    const DrawCall& d = cur->draws[i];
    Pair p{-1, 0, 0, 0, false, 0};
    auto it = prev_index_.find(d.identity);
    if (it != prev_index_.end()) {
      const DrawCall& pd = prev->draws[it->second];
      bool vertex_ranges_valid = pd.first_vertex <= prev->vertices.size() && pd.vertex_count <= prev->vertices.size() - pd.first_vertex &&
          d.first_vertex <= cur->vertices.size() && d.vertex_count <= cur->vertices.size() - d.first_vertex;
      // Same stream, byte for byte: the draw moves by its matrices alone. Otherwise the geometry
      // itself is animated, and the two streams are blended per presented frame (below) as long as
      // they still describe the same primitive with the same layout.
      bool same_vertices = vertex_ranges_valid && d.vertex_count && pd.vertex_count == d.vertex_count &&
          !std::memcmp(prev->vertices.data() + pd.first_vertex, cur->vertices.data() + d.first_vertex,
                       (size_t)d.vertex_count * sizeof(Vertex));
      bool valid = false;
      if (d.object_generation != pd.object_generation) ++stats_.missing;
      else if (d.xf_regs[0x26] != 0) ++stats_.hud;
      // Immediate-mode text and other unobserved primitives use texture/count/
      // submission-ordinal identities. Inserting or removing a glyph can reuse
      // that key for a different primitive. A changing stream needs an observed
      // object lifetime before it is safe to invent geometry between the draws.
      else if (!d.object_generation && !same_vertices) ++stats_.geometry;
      else if (!vertex_ranges_valid || !d.vertex_count || pd.vertex_count != d.vertex_count ||
          pd.primitive != d.primitive || pd.components != d.components) ++stats_.geometry;
      else if (!same_draw_bp(pd.bp, d.bp, stats_.state_register) || !same_textures(pd, d) ||
               !same_matrix_bindings(pd, d)) ++stats_.state;
      else if (std::memcmp(&pd.xf_regs[0x20], &d.xf_regs[0x20], 7 * sizeof(uint32_t))) ++stats_.projection;
      else { valid = true; p.blend_vertices = !same_vertices; }
      if (valid) {
        p.prev_draw = it->second;
        // Collect used position matrix slots: per-vertex indices or the CP default, plus texgen matrices.
        if (d.components & VB_HAS_POSMTXIDX) {
          for (uint32_t v = 0; v < d.vertex_count; ++v) {
            uint32_t idx = cur->vertices[d.first_vertex + v].posmtx;
            if (idx < 64) p.pos_slots |= 1ull << idx;
          }
        } else {
          p.pos_slots |= 1ull << (d.matrix_index_a & 63);
        }
        uint32_t texgens = d.xf_regs[0x3F] & 15;
        for (uint32_t k = 0; k < texgens && k < 8; ++k) {
          const uint32_t type = tmi_texgentype(d.xf_regs[0x40 + k]);
          if (type >= 1 && type <= 3) continue; // emboss/colour generators do not read these matrices
          if (d.components & (VB_HAS_TEXMTXIDX0 << k)) {
            for (uint32_t v = 0; v < d.vertex_count; ++v) {
              const uint32_t idx = cur->vertices[d.first_vertex + v].texmtx[k];
              if (idx < 64) p.tex_slots |= 1ull << idx;
            }
          } else {
            p.tex_slots |= 1ull << texture_matrix_index(d, k);
          }
        }
        p.used_slots = p.pos_slots | p.tex_slots;
        ++stats_.paired;
      }
    }
    if (it == prev_index_.end()) ++stats_.missing;
    pairs_[i] = p;
  }
  // Reserve a disjoint range of the blend buffer for every draw whose geometry is animated, so the
  // parallel solver chunks can fill them without sharing anything.
  size_t blended_vertices = 0;
  for (size_t i = 0; i < pairs_.size(); ++i) {
    if (!pairs_[i].blend_vertices) continue;
    pairs_[i].blend_offset = blended_vertices;
    blended_vertices += cur->draws[i].vertex_count;
  }
  vertex_blend_.resize(blended_vertices);
}

// Blends one draw's vertex stream between the two simulation frames. Sparks, shields, hit flashes
// and similar effects rewrite their vertices every frame rather than moving a matrix, so this is
// the only way they can move between simulation frames. Returns false when a vertex jumps far
// enough that the buffer is showing different geometry rather than the same geometry in motion.
static bool blend_vertex_stream(const Vertex* previous, const Vertex* current, uint32_t count,
                                double t, bool interpolate, float max_translation, Vertex* out) {
  const float phase = (float)t;
  for (uint32_t v = 0; v < count; ++v) {
    // Matrix selectors are discrete bindings, not animation channels. Blending
    // positions across a changed binding deforms a newly assigned primitive.
    if (previous[v].posmtx != current[v].posmtx ||
        std::memcmp(previous[v].texmtx, current[v].texmtx, sizeof current[v].texmtx)) return false;
    for (int k = 0; k < 3; ++k) {
      const float delta = current[v].pos[k] - previous[v].pos[k];
      if (!std::isfinite(delta) || std::abs(delta) > max_translation) return false;
    }
  }
  for (uint32_t v = 0; v < count; ++v) {
    const Vertex& a = previous[v];
    const Vertex& b = current[v];
    Vertex& o = out[v];
    o = b;   // indices, matrix selects and anything not blended come from the current frame
    const float* base_pos = interpolate ? a.pos : b.pos;
    for (int k = 0; k < 3; ++k) o.pos[k] = base_pos[k] + phase * (b.pos[k] - a.pos[k]);
    const float* base_nrm = interpolate ? a.nrm : b.nrm;
    for (int k = 0; k < 3; ++k) o.nrm[k] = base_nrm[k] + phase * (b.nrm[k] - a.nrm[k]);
    for (int k = 0; k < 8; ++k) {
      const float* base_uv = interpolate ? a.uv[k] : b.uv[k];
      o.uv[k][0] = base_uv[0] + phase * (b.uv[k][0] - a.uv[k][0]);
      o.uv[k][1] = base_uv[1] + phase * (b.uv[k][1] - a.uv[k][1]);
    }
    for (int k = 0; k < 4; ++k) {
      const int base0 = interpolate ? a.col0[k] : b.col0[k], base1 = interpolate ? a.col1[k] : b.col1[k];
      o.col0[k] = (uint8_t)std::min(255.0f, std::max(0.0f, base0 + phase * ((int)b.col0[k] - (int)a.col0[k])));
      o.col1[k] = (uint8_t)std::min(255.0f, std::max(0.0f, base1 + phase * ((int)b.col1[k] - (int)a.col1[k])));
    }
  }
  return true;
}

namespace {
// Persistent workers for the authored solver: every presented frame re-poses ~1000 draws, which
// are independent apart from the per-chain sample cache (kept per chunk; draws of one object are
// contiguous so chains rarely repeat across chunks).
class SolverPool {
 public:
  explicit SolverPool(int workers) {
    for (int i = 0; i < workers; ++i) threads_.emplace_back([this, i] { loop(i + 1); });
  }
  ~SolverPool() {
    { std::lock_guard<std::mutex> lk(m_); quit_ = true; }
    cv_.notify_all();
    for (auto& t : threads_) t.join();
  }
  int chunks() const { return (int)threads_.size() + 1; }
  // Runs fn(chunk) for chunk in [0, chunks()); the caller executes chunk 0.
  void run(const std::function<void(int)>& fn) {
    { std::lock_guard<std::mutex> lk(m_); job_ = &fn; pending_ = (int)threads_.size(); ++generation_; }
    cv_.notify_all();
    fn(0);
    std::unique_lock<std::mutex> lk(m_);
    done_.wait(lk, [&] { return pending_ == 0; });
    job_ = nullptr;
  }
 private:
  void loop(int chunk) {
    uint64_t seen = 0;
    for (;;) {
      const std::function<void(int)>* job;
      {
        std::unique_lock<std::mutex> lk(m_);
        cv_.wait(lk, [&] { return quit_ || generation_ != seen; });
        if (quit_) return;
        seen = generation_;
        job = job_;
      }
      (*job)(chunk);
      std::lock_guard<std::mutex> lk(m_);
      if (--pending_ == 0) done_.notify_one();
    }
  }
  std::vector<std::thread> threads_;
  std::mutex m_;
  std::condition_variable cv_, done_;
  const std::function<void(int)>* job_ = nullptr;
  int pending_ = 0;
  uint64_t generation_ = 0;
  bool quit_ = false;
};
SolverPool& solver_pool() { static SolverPool pool(3); return pool; }
}  // namespace

void SubFrameSolver::build(double t, bool interpolate, std::vector<DrawMatrices>& out, bool authored) const {
  out.resize(cur_ ? cur_->draws.size() : 0);
  if (!cur_) return;
  SubFrameStats* stats = &stats_;
  stats->rigid = stats->blended = stats->cuts = stats->authored = stats->carried = stats->vertex_blended = 0;
  if (authored) {
    // Sample forward from the latest state. Unsupported/discontinuous draws hold their current
    // matrices instead of inventing motion or adding a frame of delay. Chunks run in parallel.
    const size_t n = cur_->draws.size();
    set_authored_interpolate(interpolate);
    SolverPool& pool = solver_pool();
    const int chunks = n >= 128 ? pool.chunks() : 1;
    std::vector<uint32_t> counts((size_t)chunks, 0), carries((size_t)chunks, 0), blends((size_t)chunks, 0);
    auto work = [&](int chunk) {
      AuthoredCache chain_cache;   // one sampled chain per object per chunk per presented frame
      size_t begin = n * (size_t)chunk / (size_t)chunks, end = n * (size_t)(chunk + 1) / (size_t)chunks;
      uint32_t count = 0, carried = 0, blended = 0;
      for (size_t i = begin; i < end; ++i) {
        const DrawCall& d = cur_->draws[i];
        DrawMatrices& o = out[i];
        const Pair& p = pairs_[i];
        const DrawCall* pd = p.prev_draw >= 0 ? &prev_->draws[p.prev_draw] : nullptr;
        // Draws that cannot be sampled hold: at the current pose when predicting, at the previous
        // pose when interpolating, so every object stays on the same timeline.
        const DrawCall& hold = (interpolate && pd) ? *pd : d;
        std::memcpy(o.pos, hold.posMatrices, sizeof o.pos);
        std::memcpy(o.nrm, hold.normalMatrices, sizeof o.nrm);
        o.vertices = nullptr;
        if (!pd) continue;
        if (p.blend_vertices) {
          if (p.blend_offset + d.vertex_count > vertex_blend_.size() ||
              !blend_vertex_stream(&prev_->vertices[pd->first_vertex], &cur_->vertices[d.first_vertex], d.vertex_count,
                                   t, interpolate, max_translation, &vertex_blend_[p.blend_offset])) {
            // A rejected stream must also hold its current matrices. Combining
            // current vertices with an advanced or delayed matrix mixes timelines.
            std::memcpy(o.pos, d.posMatrices, sizeof o.pos);
            std::memcpy(o.nrm, d.normalMatrices, sizeof o.nrm);
            continue;
          }
          o.vertices = &vertex_blend_[p.blend_offset];
          ++blended;
        }
        // Texture-coordinate matrices animate independently of geometry: scrolling skies, water and
        // backdrops are driven entirely by them, and they used to advance only once per simulation
        // frame, which left most of the moving image stepping at 60 Hz. They are not rigid
        // transforms, so they advance by a straight per-element blend; a large jump holds.
        auto sample_textures = [&] {
          // A different starting row can still overlap a position matrix. Never
          // overwrite any of its rows with an independently sampled UV transform.
          const uint64_t position_rows = p.pos_slots | (p.pos_slots << 1) | (p.pos_slots << 2);
          for (int row = 0; row + 3 <= 64; ++row) {
            if (!(p.tex_slots & (1ull << row)) || (position_rows & (7ull << row))) continue;
            const float* previous_row = &pd->posMatrices[row * 4];
            const float* current_row = &d.posMatrices[row * 4];
            const float* base_row = &hold.posMatrices[row * 4];
            bool continuous = true;
            for (int k = 0; k < 12; ++k) {
              const float delta = current_row[k] - previous_row[k];
              if (!std::isfinite(delta) || std::abs(delta) > 16.0f) { continuous = false; break; }
            }
            if (!continuous) { std::memcpy(&o.pos[row * 4], current_row, 12 * sizeof(float)); continue; }
            for (int k = 0; k < 12; ++k) o.pos[row * 4 + k] = base_row[k] + (float)t * (current_row[k] - previous_row[k]);
          }
        };
        if (!d.authored_pose || !pd->authored_pose) { sample_textures(); continue; }
        bool posed = false;
        if (d.authored_pose->envelope) {
          posed = sample_authored_envelope(*pd->authored_pose, *d.authored_pose, t, d.posMatrices, d.normalMatrices, o.pos, o.nrm, &chain_cache);
        } else if (!(d.components & VB_HAS_POSMTXIDX)) {
          // One chain, so one position matrix: the draw's own row, not necessarily row 0. Stage and
          // effect geometry commonly sits at a higher index and used to be skipped outright.
          const uint32_t row = d.matrix_index_a & 63;
          if (row + 3 <= 64) {
            const bool has_normals = row + 3 <= 32;
            static const float identity_normals[9] = {1, 0, 0, 0, 1, 0, 0, 0, 1};
            float sampled_pos[12], sampled_nrm[9];
            if (sample_authored(*pd->authored_pose, *d.authored_pose, t, &d.posMatrices[row * 4], sampled_pos, sampled_nrm,
                                has_normals ? &d.normalMatrices[row * 3] : identity_normals, &chain_cache)) {
              std::memcpy(&o.pos[row * 4], sampled_pos, sizeof sampled_pos);
              if (has_normals) std::memcpy(&o.nrm[row * 3], sampled_nrm, sizeof sampled_nrm);
              posed = true;
            }
          }
        }
        if (posed) ++count;
        else if (carry_camera(*pd->authored_pose, *d.authored_pose, t, hold.posMatrices, hold.normalMatrices, p.pos_slots, o.pos, o.nrm)) ++carried;
        // Envelope sampling publishes a complete matrix array. Apply disjoint UV
        // animation afterwards so that publication cannot erase the sampled UVs.
        sample_textures();
      }
      counts[(size_t)chunk] = count;
      carries[(size_t)chunk] = carried;
      blends[(size_t)chunk] = blended;
    };
    if (chunks == 1) work(0); else pool.run(work);
    for (uint32_t c : counts) stats->authored += c;
    for (uint32_t c : carries) stats->carried += c;
    for (uint32_t c : blends) stats->vertex_blended += c;
    return;
  }
  for (size_t i = 0; i < cur_->draws.size(); ++i) {
    const DrawCall& d = cur_->draws[i];
    DrawMatrices& o = out[i];
    const Pair& p = pairs_[i];
    const DrawCall* pd = p.prev_draw >= 0 ? &prev_->draws[p.prev_draw] : nullptr;
    const DrawCall& base = (interpolate && pd) ? *pd : d;
    o.vertices = nullptr;
    std::memcpy(o.pos, base.posMatrices, sizeof o.pos);
    std::memcpy(o.nrm, base.normalMatrices, sizeof o.nrm);
    if (!pd) continue;
    for (int row = 0; row < 64; ++row) {
      if (!(p.used_slots & (1ull << row))) continue;
      if (row + 3 > 64) break;
      const float* prev_m = &pd->posMatrices[row * 4];
      const float* cur_m = &d.posMatrices[row * 4];
      // Normal matrix for pos index `row` lives at normalMatrices[row*3 ..] when row < 32.
      bool has_nrm = row < 32 && row + 3 <= 32;
      float nrm_out[9];
      fractional(prev_m, cur_m, t, interpolate, max_translation, max_rotation, &o.pos[row * 4],
                 has_nrm ? nrm_out : nullptr, has_nrm ? &d.normalMatrices[row * 3] : nullptr,
                 has_nrm ? &pd->normalMatrices[row * 3] : nullptr, stats);
      if (has_nrm) std::memcpy(&o.nrm[row * 3], nrm_out, sizeof nrm_out);
    }
  }
}

}  // namespace gx
