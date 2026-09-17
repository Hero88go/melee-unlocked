// GX command processor: decodes the FIFO stream into register state and captured frames.
// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include <cstdint>
#include <memory>
#include <vector>
#include "gx_regs.h"
#include "texture_snapshot.h"

namespace gx {

constexpr int EFB_WIDTH = 640, EFB_HEIGHT = 528;

#pragma pack(push, 1)
struct Vertex {
  float pos[3];
  float nrm[3];
  uint8_t col0[4];
  uint8_t col1[4];
  float uv[8][2];
  uint8_t posmtx;
  uint8_t texmtx[8];
  uint8_t pad[3];
};
#pragma pack(pop)
static_assert(sizeof(Vertex) == 108, "vertex layout");

// Components present in the vertex stream (VB_HAS_* in Dolphin).
enum : uint32_t {
  VB_HAS_POSMTXIDX = 1u << 0,
  VB_HAS_TEXMTXIDX0 = 1u << 1,   // ..7 -> bits 1..8
  VB_HAS_NRM0 = 1u << 10, VB_HAS_NRM1 = 1u << 11, VB_HAS_NRM2 = 1u << 12,
  VB_HAS_COL0 = 1u << 13, VB_HAS_COL1 = 1u << 14,
  VB_HAS_UV0 = 1u << 15,         // ..7 -> bits 15..22
};

// Snapshot of everything a draw needs; the backend replays these.
struct TextureRef {
  uint32_t addr = 0, width = 0, height = 0, format = 0, tlut_addr = 0, tlut_format = 0;
  uint32_t mode0 = 0, mode1 = 0;   // wrap/filter/lod
  uint32_t mip_levels = 1;
  std::shared_ptr<const TextureSnapshot> data;
  bool used = false;
};

struct AuthoredPose;
struct DrawCall {
  // ~5 KB of register and matrix snapshots, recorded ~1400 times per simulation frame. Zeroing all
  // of that costs real time on the simulation thread, so record_draw (which overwrites every one of
  // those arrays) opts out with the tag below. Every other caller gets the safe zeroed default.
  struct SkipInit {};
  DrawCall() = default;
  explicit DrawCall(SkipInit) {}
  uint32_t primitive;              // GX primitive opcode & 0xF8
  uint32_t first_vertex, vertex_count;
  uint32_t components;
  // Register snapshots used by shader generation and pipeline state.
  BPMemory bp;
  // XF subset needed by the vertex shader (matrices are copied in full: simplest and rollback-safe).
  float posMatrices[256];
  float normalMatrices[96];
  float postMatrices[256];
  uint8_t lights[8][64];           // raw light blocks (Light struct is 64 bytes)
  uint32_t xf_regs[0x58];          // 0x1000..0x1057
  uint32_t matrix_index_a, matrix_index_b;
  int32_t tev_colors[4][4];   // RGBA, signed 11-bit (BP E0-E7 writes with type 0)
  int32_t tev_kcolors[4][4];  // RGBA (type 1 writes)
  TextureRef textures[8] = {};     // snapshot_textures relies on `used` starting false
  int viewport_x = 0, viewport_y = 0, viewport_w = 0, viewport_h = 0;  // unused placeholders (computed by backend)
  // Stable identity of this draw across frames (display-list address + call ordinal + draw ordinal,
  // or texture/size/ordinal for immediate-mode draws). Used to pair draws for sub-frame rendering.
  uint64_t identity = 0;
  std::shared_ptr<const AuthoredPose> authored_pose;
  uint64_t object_generation = 0; // Allocated JObj lifetime; zero means unobserved.
  // Player slot (0..5) whose fighter rendered this draw, 0xFF for everything else. Display only:
  // it feeds the per-player tint, is not part of `identity` and never reaches a shader UID.
  uint8_t owner_player = 0xFF;
  // Skinned (envelope) draw: the fighter's model. Its shadow and its effects are rigid draws, so
  // this is what tells the model apart from everything else the same player renders.
  bool skinned = false;
  // Render-thread cache: the pipeline resolved for this draw (valid for the backend that set it).
  // Sub-frames re-present the same draws, so the shader UIDs are hashed once per simulation frame.
  mutable void* cached_pipeline = nullptr;
  mutable uint64_t cached_pipeline_owner = 0;
};

// Replacement transform state for one draw when a sub-frame is rendered between simulation frames.
struct DrawMatrices {
  float pos[256];
  float nrm[96];
  // Effects that rewrite their vertex stream every simulation frame (sparks, shields, hit flashes)
  // cannot be moved by a matrix. When set, the renderer draws these vertices instead of the frame's.
  const Vertex* vertices = nullptr;
};

struct EfbCopy {
  uint32_t dest_addr, dest_stride, src_x, src_y, src_w, src_h;
  uint32_t format;   // copy format (tp_realFormat)
  bool to_xfb, clear, intensity, half_scale, is_depth;
  uint32_t clear_color, clear_z;  // ARGB, 24-bit z
  float y_scale;
};

struct FrameCommand {
  enum Kind { Draw, Copy } kind;
  uint32_t index;   // into draws or copies
};

struct Frame {
  std::vector<Vertex> vertices;
  std::vector<DrawCall> draws;
  std::vector<EfbCopy> copies;
  std::vector<FrameCommand> commands;
  uint64_t sequence = 0;
  double time = 0.0;   // host seconds of the retrace this frame belongs to (see host::frame_time)
  // sequence and time are reset too: a recycled frame is handed back to the producer as "cleared",
  // and the renderer decides when to present by comparing sequences. Leaving a stale one on a
  // buffer that is about to be refilled is only harmless while every producer remembers to assign
  // one before pushing.
  void clear() { vertices.clear(); draws.clear(); copies.clear(); commands.clear(); sequence = 0; time = 0.0; }
};

// Renderer interface implemented by the D3D12 backend (or a null backend).
struct Backend {
  virtual ~Backend() = default;
  virtual void set_present_deadline(double) {}
  virtual double presentation_wait_seconds() const { return 0; }
  virtual void submit_frame(const Frame& frame) = 0;   // called at XFB copy
  // Queueing backends take the frame's buffers and hand back recycled ones (no copy, no
  // per-frame reallocation on the simulation thread); `frame` comes back cleared either way.
  virtual void submit_and_recycle(Frame& frame) { submit_frame(frame); frame.clear(); }
  // Render `frame` with per-draw transform overrides (one entry per frame.draws element); the
  // default ignores the overrides. Used by the sub-frame presenter for unlocked frame rates.
  virtual void submit_frame(const Frame& frame, const DrawMatrices* overrides) { (void)overrides; submit_frame(frame); }
  // Drain mode: execute the frame's commands (copies, clears, uploads) but do not blit or present it.
  virtual void set_skip_present(bool) {}
};

void init(Backend* backend);
void write_fifo(uint32_t value, int bytes);   // write-gather pipe byte stream
void stats(uint64_t* commands, uint64_t* draws, uint64_t* vertices, uint32_t* efb_copies);
const uint8_t* tmem();

}  // namespace gx
